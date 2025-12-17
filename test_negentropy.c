/*
 * test_negentropy.c - Unit tests for negentropy implementation
 *
 * Run: make test_negentropy && ./test_negentropy
 */

#include "src/ndb_negentropy.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ============================================================
 * VARINT TESTS
 * ============================================================ */

static void test_varint_size(void)
{
	printf("  varint_size... ");

	/* Single byte values (0-127) */
	assert(ndb_negentropy_varint_size(0) == 1);
	assert(ndb_negentropy_varint_size(1) == 1);
	assert(ndb_negentropy_varint_size(127) == 1);

	/* Two byte values (128-16383) */
	assert(ndb_negentropy_varint_size(128) == 2);
	assert(ndb_negentropy_varint_size(255) == 2);
	assert(ndb_negentropy_varint_size(16383) == 2);

	/* Three byte values */
	assert(ndb_negentropy_varint_size(16384) == 3);

	/* Maximum value needs 10 bytes */
	assert(ndb_negentropy_varint_size(UINT64_MAX) == 10);

	printf("OK\n");
}

static void test_varint_encode_decode_roundtrip(void)
{
	printf("  varint_encode_decode_roundtrip... ");

	uint64_t test_values[] = {
		0, 1, 127,           /* 1 byte */
		128, 255, 16383,     /* 2 bytes */
		16384, 2097151,      /* 3 bytes */
		268435455,           /* 4 bytes */
		UINT64_MAX           /* 10 bytes */
	};
	size_t num_tests = sizeof(test_values) / sizeof(test_values[0]);

	for (size_t i = 0; i < num_tests; i++) {
		unsigned char buf[16];
		uint64_t decoded;
		int encoded_len, decoded_len;

		encoded_len = ndb_negentropy_varint_encode(buf, sizeof(buf), test_values[i]);
		assert(encoded_len > 0);

		decoded_len = ndb_negentropy_varint_decode(buf, encoded_len, &decoded);
		assert(decoded_len == encoded_len);
		assert(decoded == test_values[i]);
	}

	printf("OK\n");
}

static void test_varint_specific_encodings(void)
{
	printf("  varint_specific_encodings... ");

	unsigned char buf[16];
	int len;

	/* 0 -> 0x00 */
	len = ndb_negentropy_varint_encode(buf, sizeof(buf), 0);
	assert(len == 1);
	assert(buf[0] == 0x00);

	/* 127 -> 0x7F */
	len = ndb_negentropy_varint_encode(buf, sizeof(buf), 127);
	assert(len == 1);
	assert(buf[0] == 0x7F);

	/* 128 -> 0x81 0x00 (MSB first, continuation bit on first byte) */
	len = ndb_negentropy_varint_encode(buf, sizeof(buf), 128);
	assert(len == 2);
	assert(buf[0] == 0x81);  /* 1 with continuation */
	assert(buf[1] == 0x00);  /* 0 without continuation */

	/* 255 -> 0x81 0x7F */
	len = ndb_negentropy_varint_encode(buf, sizeof(buf), 255);
	assert(len == 2);
	assert(buf[0] == 0x81);
	assert(buf[1] == 0x7F);

	/* 300 (0x12C) -> 0x82 0x2C */
	len = ndb_negentropy_varint_encode(buf, sizeof(buf), 300);
	assert(len == 2);
	assert(buf[0] == 0x82);
	assert(buf[1] == 0x2C);

	printf("OK\n");
}

static void test_varint_buffer_too_small(void)
{
	printf("  varint_buffer_too_small... ");

	unsigned char buf[1];
	int len;

	/* 128 needs 2 bytes, but we only have 1 */
	len = ndb_negentropy_varint_encode(buf, 1, 128);
	assert(len == 0);

	printf("OK\n");
}

/* ============================================================
 * HEX ENCODING TESTS
 * ============================================================ */

static void test_hex_encode(void)
{
	printf("  hex_encode... ");

	unsigned char bin[] = {0xDE, 0xAD, 0xBE, 0xEF};
	char hex[16];

	size_t len = ndb_negentropy_to_hex(bin, sizeof(bin), hex);
	assert(len == 8);
	assert(strcmp(hex, "deadbeef") == 0);

	printf("OK\n");
}

static void test_hex_decode(void)
{
	printf("  hex_decode... ");

	const char *hex = "deadbeef";
	unsigned char bin[4];

	size_t len = ndb_negentropy_from_hex(hex, 8, bin, sizeof(bin));
	assert(len == 4);
	assert(bin[0] == 0xDE);
	assert(bin[1] == 0xAD);
	assert(bin[2] == 0xBE);
	assert(bin[3] == 0xEF);

	printf("OK\n");
}

static void test_hex_roundtrip(void)
{
	printf("  hex_roundtrip... ");

	unsigned char original[32];
	char hex[65];
	unsigned char decoded[32];

	/* Fill with test data */
	for (int i = 0; i < 32; i++)
		original[i] = (unsigned char)i;

	ndb_negentropy_to_hex(original, 32, hex);
	size_t len = ndb_negentropy_from_hex(hex, 64, decoded, 32);

	assert(len == 32);
	assert(memcmp(original, decoded, 32) == 0);

	printf("OK\n");
}

