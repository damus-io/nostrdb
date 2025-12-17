/*
 * ndb_negentropy.c - Native Negentropy for NostrDB
 *
 * Implementation of the negentropy set reconciliation protocol.
 * See ndb_negentropy.h for API documentation.
 */

#include "ndb_negentropy.h"
#include <string.h>

/* ============================================================
 * VARINT ENCODING/DECODING
 * ============================================================
 *
 * Negentropy varints are MSB-first (most significant byte first).
 * This is the opposite of the common LEB128 encoding.
 *
 * Encoding strategy:
 * 1. Determine how many 7-bit groups we need
 * 2. Write them MSB-first, setting the high bit on all but the last
 *
 * Example: Encoding 300 (0x12C)
 *   - Binary: 0000 0001 0010 1100
 *   - 7-bit groups (MSB first): 0000010, 0101100
 *   - Add continuation bits: 10000010, 00101100
 *   - Result: 0x82 0x2C
 */


/*
 * Calculate how many bytes a value needs when encoded as a varint.
 *
 * Each byte encodes 7 bits of data, so we count how many 7-bit
 * groups are needed to represent the value.
 */
int ndb_negentropy_varint_size(uint64_t n)
{
	int size;

	/* Zero needs exactly one byte */
	if (n == 0)
		return 1;

	/* Count 7-bit groups needed */
	size = 0;
	while (n > 0) {
		size++;
		n >>= 7;
	}

	return size;
}


/*
 * Encode a 64-bit value as an MSB-first varint.
 *
 * We first calculate the size, then write bytes from most significant
 * to least significant. All bytes except the last have the high bit set.
 */
int ndb_negentropy_varint_encode(unsigned char *buf, size_t buflen, uint64_t n)
{
	int size;
	int i;

	/* Calculate required size */
	size = ndb_negentropy_varint_size(n);

	/* Guard: ensure buffer is large enough */
	if (buflen < (size_t)size)
		return 0;

	/*
	 * Write bytes from right to left (LSB to MSB position in buffer).
	 * The rightmost byte (last written) has no continuation bit.
	 * All others have the high bit set.
	 */
	for (i = size - 1; i >= 0; i--) {
		/* Extract lowest 7 bits */
		unsigned char byte = n & 0x7F;

		/* Set continuation bit on all but the last byte */
		if (i != size - 1)
			byte |= 0x80;

		buf[i] = byte;
		n >>= 7;
	}

	return size;
}


/*
 * Decode an MSB-first varint from a buffer.
 *
 * Read bytes until we find one without the continuation bit (high bit).
 * Maximum length is 10 bytes (ceil(64/7) = 10).
 */
int ndb_negentropy_varint_decode(const unsigned char *buf, size_t buflen,
                                  uint64_t *out)
{
	uint64_t result;
	size_t i;

	/* Guard: need at least one byte */
	if (buflen == 0)
		return 0;

	/* Guard: output pointer must be valid */
	if (out == NULL)
		return 0;

	result = 0;

	for (i = 0; i < buflen && i < 10; i++) {
		unsigned char byte = buf[i];

		/*
		 * Shift existing value left by 7 bits and add new 7 bits.
		 * This builds the value MSB-first.
		 */
		result = (result << 7) | (byte & 0x7F);

		/* If high bit is not set, this is the last byte */
		if ((byte & 0x80) == 0) {
			*out = result;
			return (int)(i + 1);
		}
	}

	/*
	 * If we get here, either:
	 * - We consumed 10 bytes without finding a terminator (malformed)
	 * - We ran out of buffer (incomplete)
	 */
	return 0;
}


/* ============================================================
 * HEX ENCODING UTILITIES
 * ============================================================
 */

/* Lookup table for hex encoding (lowercase as per nostr convention) */
static const char hex_chars[] = "0123456789abcdef";


/*
 * Convert binary data to lowercase hex string.
 *
 * Each input byte becomes two hex characters.
 * Output is NUL-terminated.
 */
size_t ndb_negentropy_to_hex(const unsigned char *bin, size_t len, char *hex)
{
	size_t i;

	for (i = 0; i < len; i++) {
		hex[i * 2]     = hex_chars[(bin[i] >> 4) & 0x0F];
		hex[i * 2 + 1] = hex_chars[bin[i] & 0x0F];
	}

	hex[len * 2] = '\0';
	return len * 2;
}


/*
 * Convert a single hex character to its numeric value.
 * Returns -1 for invalid characters.
 */
