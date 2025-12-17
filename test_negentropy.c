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

	printf("\n=== All tests passed! ===\n");
	return 0;
}