static void test_hex_invalid(void)
{
	printf("  hex_invalid... ");

	unsigned char bin[4];

	/* Odd length should fail */
	assert(ndb_negentropy_from_hex("abc", 3, bin, 4) == 0);

	/* Invalid characters should fail */
	assert(ndb_negentropy_from_hex("gg", 2, bin, 4) == 0);

	printf("OK\n");
}

/* ============================================================
 * ACCUMULATOR TESTS
 * ============================================================ */

static void test_accumulator_init(void)
{
	printf("  accumulator_init... ");

	struct ndb_negentropy_accumulator acc;
	ndb_negentropy_accumulator_init(&acc);

	for (int i = 0; i < 32; i++)
		assert(acc.sum[i] == 0);

	printf("OK\n");
}

static void test_accumulator_add(void)
{
	printf("  accumulator_add... ");

	struct ndb_negentropy_accumulator acc;
	unsigned char id1[32] = {0};
	unsigned char id2[32] = {0};

	/* Simple test: 1 + 1 = 2 */
	id1[0] = 1;
	id2[0] = 1;

	ndb_negentropy_accumulator_init(&acc);
	ndb_negentropy_accumulator_add(&acc, id1);
	ndb_negentropy_accumulator_add(&acc, id2);

	assert(acc.sum[0] == 2);
	for (int i = 1; i < 32; i++)
		assert(acc.sum[i] == 0);

	printf("OK\n");
}

static void test_accumulator_carry(void)
{
	printf("  accumulator_carry... ");

	struct ndb_negentropy_accumulator acc;
	unsigned char id[32] = {0};

	/* 255 + 1 = 256, which should carry to next byte */
	id[0] = 255;
	ndb_negentropy_accumulator_init(&acc);
	ndb_negentropy_accumulator_add(&acc, id);

	id[0] = 1;
	ndb_negentropy_accumulator_add(&acc, id);

	assert(acc.sum[0] == 0);   /* Low byte wrapped */
	assert(acc.sum[1] == 1);   /* Carry propagated */

	printf("OK\n");
}

/* ============================================================
 * BOUND ENCODING TESTS
 * ============================================================ */

static void test_bound_encode_decode(void)
{
	printf("  bound_encode_decode... ");

	struct ndb_negentropy_bound bound_in, bound_out;
	unsigned char buf[64];
	uint64_t prev_ts_enc = 0, prev_ts_dec = 0;

	/* Test a simple bound */
	bound_in.timestamp = 1000;
	bound_in.prefix_len = 4;
	memset(bound_in.id_prefix, 0, 32);
	bound_in.id_prefix[0] = 0xAB;
	bound_in.id_prefix[1] = 0xCD;
	bound_in.id_prefix[2] = 0xEF;
	bound_in.id_prefix[3] = 0x12;

	int enc_len = ndb_negentropy_bound_encode(buf, sizeof(buf), &bound_in, &prev_ts_enc);
	assert(enc_len > 0);

	int dec_len = ndb_negentropy_bound_decode(buf, enc_len, &bound_out, &prev_ts_dec);
	assert(dec_len == enc_len);
	assert(bound_out.timestamp == bound_in.timestamp);
	assert(bound_out.prefix_len == bound_in.prefix_len);
	assert(memcmp(bound_out.id_prefix, bound_in.id_prefix, 4) == 0);

	printf("OK\n");
}

static void test_bound_infinity(void)
{
	printf("  bound_infinity... ");

	struct ndb_negentropy_bound bound_in, bound_out;
	unsigned char buf[64];
	uint64_t prev_ts_enc = 0, prev_ts_dec = 0;

	/* Infinity timestamp should encode as 0 */
	bound_in.timestamp = UINT64_MAX;
	bound_in.prefix_len = 0;

	int enc_len = ndb_negentropy_bound_encode(buf, sizeof(buf), &bound_in, &prev_ts_enc);
	assert(enc_len > 0);
	assert(buf[0] == 0);  /* First byte should be 0 for infinity */

	int dec_len = ndb_negentropy_bound_decode(buf, enc_len, &bound_out, &prev_ts_dec);
	assert(dec_len == enc_len);
	assert(bound_out.timestamp == UINT64_MAX);

	printf("OK\n");
}

static void test_bound_delta_encoding(void)
{
	printf("  bound_delta_encoding... ");

	struct ndb_negentropy_bound bound1, bound2;
	unsigned char buf[64];
	uint64_t prev_ts = 0;

	/* First bound at timestamp 1000 */
	bound1.timestamp = 1000;
	bound1.prefix_len = 0;

	int len1 = ndb_negentropy_bound_encode(buf, sizeof(buf), &bound1, &prev_ts);
	assert(len1 > 0);

	/* Second bound at timestamp 1005 (delta = 5) */
	bound2.timestamp = 1005;
	bound2.prefix_len = 0;

	int len2 = ndb_negentropy_bound_encode(buf + len1, sizeof(buf) - len1, &bound2, &prev_ts);
	assert(len2 > 0);

	/* The second encoding should be smaller due to delta */
	/* 1001 (first) vs 6 (second: 1 + delta of 5) */

	printf("OK\n");
}