static int hex_char_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';

	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;

	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return -1;
}


/*
 * Convert hex string to binary data.
 *
 * Input length must be even (two hex chars per byte).
 * Invalid hex characters cause an error return.
 */
size_t ndb_negentropy_from_hex(const char *hex, size_t hexlen,
                                unsigned char *bin, size_t binlen)
{
	size_t i;
	size_t out_len;
	int high, low;

	/* Guard: hex string must have even length */
	if (hexlen % 2 != 0)
		return 0;

	out_len = hexlen / 2;

	/* Guard: output buffer must be large enough */
	if (binlen < out_len)
		return 0;

	for (i = 0; i < out_len; i++) {
		high = hex_char_value(hex[i * 2]);
		low  = hex_char_value(hex[i * 2 + 1]);

		/* Guard: both characters must be valid hex */
		if (high < 0 || low < 0)
			return 0;

		bin[i] = (unsigned char)((high << 4) | low);
	}

	return out_len;
}


/* ============================================================
 * FINGERPRINT COMPUTATION
 * ============================================================
 */

/*
 * Initialize accumulator to zero.
 */
void ndb_negentropy_accumulator_init(struct ndb_negentropy_accumulator *acc)
{
	memset(acc->sum, 0, sizeof(acc->sum));
}


/*
 * Add a 32-byte ID to the accumulator (mod 2^256).
 *
 * Both the accumulator and ID are treated as little-endian 256-bit
 * unsigned integers. We perform byte-by-byte addition with carry
 * propagation. Any final carry is discarded (mod 2^256).
 */
void ndb_negentropy_accumulator_add(struct ndb_negentropy_accumulator *acc,
                                     const unsigned char *id)
{
	int i;
	uint16_t carry = 0;

	/*
	 * Add byte-by-byte, propagating carry.
	 * Little-endian: byte 0 is least significant.
	 */
	for (i = 0; i < 32; i++) {
		uint16_t sum = (uint16_t)acc->sum[i] + (uint16_t)id[i] + carry;
		acc->sum[i] = (unsigned char)(sum & 0xFF);
		carry = sum >> 8;
	}

	/* Carry overflow is discarded (mod 2^256) */
}


/*
 * Compute fingerprint from accumulator and count.
 *
 * The fingerprint is: SHA256(sum || varint(count))[:16]
 *
 * We need access to SHA256. NostrDB uses the ccan/crypto/sha256 library.
 */
#include "ccan/crypto/sha256/sha256.h"

void ndb_negentropy_fingerprint(const struct ndb_negentropy_accumulator *acc,
                                 size_t count,
                                 unsigned char *out)
{
	struct sha256 hash;
	unsigned char buf[32 + 10];  /* 32-byte sum + up to 10-byte varint */
	int varint_len;
	size_t total_len;

	/* Copy the 32-byte sum */
	memcpy(buf, acc->sum, 32);

	/* Append count as varint */
	varint_len = ndb_negentropy_varint_encode(buf + 32, 10, (uint64_t)count);
	total_len = 32 + (size_t)varint_len;

	/* Hash and take first 16 bytes */
	sha256(&hash, buf, total_len);
	memcpy(out, hash.u.u8, 16);
}


/* ============================================================
 * BOUND ENCODING/DECODING
 * ============================================================
 */

/*
 * Encode a bound into a buffer.
 *
 * Format: <encodedTimestamp (Varint)> <prefixLen (Varint)> <idPrefix (bytes)>
 *
 * Timestamp encoding:
 * - UINT64_MAX ("infinity") encodes as 0
 * - All other values encode as (1 + delta_from_previous)
 */
int ndb_negentropy_bound_encode(unsigned char *buf, size_t buflen,
                                 const struct ndb_negentropy_bound *bound,
                                 uint64_t *prev_timestamp)
{
	size_t offset = 0;
	int written;
	uint64_t encoded_ts;

	/* Guard: validate inputs */
	if (buf == NULL || bound == NULL || prev_timestamp == NULL)
		return 0;

	/*
	 * Encode timestamp:
	 * - Infinity (UINT64_MAX) -> 0
	 * - Otherwise -> 1 + (timestamp - prev_timestamp)
	 */
	if (bound->timestamp == UINT64_MAX) {
		encoded_ts = 0;
	} else {
		uint64_t delta = bound->timestamp - *prev_timestamp;
		encoded_ts = 1 + delta;
		*prev_timestamp = bound->timestamp;
	}

	/* Write encoded timestamp */
	written = ndb_negentropy_varint_encode(buf + offset, buflen - offset, encoded_ts);
	if (written == 0)
		return 0;
	offset += (size_t)written;

	/* Write prefix length */
	written = ndb_negentropy_varint_encode(buf + offset, buflen - offset,
	                                        (uint64_t)bound->prefix_len);
	if (written == 0)
		return 0;
	offset += (size_t)written;

	/* Guard: ensure room for prefix bytes */
	if (offset + bound->prefix_len > buflen)
		return 0;

	/* Write ID prefix bytes */
	if (bound->prefix_len > 0)
		memcpy(buf + offset, bound->id_prefix, bound->prefix_len);
	offset += bound->prefix_len;

	return (int)offset;
}