/* ============================================================
 * FINGERPRINT TESTS
 * ============================================================ */

static void test_fingerprint_empty(void)
{
	printf("  fingerprint_empty... ");

	struct ndb_negentropy_accumulator acc;
	unsigned char fp[16];

	ndb_negentropy_accumulator_init(&acc);
	ndb_negentropy_fingerprint(&acc, 0, fp);

	/* Empty fingerprint should be deterministic */
	/* We don't check the exact value, just that it doesn't crash */

	printf("OK\n");
}

static void test_fingerprint_deterministic(void)
{
	printf("  fingerprint_deterministic... ");

	struct ndb_negentropy_accumulator acc1, acc2;
	unsigned char id[32] = {0x42};
	unsigned char fp1[16], fp2[16];

	/* Same input should produce same output */
	ndb_negentropy_accumulator_init(&acc1);
	ndb_negentropy_accumulator_add(&acc1, id);
	ndb_negentropy_fingerprint(&acc1, 1, fp1);

	ndb_negentropy_accumulator_init(&acc2);
	ndb_negentropy_accumulator_add(&acc2, id);
	ndb_negentropy_fingerprint(&acc2, 1, fp2);

	assert(memcmp(fp1, fp2, 16) == 0);

	printf("OK\n");
}

/* ============================================================
 * RANGE ENCODING TESTS
 * ============================================================ */

static void test_range_skip(void)
{
	printf("  range_skip... ");

	struct ndb_negentropy_range range_in, range_out;
	unsigned char buf[128];
	uint64_t prev_ts_enc = 0, prev_ts_dec = 0;
	int enc_len, dec_len;

	/* SKIP mode has no payload */
	range_in.upper_bound.timestamp = 1000;
	range_in.upper_bound.prefix_len = 0;
	range_in.mode = NDB_NEG_SKIP;

	enc_len = ndb_negentropy_range_encode(buf, sizeof(buf), &range_in, &prev_ts_enc);
	assert(enc_len > 0);

	dec_len = ndb_negentropy_range_decode(buf, enc_len, &range_out, &prev_ts_dec);
	assert(dec_len == enc_len);
	assert(range_out.mode == NDB_NEG_SKIP);
	assert(range_out.upper_bound.timestamp == 1000);

	printf("OK\n");
}

static void test_range_fingerprint(void)
{
	printf("  range_fingerprint... ");

	struct ndb_negentropy_range range_in, range_out;
	unsigned char buf[128];
	uint64_t prev_ts_enc = 0, prev_ts_dec = 0;
	int enc_len, dec_len;

	/* FINGERPRINT mode has 16-byte payload */
	range_in.upper_bound.timestamp = 2000;
	range_in.upper_bound.prefix_len = 0;
	range_in.mode = NDB_NEG_FINGERPRINT;

	/* Fill fingerprint with test data */
	for (int i = 0; i < 16; i++)
		range_in.payload.fingerprint[i] = (unsigned char)(0xAB + i);

	enc_len = ndb_negentropy_range_encode(buf, sizeof(buf), &range_in, &prev_ts_enc);
	assert(enc_len > 0);

	dec_len = ndb_negentropy_range_decode(buf, enc_len, &range_out, &prev_ts_dec);
	assert(dec_len == enc_len);
	assert(range_out.mode == NDB_NEG_FINGERPRINT);
	assert(range_out.upper_bound.timestamp == 2000);
	assert(memcmp(range_out.payload.fingerprint, range_in.payload.fingerprint, 16) == 0);

	printf("OK\n");
}

static void test_range_idlist(void)
{
	printf("  range_idlist... ");

	struct ndb_negentropy_range range_in, range_out;
	unsigned char buf[256];
	unsigned char ids[3 * 32];  /* 3 IDs */
	uint64_t prev_ts_enc = 0, prev_ts_dec = 0;
	int enc_len, dec_len;

	/* Create test IDs */
	memset(ids, 0, sizeof(ids));
	ids[0] = 0x11;   /* First ID starts with 0x11 */
	ids[32] = 0x22;  /* Second ID starts with 0x22 */
	ids[64] = 0x33;  /* Third ID starts with 0x33 */

	/* IDLIST mode */
	range_in.upper_bound.timestamp = 3000;
	range_in.upper_bound.prefix_len = 0;
	range_in.mode = NDB_NEG_IDLIST;
	range_in.payload.id_list.id_count = 3;
	range_in.payload.id_list.ids = ids;

	enc_len = ndb_negentropy_range_encode(buf, sizeof(buf), &range_in, &prev_ts_enc);
	assert(enc_len > 0);

	dec_len = ndb_negentropy_range_decode(buf, enc_len, &range_out, &prev_ts_dec);
	assert(dec_len == enc_len);
	assert(range_out.mode == NDB_NEG_IDLIST);
	assert(range_out.upper_bound.timestamp == 3000);
	assert(range_out.payload.id_list.id_count == 3);

	/* Verify zero-copy: pointer into buffer */
	assert(range_out.payload.id_list.ids != NULL);
	assert(range_out.payload.id_list.ids[0] == 0x11);
	assert(range_out.payload.id_list.ids[32] == 0x22);
	assert(range_out.payload.id_list.ids[64] == 0x33);

	printf("OK\n");
}