/*
 * Decode a bound from a buffer.
 */
int ndb_negentropy_bound_decode(const unsigned char *buf, size_t buflen,
                                 struct ndb_negentropy_bound *bound,
                                 uint64_t *prev_timestamp)
{
	size_t offset = 0;
	int consumed;
	uint64_t encoded_ts;
	uint64_t prefix_len;

	/* Guard: validate inputs */
	if (buf == NULL || bound == NULL || prev_timestamp == NULL)
		return 0;

	/* Read encoded timestamp */
	consumed = ndb_negentropy_varint_decode(buf + offset, buflen - offset, &encoded_ts);
	if (consumed == 0)
		return 0;
	offset += (size_t)consumed;

	/*
	 * Decode timestamp:
	 * - 0 -> Infinity (UINT64_MAX)
	 * - Otherwise -> prev_timestamp + (encoded_ts - 1)
	 */
	if (encoded_ts == 0) {
		bound->timestamp = UINT64_MAX;
	} else {
		bound->timestamp = *prev_timestamp + (encoded_ts - 1);
		*prev_timestamp = bound->timestamp;
	}

	/* Read prefix length */
	consumed = ndb_negentropy_varint_decode(buf + offset, buflen - offset, &prefix_len);
	if (consumed == 0)
		return 0;
	offset += (size_t)consumed;

	/* Guard: prefix length must be <= 32 */
	if (prefix_len > 32)
		return 0;

	bound->prefix_len = (uint8_t)prefix_len;

	/* Guard: ensure buffer has enough bytes for prefix */
	if (offset + bound->prefix_len > buflen)
		return 0;

	/* Read ID prefix bytes, zero the rest */
	memset(bound->id_prefix, 0, 32);
	if (bound->prefix_len > 0)
		memcpy(bound->id_prefix, buf + offset, bound->prefix_len);
	offset += bound->prefix_len;

	return (int)offset;
}


/* ============================================================
 * RANGE ENCODING/DECODING
 * ============================================================
 *
 * Ranges are the core unit of negentropy messages. Each range
 * specifies a section of the item space and what to do with it.
 */


/*
 * Encode a range into a buffer.
 *
 * Format: <bound> <mode> <payload>
 *
 * We use early returns for each error condition to avoid deep nesting.
 */
int ndb_negentropy_range_encode(unsigned char *buf, size_t buflen,
                                 const struct ndb_negentropy_range *range,
                                 uint64_t *prev_timestamp)
{
	size_t offset = 0;
	int written;

	/* Guard: validate inputs */
	if (buf == NULL || range == NULL || prev_timestamp == NULL)
		return 0;

	/* Encode the upper bound */
	written = ndb_negentropy_bound_encode(buf + offset, buflen - offset,
	                                       &range->upper_bound, prev_timestamp);
	if (written == 0)
		return 0;
	offset += (size_t)written;

	/* Encode the mode */
	written = ndb_negentropy_varint_encode(buf + offset, buflen - offset,
	                                        (uint64_t)range->mode);
	if (written == 0)
		return 0;
	offset += (size_t)written;

	/* Encode the payload based on mode */
	switch (range->mode) {

	case NDB_NEG_SKIP:
		/* No payload for SKIP mode */
		break;

	case NDB_NEG_FINGERPRINT:
		/* 16-byte fingerprint */
		if (offset + 16 > buflen)
			return 0;
		memcpy(buf + offset, range->payload.fingerprint, 16);
		offset += 16;
		break;

	case NDB_NEG_IDLIST: {
		/*
		 * IdList: <count (Varint)> <ids (32 bytes each)>
		 */
		size_t id_count = range->payload.id_list.id_count;
		size_t ids_size = id_count * 32;

		/* Write count */
		written = ndb_negentropy_varint_encode(buf + offset, buflen - offset,
		                                        (uint64_t)id_count);
		if (written == 0)
			return 0;
		offset += (size_t)written;

		/* Guard: ensure room for all IDs */
		if (offset + ids_size > buflen)
			return 0;

		/* Write IDs */
		if (id_count > 0 && range->payload.id_list.ids != NULL)
			memcpy(buf + offset, range->payload.id_list.ids, ids_size);
		offset += ids_size;
		break;
	}

	case NDB_NEG_IDLIST_RESPONSE: {
		/*
		 * IdListResponse:
		 *   <haveIds (IdList)> <bitfieldLen (Varint)> <bitfield>
		 *
		 * haveIds is an IdList (count + ids) of IDs the server has.
		 * bitfield indicates which client IDs the server needs.
		 */
		size_t have_count = range->payload.id_list_response.have_count;
		size_t have_size = have_count * 32;
		size_t bf_len = range->payload.id_list_response.bitfield_len;

		/* Write have_count */
		written = ndb_negentropy_varint_encode(buf + offset, buflen - offset,
		                                        (uint64_t)have_count);
		if (written == 0)
			return 0;
		offset += (size_t)written;

		/* Guard: ensure room for have_ids */
		if (offset + have_size > buflen)
			return 0;

		/* Write have_ids */
		if (have_count > 0 && range->payload.id_list_response.have_ids != NULL)
			memcpy(buf + offset, range->payload.id_list_response.have_ids, have_size);
		offset += have_size;

		/* Write bitfield length */
		written = ndb_negentropy_varint_encode(buf + offset, buflen - offset,
		                                        (uint64_t)bf_len);
		if (written == 0)
			return 0;
		offset += (size_t)written;

		/* Guard: ensure room for bitfield */
		if (offset + bf_len > buflen)
			return 0;

		/* Write bitfield */
		if (bf_len > 0 && range->payload.id_list_response.bitfield != NULL)
			memcpy(buf + offset, range->payload.id_list_response.bitfield, bf_len);
		offset += bf_len;
		break;
	}

	default:
		/* Unknown mode */
		return 0;
	}

	return (int)offset;
}


/*
 * Decode a range from a buffer.
 *
 * For IDLIST and IDLIST_RESPONSE modes, the payload pointers point
 * directly into the input buffer for zero-copy access.
 */
int ndb_negentropy_range_decode(const unsigned char *buf, size_t buflen,
                                 struct ndb_negentropy_range *range,
                                 uint64_t *prev_timestamp)
{
	size_t offset = 0;
	int consumed;
	uint64_t mode_val;

	/* Guard: validate inputs */
	if (buf == NULL || range == NULL || prev_timestamp == NULL)
		return 0;

	/* Decode the upper bound */
	consumed = ndb_negentropy_bound_decode(buf + offset, buflen - offset,
	                                        &range->upper_bound, prev_timestamp);
	if (consumed == 0)
		return 0;
	offset += (size_t)consumed;

	/* Decode the mode */
	consumed = ndb_negentropy_varint_decode(buf + offset, buflen - offset, &mode_val);
	if (consumed == 0)
		return 0;
	offset += (size_t)consumed;

	/* Guard: mode must be valid */
	if (mode_val > NDB_NEG_IDLIST_RESPONSE)
		return 0;
	range->mode = (enum ndb_negentropy_mode)mode_val;

	/* Decode payload based on mode */
	switch (range->mode) {

	case NDB_NEG_SKIP:
		/* No payload */
		break;

	case NDB_NEG_FINGERPRINT:
		/* 16-byte fingerprint */
		if (offset + 16 > buflen)
			return 0;
		memcpy(range->payload.fingerprint, buf + offset, 16);
		offset += 16;
		break;

	case NDB_NEG_IDLIST: {
		/*
		 * IdList: <count (Varint)> <ids (32 bytes each)>
		 */
		uint64_t id_count;
		size_t ids_size;

		/* Read count */
		consumed = ndb_negentropy_varint_decode(buf + offset, buflen - offset, &id_count);
		if (consumed == 0)
			return 0;
		offset += (size_t)consumed;

		ids_size = (size_t)id_count * 32;

		/* Guard: ensure buffer has all IDs */
		if (offset + ids_size > buflen)
			return 0;

		/* Point directly into buffer (zero-copy) */
		range->payload.id_list.id_count = (size_t)id_count;
		range->payload.id_list.ids = (id_count > 0) ? (buf + offset) : NULL;
		offset += ids_size;
		break;
	}

	case NDB_NEG_IDLIST_RESPONSE: {
		/*
		 * IdListResponse:
		 *   <haveIds (IdList)> <bitfieldLen (Varint)> <bitfield>
		 */
		uint64_t have_count;
		size_t have_size;
		uint64_t bf_len;

		/* Read have_count */
		consumed = ndb_negentropy_varint_decode(buf + offset, buflen - offset, &have_count);
		if (consumed == 0)
			return 0;
		offset += (size_t)consumed;

		have_size = (size_t)have_count * 32;

		/* Guard: ensure buffer has all have_ids */
		if (offset + have_size > buflen)
			return 0;

		/* Point directly into buffer (zero-copy) */
		range->payload.id_list_response.have_count = (size_t)have_count;
		range->payload.id_list_response.have_ids = (have_count > 0) ? (buf + offset) : NULL;
		offset += have_size;

		/* Read bitfield length */
		consumed = ndb_negentropy_varint_decode(buf + offset, buflen - offset, &bf_len);
		if (consumed == 0)
			return 0;
		offset += (size_t)consumed;

		/* Guard: ensure buffer has bitfield */
		if (offset + bf_len > buflen)
			return 0;

		/* Point directly into buffer (zero-copy) */
		range->payload.id_list_response.bitfield_len = (size_t)bf_len;
		range->payload.id_list_response.bitfield = (bf_len > 0) ? (buf + offset) : NULL;
		offset += bf_len;
		break;
	}

	default:
		/* Unknown mode */
		return 0;
	}

	return (int)offset;
}