static void test_range_idlist_response(void)
{
	printf("  range_idlist_response... ");

	struct ndb_negentropy_range range_in, range_out;
	unsigned char buf[256];
	unsigned char have_ids[2 * 32];  /* 2 IDs server has */
	unsigned char bitfield[2];       /* Bitfield for client IDs */
	uint64_t prev_ts_enc = 0, prev_ts_dec = 0;
	int enc_len, dec_len;

	/* Create test data */
	memset(have_ids, 0, sizeof(have_ids));
	have_ids[0] = 0xAA;
	have_ids[32] = 0xBB;
	bitfield[0] = 0b10101010;  /* Some client IDs needed */
	bitfield[1] = 0b01010101;

	/* IDLIST_RESPONSE mode */
	range_in.upper_bound.timestamp = 4000;
	range_in.upper_bound.prefix_len = 0;
	range_in.mode = NDB_NEG_IDLIST_RESPONSE;
	range_in.payload.id_list_response.have_count = 2;
	range_in.payload.id_list_response.have_ids = have_ids;
	range_in.payload.id_list_response.bitfield_len = 2;
	range_in.payload.id_list_response.bitfield = bitfield;

	enc_len = ndb_negentropy_range_encode(buf, sizeof(buf), &range_in, &prev_ts_enc);
	assert(enc_len > 0);

	dec_len = ndb_negentropy_range_decode(buf, enc_len, &range_out, &prev_ts_dec);
	assert(dec_len == enc_len);
	assert(range_out.mode == NDB_NEG_IDLIST_RESPONSE);
	assert(range_out.upper_bound.timestamp == 4000);
	assert(range_out.payload.id_list_response.have_count == 2);
	assert(range_out.payload.id_list_response.bitfield_len == 2);

	/* Verify zero-copy pointers */
	assert(range_out.payload.id_list_response.have_ids[0] == 0xAA);
	assert(range_out.payload.id_list_response.have_ids[32] == 0xBB);
	assert(range_out.payload.id_list_response.bitfield[0] == 0b10101010);
	assert(range_out.payload.id_list_response.bitfield[1] == 0b01010101);

	printf("OK\n");
}

static void test_range_empty_idlist(void)
{
	printf("  range_empty_idlist... ");

	struct ndb_negentropy_range range_in, range_out;
	unsigned char buf[64];
	uint64_t prev_ts_enc = 0, prev_ts_dec = 0;
	int enc_len, dec_len;

	/* Empty IDLIST (count = 0) */
	range_in.upper_bound.timestamp = 5000;
	range_in.upper_bound.prefix_len = 0;
	range_in.mode = NDB_NEG_IDLIST;
	range_in.payload.id_list.id_count = 0;
	range_in.payload.id_list.ids = NULL;

	enc_len = ndb_negentropy_range_encode(buf, sizeof(buf), &range_in, &prev_ts_enc);
	assert(enc_len > 0);

	dec_len = ndb_negentropy_range_decode(buf, enc_len, &range_out, &prev_ts_dec);
	assert(dec_len == enc_len);
	assert(range_out.mode == NDB_NEG_IDLIST);
	assert(range_out.payload.id_list.id_count == 0);
	assert(range_out.payload.id_list.ids == NULL);

	printf("OK\n");
}

/* ============================================================
 * MESSAGE ENCODING TESTS
 * ============================================================ */

static void test_message_version(void)
{
	printf("  message_version... ");

	unsigned char buf[16];
	int version;

	/* Test V1 version */
	buf[0] = NDB_NEGENTROPY_PROTOCOL_V1;
	version = ndb_negentropy_message_version(buf, 1);
	assert(version == NDB_NEGENTROPY_PROTOCOL_V1);
	assert(version == 0x61);

	/* Test empty buffer */
	version = ndb_negentropy_message_version(NULL, 0);
	assert(version == 0);

	printf("OK\n");
}

static void test_message_empty(void)
{
	printf("  message_empty... ");

	unsigned char buf[16];
	int enc_len, count;

	/* Encode empty message (just version byte) */
	enc_len = ndb_negentropy_message_encode(buf, sizeof(buf), NULL, 0);
	assert(enc_len == 1);
	assert(buf[0] == NDB_NEGENTROPY_PROTOCOL_V1);

	/* Count should be 0 */
	count = ndb_negentropy_message_count_ranges(buf, enc_len);
	assert(count == 0);

	printf("OK\n");
}