/* ============================================================
 * MESSAGE ENCODING/DECODING
 * ============================================================
 *
 * Messages are the complete wire-format units. Each message
 * starts with a version byte followed by concatenated ranges.
 */


/*
 * Encode a complete negentropy message.
 *
 * Format: <version (1 byte)> <range>*
 */
int ndb_negentropy_message_encode(unsigned char *buf, size_t buflen,
                                   const struct ndb_negentropy_range *ranges,
                                   size_t num_ranges)
{
	size_t offset = 0;
	size_t i;
	uint64_t prev_timestamp = 0;
	int written;

	/* Guard: need at least 1 byte for version */
	if (buf == NULL || buflen < 1)
		return 0;

	/* Guard: enforce range limit for DOS protection */
	if (num_ranges > NDB_NEGENTROPY_MAX_RANGES)
		return 0;

	/* Write protocol version byte */
	buf[offset++] = NDB_NEGENTROPY_PROTOCOL_V1;

	/* Encode each range */
	for (i = 0; i < num_ranges; i++) {
		written = ndb_negentropy_range_encode(buf + offset, buflen - offset,
		                                       &ranges[i], &prev_timestamp);
		if (written == 0)
			return 0;

		offset += (size_t)written;
	}

	return (int)offset;
}


/*
 * Get the protocol version from a message.
 *
 * Simply returns the first byte, which is the version.
 */
int ndb_negentropy_message_version(const unsigned char *buf, size_t buflen)
{
	if (buf == NULL || buflen < 1)
		return 0;

	return (int)buf[0];
}


/*
 * Count ranges in a message.
 *
 * We parse through the message skipping the version byte,
 * then iterate through ranges counting each one.
 */
int ndb_negentropy_message_count_ranges(const unsigned char *buf, size_t buflen)
{
	const unsigned char *p;
	size_t remaining;
	uint64_t prev_timestamp = 0;
	struct ndb_negentropy_range range;
	int count = 0;
	int consumed;

	/* Guard: need at least version byte */
	if (buf == NULL || buflen < 1)
		return -1;

	/* Check version is V1 */
	if (buf[0] != NDB_NEGENTROPY_PROTOCOL_V1)
		return -1;

	/* Skip version byte */
	p = buf + 1;
	remaining = buflen - 1;

	/*
	 * Parse ranges until buffer exhausted.
	 * We use the actual decode function to ensure we count
	 * correctly even with complex payloads.
	 */
	while (remaining > 0) {
		consumed = ndb_negentropy_range_decode(p, remaining, &range, &prev_timestamp);

		/* Decode error */
		if (consumed == 0)
			return -1;

		/* Guard: ensure we don't exceed limit */
		count++;
		if (count > NDB_NEGENTROPY_MAX_RANGES)
			return -1;

		p += consumed;
		remaining -= (size_t)consumed;
	}

	return count;
}