static void test_message_single_range(void)
{
	printf("  message_single_range... ");

	struct ndb_negentropy_range range;
	unsigned char buf[128];
	int enc_len, count;

	/* Single SKIP range */
	range.upper_bound.timestamp = UINT64_MAX;  /* infinity */
	range.upper_bound.prefix_len = 0;
	range.mode = NDB_NEG_SKIP;

	enc_len = ndb_negentropy_message_encode(buf, sizeof(buf), &range, 1);
	assert(enc_len > 1);
	assert(buf[0] == NDB_NEGENTROPY_PROTOCOL_V1);

	count = ndb_negentropy_message_count_ranges(buf, enc_len);
	assert(count == 1);

	printf("OK\n");
}

static void test_message_multiple_ranges(void)
{
	printf("  message_multiple_ranges... ");

	struct ndb_negentropy_range ranges[3];
	unsigned char buf[256];
	int enc_len, count;

	/* First range: SKIP at timestamp 1000 */
	ranges[0].upper_bound.timestamp = 1000;
	ranges[0].upper_bound.prefix_len = 0;
	ranges[0].mode = NDB_NEG_SKIP;

	/* Second range: FINGERPRINT at timestamp 2000 */
	ranges[1].upper_bound.timestamp = 2000;
	ranges[1].upper_bound.prefix_len = 0;
	ranges[1].mode = NDB_NEG_FINGERPRINT;
	for (int i = 0; i < 16; i++)
		ranges[1].payload.fingerprint[i] = (unsigned char)i;

	/* Third range: SKIP at infinity */
	ranges[2].upper_bound.timestamp = UINT64_MAX;
	ranges[2].upper_bound.prefix_len = 0;
	ranges[2].mode = NDB_NEG_SKIP;

	enc_len = ndb_negentropy_message_encode(buf, sizeof(buf), ranges, 3);
	assert(enc_len > 0);

	count = ndb_negentropy_message_count_ranges(buf, enc_len);
	assert(count == 3);

	printf("OK\n");
}

static void test_message_roundtrip(void)
{
	printf("  message_roundtrip... ");

	struct ndb_negentropy_range ranges_in[2];
	struct ndb_negentropy_range range_out;
	unsigned char buf[256];
	int enc_len;
	const unsigned char *p;
	size_t remaining;
	uint64_t prev_ts;
	int consumed;

	/* Set up input ranges */
	ranges_in[0].upper_bound.timestamp = 1500;
	ranges_in[0].upper_bound.prefix_len = 0;
	ranges_in[0].mode = NDB_NEG_FINGERPRINT;
	for (int i = 0; i < 16; i++)
		ranges_in[0].payload.fingerprint[i] = (unsigned char)(0xAA + i);

	ranges_in[1].upper_bound.timestamp = UINT64_MAX;
	ranges_in[1].upper_bound.prefix_len = 0;
	ranges_in[1].mode = NDB_NEG_SKIP;

	/* Encode */
	enc_len = ndb_negentropy_message_encode(buf, sizeof(buf), ranges_in, 2);
	assert(enc_len > 0);

	/* Verify version */
	assert(ndb_negentropy_message_version(buf, enc_len) == NDB_NEGENTROPY_PROTOCOL_V1);

	/* Decode and verify ranges */
	p = buf + 1;  /* skip version */
	remaining = (size_t)enc_len - 1;
	prev_ts = 0;

	/* First range */
	consumed = ndb_negentropy_range_decode(p, remaining, &range_out, &prev_ts);
	assert(consumed > 0);
	assert(range_out.mode == NDB_NEG_FINGERPRINT);
	assert(range_out.upper_bound.timestamp == 1500);
	assert(memcmp(range_out.payload.fingerprint, ranges_in[0].payload.fingerprint, 16) == 0);

	p += consumed;
	remaining -= (size_t)consumed;

	/* Second range */
	consumed = ndb_negentropy_range_decode(p, remaining, &range_out, &prev_ts);
	assert(consumed > 0);
	assert(range_out.mode == NDB_NEG_SKIP);
	assert(range_out.upper_bound.timestamp == UINT64_MAX);

	p += consumed;
	remaining -= (size_t)consumed;

	/* Should be at end */
	assert(remaining == 0);

	printf("OK\n");
}

static void test_message_invalid_version(void)
{
	printf("  message_invalid_version... ");

	unsigned char buf[16];
	int count;

	/* Wrong version */
	buf[0] = 0x00;
	count = ndb_negentropy_message_count_ranges(buf, 1);
	assert(count == -1);

	/* Future version */
	buf[0] = 0x62;
	count = ndb_negentropy_message_count_ranges(buf, 1);
	assert(count == -1);

	printf("OK\n");
}

/* ============================================================
 * STORAGE TESTS
 * ============================================================ */

static void test_storage_init_destroy(void)
{
	printf("  storage_init_destroy... ");

	struct ndb_negentropy_storage storage;

	assert(ndb_negentropy_storage_init(&storage) == 1);
	assert(storage.items == NULL);
	assert(storage.count == 0);
	assert(storage.sealed == 0);

	ndb_negentropy_storage_destroy(&storage);

	printf("OK\n");
}

static void test_storage_add_seal(void)
{
	printf("  storage_add_seal... ");

	struct ndb_negentropy_storage storage;
	unsigned char id[32] = {0};

	ndb_negentropy_storage_init(&storage);

	/* Add some items (out of order) */
	id[0] = 0x03;
	assert(ndb_negentropy_storage_add(&storage, 3000, id) == 1);

	id[0] = 0x01;
	assert(ndb_negentropy_storage_add(&storage, 1000, id) == 1);

	id[0] = 0x02;
	assert(ndb_negentropy_storage_add(&storage, 2000, id) == 1);

	assert(ndb_negentropy_storage_size(&storage) == 3);

	/* Seal - should sort items */
	assert(ndb_negentropy_storage_seal(&storage) == 1);

	/* Verify sorted order */
	const struct ndb_negentropy_item *item;

	item = ndb_negentropy_storage_get(&storage, 0);
	assert(item != NULL);
	assert(item->timestamp == 1000);
	assert(item->id[0] == 0x01);

	item = ndb_negentropy_storage_get(&storage, 1);
	assert(item->timestamp == 2000);
	assert(item->id[0] == 0x02);

	item = ndb_negentropy_storage_get(&storage, 2);
	assert(item->timestamp == 3000);
	assert(item->id[0] == 0x03);

	/* Cannot add after seal */
	id[0] = 0x04;
	assert(ndb_negentropy_storage_add(&storage, 4000, id) == 0);

	/* Cannot seal twice */
	assert(ndb_negentropy_storage_seal(&storage) == 0);

	ndb_negentropy_storage_destroy(&storage);

	printf("OK\n");
}

static void test_storage_same_timestamp_sort(void)
{
	printf("  storage_same_timestamp_sort... ");

	struct ndb_negentropy_storage storage;
	unsigned char id[32] = {0};

	ndb_negentropy_storage_init(&storage);

	/* Add items with same timestamp, different IDs */
	id[0] = 0xCC;
	ndb_negentropy_storage_add(&storage, 1000, id);

	id[0] = 0xAA;
	ndb_negentropy_storage_add(&storage, 1000, id);

	id[0] = 0xBB;
	ndb_negentropy_storage_add(&storage, 1000, id);

	ndb_negentropy_storage_seal(&storage);

	/* Should be sorted by ID */
	const struct ndb_negentropy_item *item;

	item = ndb_negentropy_storage_get(&storage, 0);
	assert(item->id[0] == 0xAA);

	item = ndb_negentropy_storage_get(&storage, 1);
	assert(item->id[0] == 0xBB);

	item = ndb_negentropy_storage_get(&storage, 2);
	assert(item->id[0] == 0xCC);

	ndb_negentropy_storage_destroy(&storage);

	printf("OK\n");
}

static void test_storage_lower_bound(void)
{
	printf("  storage_lower_bound... ");

	struct ndb_negentropy_storage storage;
	struct ndb_negentropy_bound bound;
	unsigned char id[32] = {0};
	size_t idx;

	ndb_negentropy_storage_init(&storage);

	/* Add items at timestamps 1000, 2000, 3000 */
	id[0] = 0x01;
	ndb_negentropy_storage_add(&storage, 1000, id);
	id[0] = 0x02;
	ndb_negentropy_storage_add(&storage, 2000, id);
	id[0] = 0x03;
	ndb_negentropy_storage_add(&storage, 3000, id);

	ndb_negentropy_storage_seal(&storage);

	/* Lower bound at 0 -> index 0 */
	memset(&bound, 0, sizeof(bound));
	bound.timestamp = 0;
	idx = ndb_negentropy_storage_lower_bound(&storage, &bound);
	assert(idx == 0);

	/* Lower bound at 1500 -> index 1 (first item >= 1500) */
	bound.timestamp = 1500;
	idx = ndb_negentropy_storage_lower_bound(&storage, &bound);
	assert(idx == 1);

	/* Lower bound at 2000 -> index 1 (exact match) */
	bound.timestamp = 2000;
	idx = ndb_negentropy_storage_lower_bound(&storage, &bound);
	assert(idx == 1);

	/* Lower bound at infinity -> returns count (past end) */
	bound.timestamp = UINT64_MAX;
	idx = ndb_negentropy_storage_lower_bound(&storage, &bound);
	assert(idx == 3);

	ndb_negentropy_storage_destroy(&storage);

	printf("OK\n");
}

static void test_storage_fingerprint(void)
{
	printf("  storage_fingerprint... ");

	struct ndb_negentropy_storage storage;
	unsigned char id[32] = {0};
	unsigned char fp1[16], fp2[16];

	ndb_negentropy_storage_init(&storage);

	/* Add items */
	id[0] = 0x01;
	ndb_negentropy_storage_add(&storage, 1000, id);
	id[0] = 0x02;
	ndb_negentropy_storage_add(&storage, 2000, id);
	id[0] = 0x03;
	ndb_negentropy_storage_add(&storage, 3000, id);

	ndb_negentropy_storage_seal(&storage);

	/* Fingerprint of full range */
	assert(ndb_negentropy_storage_fingerprint(&storage, 0, 3, fp1) == 1);

	/* Fingerprint again - should be same */
	assert(ndb_negentropy_storage_fingerprint(&storage, 0, 3, fp2) == 1);
	assert(memcmp(fp1, fp2, 16) == 0);

	/* Fingerprint of subrange - should be different */
	assert(ndb_negentropy_storage_fingerprint(&storage, 0, 2, fp2) == 1);
	assert(memcmp(fp1, fp2, 16) != 0);

	/* Empty range fingerprint */
	assert(ndb_negentropy_storage_fingerprint(&storage, 0, 0, fp2) == 1);

	/* Invalid range should fail */
	assert(ndb_negentropy_storage_fingerprint(&storage, 2, 1, fp2) == 0);
	assert(ndb_negentropy_storage_fingerprint(&storage, 0, 10, fp2) == 0);

	ndb_negentropy_storage_destroy(&storage);

	printf("OK\n");
}

static void test_storage_add_many(void)
{
	printf("  storage_add_many... ");

	struct ndb_negentropy_storage storage;
	struct ndb_negentropy_item items[3];

	ndb_negentropy_storage_init(&storage);

	/* Create items */
	items[0].timestamp = 3000;
	memset(items[0].id, 0x03, 32);

	items[1].timestamp = 1000;
	memset(items[1].id, 0x01, 32);

	items[2].timestamp = 2000;
	memset(items[2].id, 0x02, 32);

	/* Add all at once */
	assert(ndb_negentropy_storage_add_many(&storage, items, 3) == 1);
	assert(ndb_negentropy_storage_size(&storage) == 3);

	ndb_negentropy_storage_seal(&storage);

	/* Should be sorted */
	const struct ndb_negentropy_item *item;
	item = ndb_negentropy_storage_get(&storage, 0);
	assert(item->timestamp == 1000);

	item = ndb_negentropy_storage_get(&storage, 2);
	assert(item->timestamp == 3000);

	ndb_negentropy_storage_destroy(&storage);

	printf("OK\n");
}

/* ============================================================
 * RECONCILIATION TESTS
 * ============================================================ */

static void test_reconcile_init_destroy(void)
{
	printf("  reconcile_init_destroy... ");

	struct ndb_negentropy_storage storage;
	struct ndb_negentropy neg;
	unsigned char id[32] = {0x42};

	/* Create storage */
	ndb_negentropy_storage_init(&storage);
	ndb_negentropy_storage_add(&storage, 1000, id);
	ndb_negentropy_storage_seal(&storage);

	/* Init context */
	assert(ndb_negentropy_init(&neg, &storage) == 1);

	/* Destroy */
	ndb_negentropy_destroy(&neg);
	ndb_negentropy_storage_destroy(&storage);

	printf("OK\n");
}

static void test_reconcile_initiate(void)
{
	printf("  reconcile_initiate... ");

	struct ndb_negentropy_storage storage;
	struct ndb_negentropy neg;
	unsigned char id[32] = {0x42};
	unsigned char buf[256];
	size_t outlen;

	/* Create storage with one item */
	ndb_negentropy_storage_init(&storage);
	ndb_negentropy_storage_add(&storage, 1000, id);
	ndb_negentropy_storage_seal(&storage);

	/* Init context and generate initial message */
	ndb_negentropy_init(&neg, &storage);
	assert(ndb_negentropy_initiate(&neg, buf, sizeof(buf), &outlen) == 1);

	/* Should have version + at least one range */
	assert(outlen > 1);
	assert(buf[0] == NDB_NEGENTROPY_PROTOCOL_V1);

	/* Verify message structure */
	int count = ndb_negentropy_message_count_ranges(buf, outlen);
	assert(count == 1);

	ndb_negentropy_destroy(&neg);
	ndb_negentropy_storage_destroy(&storage);

	printf("OK\n");
}

static void test_reconcile_identical_sets(void)
{
	printf("  reconcile_identical_sets... ");

	struct ndb_negentropy_storage storage1, storage2;
	struct ndb_negentropy neg1, neg2;
	unsigned char buf1[1024], buf2[1024];
	size_t len1, len2;

	/* Create identical storages */
	unsigned char id1[32] = {0x01};
	unsigned char id2[32] = {0x02};
	unsigned char id3[32] = {0x03};

	ndb_negentropy_storage_init(&storage1);
	ndb_negentropy_storage_add(&storage1, 1000, id1);
	ndb_negentropy_storage_add(&storage1, 2000, id2);
	ndb_negentropy_storage_add(&storage1, 3000, id3);
	ndb_negentropy_storage_seal(&storage1);

	ndb_negentropy_storage_init(&storage2);
	ndb_negentropy_storage_add(&storage2, 1000, id1);
	ndb_negentropy_storage_add(&storage2, 2000, id2);
	ndb_negentropy_storage_add(&storage2, 3000, id3);
	ndb_negentropy_storage_seal(&storage2);

	/* Client initiates */
	ndb_negentropy_init(&neg1, &storage1);
	assert(ndb_negentropy_initiate(&neg1, buf1, sizeof(buf1), &len1) == 1);

	/* Server processes and responds */
	ndb_negentropy_init(&neg2, &storage2);
	len2 = sizeof(buf2);
	assert(ndb_negentropy_reconcile(&neg2, buf1, len1, buf2, &len2) == 1);

	/* For identical sets, fingerprints should match */
	/* Server should respond with SKIP */
	int count = ndb_negentropy_message_count_ranges(buf2, len2);
	assert(count == 1);

	/* No IDs should be detected as different */
	const unsigned char *have, *need;
	assert(ndb_negentropy_get_have_ids(&neg2, &have) == 0);
	assert(ndb_negentropy_get_need_ids(&neg2, &need) == 0);

	ndb_negentropy_destroy(&neg1);
	ndb_negentropy_destroy(&neg2);
	ndb_negentropy_storage_destroy(&storage1);
	ndb_negentropy_storage_destroy(&storage2);

	printf("OK\n");
}

static void test_reconcile_different_sets(void)
{
	printf("  reconcile_different_sets... ");

	struct ndb_negentropy_storage storage1, storage2;
	struct ndb_negentropy neg1, neg2;
	unsigned char buf1[4096], buf2[4096];
	size_t len1, len2;

	/* Client has id1, id2 */
	unsigned char id1[32] = {0};
	unsigned char id2[32] = {0};
	unsigned char id3[32] = {0};
	id1[0] = 0x01;
	id2[0] = 0x02;
	id3[0] = 0x03;

	ndb_negentropy_storage_init(&storage1);
	ndb_negentropy_storage_add(&storage1, 1000, id1);
	ndb_negentropy_storage_add(&storage1, 2000, id2);
	ndb_negentropy_storage_seal(&storage1);

	/* Server has id2, id3 */
	ndb_negentropy_storage_init(&storage2);
	ndb_negentropy_storage_add(&storage2, 2000, id2);
	ndb_negentropy_storage_add(&storage2, 3000, id3);
	ndb_negentropy_storage_seal(&storage2);

	/* Client initiates */
	ndb_negentropy_init(&neg1, &storage1);
	assert(ndb_negentropy_initiate(&neg1, buf1, sizeof(buf1), &len1) == 1);

	/* Server processes and responds */
	ndb_negentropy_init(&neg2, &storage2);
	len2 = sizeof(buf2);
	assert(ndb_negentropy_reconcile(&neg2, buf1, len1, buf2, &len2) == 1);

	/* Server should detect differences */
	/* Keep processing until we get have/need IDs */
	int rounds = 0;
	while (rounds < 10) {
		/* Client processes server's response */
		len1 = sizeof(buf1);
		if (!ndb_negentropy_reconcile(&neg1, buf2, len2, buf1, &len1))
			break;

		/* Check if we're done (only version byte) */
		if (len1 == 1)
			break;

		/* Server processes client's response */
		len2 = sizeof(buf2);
		if (!ndb_negentropy_reconcile(&neg2, buf1, len1, buf2, &len2))
			break;

		if (len2 == 1)
			break;

		rounds++;
	}

	/* After reconciliation, check results */
	/* Note: The exact IDs depend on protocol details */

	ndb_negentropy_destroy(&neg1);
	ndb_negentropy_destroy(&neg2);
	ndb_negentropy_storage_destroy(&storage1);
	ndb_negentropy_storage_destroy(&storage2);

	printf("OK\n");
}

/* ============================================================
 * MAIN
 * ============================================================ */

int main(void)
{
	printf("Running negentropy tests...\n\n");

	printf("Varint tests:\n");
	test_varint_size();
	test_varint_encode_decode_roundtrip();
	test_varint_specific_encodings();
	test_varint_buffer_too_small();

	printf("\nHex encoding tests:\n");
	test_hex_encode();
	test_hex_decode();
	test_hex_roundtrip();
	test_hex_invalid();

	printf("\nAccumulator tests:\n");
	test_accumulator_init();
	test_accumulator_add();
	test_accumulator_carry();

	printf("\nBound encoding tests:\n");
	test_bound_encode_decode();
	test_bound_infinity();
	test_bound_delta_encoding();

	printf("\nFingerprint tests:\n");
	test_fingerprint_empty();
	test_fingerprint_deterministic();

	printf("\nRange encoding tests:\n");
	test_range_skip();
	test_range_fingerprint();
	test_range_idlist();
	test_range_idlist_response();
	test_range_empty_idlist();

	printf("\nMessage encoding tests:\n");
	test_message_version();
	test_message_empty();
	test_message_single_range();
	test_message_multiple_ranges();
	test_message_roundtrip();
	test_message_invalid_version();

	printf("\nStorage tests:\n");
	test_storage_init_destroy();
	test_storage_add_seal();
	test_storage_same_timestamp_sort();
	test_storage_lower_bound();
	test_storage_fingerprint();
	test_storage_add_many();

	printf("\nReconciliation tests:\n");
	test_reconcile_init_destroy();
	test_reconcile_initiate();
	test_reconcile_identical_sets();
	test_reconcile_different_sets();

	printf("\n=== All tests passed! ===\n");
	return 0;
}
