
#include "nostrdb.h"
#include "hex.h"
#include "io.h"
#include "bolt11/bolt11.h"
#include "invoice.h"
#include "block.h"
#include "protected_queue.h"
#include "memchr.h"
#include "print_util.h"
#include "bindings/c/profile_reader.h"
#include "bindings/c/profile_verifier.h"
#include "bindings/c/meta_reader.h"
#include "bindings/c/meta_verifier.h"
#include "secp256k1.h"

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

int ndb_print_kind_keys(struct ndb_txn *txn);
#define TEST_DIR "./testdata/db"
static const char *test_dir = TEST_DIR;

static void delete_test_db() {
    // Delete ./testdata/db/data.mdb
    unlink(TEST_DIR "/data.mdb");
    unlink(TEST_DIR "/data.lock");
}

int ndb_rebuild_reaction_metadata(struct ndb_txn *txn, const unsigned char *note_id, struct ndb_note_meta_builder *builder, uint32_t *count);
int ndb_count_replies(struct ndb_txn *txn, const unsigned char *note_id, uint16_t *direct_replies, uint32_t *thread_replies);

static void db_load_events(struct ndb *ndb, const char *filename)
{
	size_t filesize;
	int written;
	char *json;
	struct stat st;

	stat(filename, &st);
	filesize = st.st_size;

	json = malloc(filesize + 1); // +1 for '\0' if you need it null-terminated
	read_file(filename, (unsigned char*)json, filesize, &written);
	assert(ndb_process_client_events(ndb, json, written));
	free(json);
}

static NdbProfile_table_t lookup_profile(struct ndb_txn *txn, uint64_t pk)
{
	void *root;
	size_t len;
	assert((root = ndb_get_profile_by_key(txn, pk, &len)));
	assert(root);

	NdbProfileRecord_table_t profile_record = NdbProfileRecord_as_root(root);
	NdbProfile_table_t profile = NdbProfileRecord_profile_get(profile_record);
	return profile;
}

static void print_search(struct ndb_txn *txn, struct ndb_search *search)
{
	NdbProfile_table_t profile = lookup_profile(txn, search->profile_key);
	const char *name = NdbProfile_name_get(profile);
	const char *display_name = NdbProfile_display_name_get(profile);
	printf("searched_name name:'%s' display_name:'%s' pk:%" PRIu64 " ts:%" PRIu64 " id:", name, display_name, search->profile_key, search->key->timestamp);
	print_hex(search->key->id, 32);
	printf("\n");
}

static void test_count_metadata()
{
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_txn txn;
	struct ndb_note_meta *meta;
	//struct ndb_note_meta_entry *counts;
	struct ndb_note_meta_entry *entry;
	uint16_t count, direct_replies[2];
	uint32_t total_reactions, reactions, thread_replies[2];
	int i;

	reactions = 0;
	delete_test_db();

	ndb_default_config(&config);
	assert(ndb_init(&ndb, test_dir, &config));

	const unsigned char id[] = {
		0xd4, 0x4a, 0xd9, 0x6c, 0xb8, 0x92, 0x40, 0x92, 0xa7, 0x6b, 0xc2, 0xaf,
		0xdd, 0xeb, 0x12, 0xeb, 0x85, 0x23, 0x3c, 0x0d, 0x03, 0xa7, 0xd9, 0xad,
		0xc4, 0x2c, 0x2a, 0x85, 0xa7, 0x9a, 0x43, 0x05
	};

	db_load_events(ndb, "testdata/test_counts.json");

	/* consume all events to ensure we're done processing */
	ndb_destroy(ndb);
	ndb_init(&ndb, test_dir, &config);

	ndb_begin_query(ndb, &txn);
	meta = ndb_get_note_meta(&txn, id);
	assert(meta);

	count = ndb_note_meta_entries_count(meta);
	entry = ndb_note_meta_entries(meta);
	for (i = 0; i < count; i++) {
		entry = ndb_note_meta_entry_at(meta, i);
		if (*ndb_note_meta_entry_type(entry) == NDB_NOTE_META_REACTION) {
			reactions += *ndb_note_meta_reaction_count(entry);
		}
	}

	entry = ndb_note_meta_find_entry(meta, NDB_NOTE_META_COUNTS, NULL);

	assert(entry);
	assert(*ndb_note_meta_counts_quotes(entry) == 2);

	thread_replies[0] = *ndb_note_meta_counts_thread_replies(entry);
	printf("\t# thread replies %d\n", thread_replies[0]);
	assert(thread_replies[0] == 93);

	direct_replies[0] = *ndb_note_meta_counts_direct_replies(entry);
	printf("\t# direct replies %d\n", direct_replies[0]);
	assert(direct_replies[0] == 83);

	total_reactions = *ndb_note_meta_counts_total_reactions(entry);
	printf("\t# total reactions %d\n", reactions);
	assert(total_reactions > 0);

	printf("\t# reactions %d\n", reactions);
	assert(reactions > 0);
	assert(total_reactions == reactions);


	ndb_end_query(&txn);

	ndb_begin_query(ndb, &txn);
	/* this is used in the migration code,
	 * let's make sure it matches the online logic */
	ndb_rebuild_reaction_metadata(&txn, id, NULL, &reactions);
	printf("\t# after-counted reactions %d\n", reactions);
	assert(reactions == total_reactions);

	ndb_count_replies(&txn, id, &direct_replies[1], &thread_replies[1]);
	printf("\t# after-counted replies direct:%d thread:%d\n", direct_replies[1], thread_replies[1]);
	assert(direct_replies[0] == direct_replies[1]);
	assert(thread_replies[0] == thread_replies[1]);

	ndb_end_query(&txn);

	ndb_destroy(ndb);
	delete_test_db();

	printf("ok test_count_metadata\n");
}

static void test_nip44_test_vector()
{
	/*
	{
	  "sec1": "0000000000000000000000000000000000000000000000000000000000000001",
	  "sec2": "0000000000000000000000000000000000000000000000000000000000000002",
	  "conversation_key": "c41c775356fd92eadc63ff5a0dc1da211b268cbea22316767095b2871ea1412d",
	  "nonce": "0000000000000000000000000000000000000000000000000000000000000001",
	  "plaintext": "a",
	  "payload": "AgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABee0G5VSK0/9YypIObAtDKfYEAjD35uVkHyB0F4DwrcNaCXlCWZKaArsGrY6M9wnuTMxWfp1RTN9Xga8no+kF5Vsb"
	}
	*/

	/*
	static const unsigned char recv_sec[32] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x01
	};

	static const unsigned char sender_pub[32] = {
		0xc6, 0x04, 0x7f, 0x94, 0x41, 0xed, 0x7d, 0x6d, 0x30, 0x45,
		0x40, 0x6e, 0x95, 0xc0, 0x7c, 0xd8, 0x5c, 0x77, 0x8e, 0x4b,
		0x8c, 0xef, 0x3c, 0xa7, 0xab, 0xac, 0x09, 0xb9, 0x5c, 0x70,
		0x9e, 0xe5
	};
	*/
	static const unsigned char recv_sec[32] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x02
	};

	static const unsigned char sender_pub[32] = {
		0x79, 0xbe, 0x66, 0x7e, 0xf9, 0xdc, 0xbb, 0xac, 0x55, 0xa0,
		0x62, 0x95, 0xce, 0x87, 0x0b, 0x07, 0x02, 0x9b, 0xfc, 0xdb,
		0x2d, 0xce, 0x28, 0xd9, 0x59, 0xf2, 0x81, 0x5b, 0x16, 0xf8,
		0x17, 0x98
	};

	static const char payload[] = "AgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABee0G5VSK0/9YypIObAtDKfYEAjD35uVkHyB0F4DwrcNaCXlCWZKaArsGrY6M9wnuTMxWfp1RTN9Xga8no+kF5Vsb";

	static const char *plaintext = "a";

	unsigned char buffer[1024];
	unsigned char *decrypted;
	uint16_t decrypted_len;
	enum ndb_decrypt_result res;
	secp256k1_context *context;
	context = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

	res = nip44_decrypt(context,
			    sender_pub, recv_sec,
			    payload, sizeof(payload)-1,
			    buffer, sizeof(buffer),
			    &decrypted, &decrypted_len);

	if (res != NIP44_OK) {
		fprintf(stderr, "nip44 error: %s\n", nip44_err_msg(res));
	}
	//printf("# decrypted(%d) '%.*s'\n", decrypted_len, decrypted_len, (char*)decrypted);
	assert(res == NIP44_OK);
	assert(memcmp(decrypted, plaintext, 1) == 0);

	secp256k1_context_destroy(context);
	printf("ok test_nip44_test_vector\n");
}

static void test_nip44_decrypt()
{
	static const unsigned char recv_sec[32] = {
		0x81, 0xa5, 0x33, 0x9b, 0xf9, 0xfa, 0xf8, 0x91, 0x5c, 0x48,
		0xf3, 0xdd, 0xca, 0xd4, 0xd3, 0xa5, 0x54, 0x06, 0xc7, 0x7b,
		0x27, 0x81, 0xc8, 0xc9, 0x94, 0x5d, 0xfd, 0xad, 0xe6, 0xae,
		0x11, 0x89
	};

	/* 81a5339bf9faf8915c48f3ddcad4d3a55406c77b2781c8c9945dfdade6ae1189 */

	/*
	static const unsigned char recv_pub[32] = {
		0xf1, 0xec, 0xfe, 0xb2, 0xed, 0xa7, 0xe7, 0x7c, 0xd7, 0x2e,
		0x63, 0x3c, 0x54, 0x12, 0xae, 0x99, 0x07, 0xb5, 0x0a, 0xd1,
		0xaf, 0xd8, 0x16, 0xab, 0x5a, 0x61, 0xac, 0x6f, 0x2c, 0x3c,
		0x6c, 0xb3
	};
	*/

	static const unsigned char sender_pub[32] = {
		  0x32, 0xe1, 0x82, 0x76, 0x35, 0x45, 0x0e, 0xbb, 0x3c, 0x5a,
		  0x7d, 0x12, 0xc1, 0xf8, 0xe7, 0xb2, 0xb5, 0x14, 0x43, 0x9a,
		  0xc1, 0x0a, 0x67, 0xee, 0xf3, 0xd9, 0xfd, 0x9c, 0x5c, 0x68,
		  0xe2, 0x45
	};

	static const char encrypted[] = "Ake9vGvum3cJJrF62qHwVDb3HC3UEP2t0G1GLByJlXi7OP4awMDomJej0aVkiZUHYPJvqgsbL30BZwBCfkd8F1596jJcOFC5KUXsCb45mHS+EqA5rs13/37HlImp76PO/1ns";
	//static const char encrypted[] = "AknLKK+v4mZyMpShz5+/iyADJk4SUEQwGV7BUWQRmCHDD6KBO7VStk16StpF4TFTGrEV1BmkZVSlNYVu4cl9C0egBXL0X9AzJBM0I3wotSUQEZwGXwnts0HdhFAQepq4kmrc";

	static const char *expected = "hello, test.c";

	unsigned char buffer[1024];
	unsigned char *decrypted;
	uint16_t decrypted_len;
	enum ndb_decrypt_result res;
	secp256k1_context *context;

	context = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

	res = nip44_decrypt(context,
			    sender_pub, recv_sec,
			    encrypted, sizeof(encrypted)-1,
			    buffer, sizeof(buffer),
			    &decrypted, &decrypted_len);

	if (res != NIP44_OK) {
		fprintf(stderr, "nip44 error: %s\n", nip44_err_msg(res));
	}
	assert(res == NIP44_OK);
	assert(strcmp((char *)decrypted, expected) == 0);

	printf("ok test_nip44_decrypt\n");
	secp256k1_context_destroy(context);
}

static void test_nip44_round_trip()
{
	static const unsigned char send_sec[32] = {
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
	};
	static const unsigned char recv_sec[32] = {
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2
	};
	static const unsigned char send_pub[32] = {
		0x79, 0xbe, 0x66, 0x7e, 0xf9, 0xdc, 0xbb, 0xac, 0x55, 0xa0,
		0x62, 0x95, 0xce, 0x87, 0x0b, 0x07, 0x02, 0x9b, 0xfc, 0xdb,
		0x2d, 0xce, 0x28, 0xd9, 0x59, 0xf2, 0x81, 0x5b, 0x16, 0xf8,
		0x17, 0x98
	};
	static const unsigned char recv_pub[32] = {
		0xc6, 0x04, 0x7f, 0x94, 0x41, 0xed, 0x7d, 0x6d, 0x30, 0x45,
		0x40, 0x6e, 0x95, 0xc0, 0x7c, 0xd8, 0x5c, 0x77, 0x8e, 0x4b,
		0x8c, 0xef, 0x3c, 0xa7, 0xab, 0xac, 0x09, 0xb9, 0x5c, 0x70,
		0x9e, 0xe5
	};
	static const char plaintext[] = "hello, world";

	unsigned char buf[1024];
	unsigned char buf2[1024];
	char *out, *plaintext_out;
	ssize_t out_len;
	int ok;
	uint16_t plaintext_len;
	secp256k1_context *context;

	context = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

	ok = nip44_encrypt(context, send_sec, recv_pub,
			   (const unsigned char *)plaintext, sizeof(plaintext)-1,
			   buf, sizeof(buf), &out, &out_len);

	if (ok != NIP44_OK)
		printf("nip44 encrypt err: %s\n", nip44_err_msg(ok));
	assert(ok == NIP44_OK);

	ok = nip44_decrypt(context, send_pub, recv_sec,
			   out, out_len,
			   buf2, sizeof(buf2),
			   (unsigned char**)&plaintext_out, &plaintext_len);

	if (ok != NIP44_OK)
		printf("nip44 decrypt err: %s\n", nip44_err_msg(ok));

	assert(ok == NIP44_OK);
	assert(plaintext_len == sizeof(plaintext)-1);
	assert(!strcmp(plaintext, plaintext_out));

	secp256k1_context_destroy(context);
}

static void kind_filter(struct ndb_filter *f, uint64_t kind)
{
	ndb_filter_init(f);
	ndb_filter_start_field(f, NDB_FILTER_KINDS);
	ndb_filter_add_int_element(f, kind);
	ndb_filter_end_field(f);
	ndb_filter_end(f);
}

static void test_giftwrap_reprocess()
{
	struct ndb *ndb;
	struct ndb_filter filter;
	struct ndb_config config;
	struct ndb_txn txn;
	struct ndb_note *rumor, *giftwrap;
	int ok;
	uint64_t subid;
	ndb_default_config(&config);
	const char *giftwrap_json;
	uint64_t note_ids[2];
	char buf[4096];

	static unsigned char recv_sec[32] = {
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2
	};

	static const unsigned char recv_pub[32] = {
		0xc6, 0x04, 0x7f, 0x94, 0x41, 0xed, 0x7d, 0x6d, 0x30, 0x45,
		0x40, 0x6e, 0x95, 0xc0, 0x7c, 0xd8, 0x5c, 0x77, 0x8e, 0x4b,
		0x8c, 0xef, 0x3c, 0xa7, 0xab, 0xac, 0x09, 0xb9, 0x5c, 0x70,
		0x9e, 0xe5
	};

	static const unsigned char giftwrap_id[32] = {
		0x94, 0x1e, 0xaf, 0x4a, 0x9b, 0xd0, 0x09, 0x0a, 0xb5, 0xd4,
		0x14, 0xff, 0x5a, 0x68, 0x25, 0x4d, 0xa5, 0x78, 0x6f, 0x0b,
		0xf8, 0xb8, 0xe1, 0x56, 0xbc, 0x37, 0xa7, 0x7e, 0xa2, 0x68,
		0x0a, 0x92
	};

	delete_test_db();
	assert(ndb_init(&ndb, test_dir, &config));

	//ndb_add_key(ndb, one);
	//ndb_add_key(ndb, recv_sec);

	kind_filter(&filter, 1059);

	subid = ndb_subscribe(ndb, &filter, 1);

	giftwrap_json = "{\"id\":\"941eaf4a9bd0090ab5d414ff5a68254da5786f0bf8b8e156bc37a77ea2680a92\",\"pubkey\":\"5fac96633ffd0f68a037778dc40d7747ddf0ceaadb7986c23c0597a56b9ab6f6\",\"created_at\":1764513655,\"kind\":1059,\"tags\":[[\"p\",\"c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5\"]],\"content\":\"AmoRXHPZ6cYI2YR1PL1J2BpxY8onrJHUvt3cOhAe1IOIk6wnighedqyUqQsJ/Bnq0qAAkldMipUuHmr1TRNeu/3Rf3AZ1+4N6/eSJb2GcVaaWkqJ6DIIW+YHvQZRuS/1ef3lo41+2zOFqJMvLiz/Pw1kLdDdi5pKWy0QHJIKFPuIE9XNhFMTMo+rYSIlpfiAXtzpM6qETFBJxabmaZOMlgnxQG6Mm2gB3Y6rmj9gLgxsPCaOnvmfi5xhA+jRvz8FYQ+WIU1ij7SFsssz27fISRRIMEOoq7CHPcsdY2Ly/PSp1t+aLpSz1e8Chnhf3lq/Y0mumLDS/BzNaFm38wQqCoWbxrn+7iLTMfINXoDm7gY6Qb4eZc/Am/wjRJsHm4F37bPiCAupzYWOPdJWO3a4TPmGaHk6MmyrHRu4V20iZK+svdJ257RuwMrYk39xCWNG985t1acPc5Of+ZKDnUcL5jVw0ZUOTrV10uq/UVoOHwDkz0mcsJ0Adhc0fKgiE7CWZgKbaO0PIaAL0I+2hs350dcEK3Go8vzMfiN1Uc7NUUMpjQzIRJC1J6CVf6HkvvQ+JD52RhXsVZN8TTkrpxEDqvCm8eSpamqIZuMbrTh5jJ/J+S83dI8rHvyAcmGrRes5wMOiVaFPglreO/H//AgvcR+N/zYOrV4qCWboU+oTBz7A/ZzMGGt6mhL4dmDKDW3p/HT0l5dEfAn71fdx/VH+jXquVr/9rCw2F4kLzXiNB6E3OBxp7bjkgnmAWCXeNL8NRcOT2l6LaF1l+xi3+NJGxP6tdry+OYz/C12jAZ223HWtQv62fLS3wAuwJH3QjrYromeWU+MNyTdMEFRyuxMq9mpYno7tYF6FpVAR7w9oIZME7F36+1I2b7MERXsgioEBGJFWotC9v5nEKWLJJajZFPlsFQf96Y6Cwovd4moVNmDJ8s2riRDx9D9NxDJxNkz0l+JSLTloTI9p7uMPC6LJtP6qgNAdNtasrUnPo8z5h7kYJR0U8ClsZbOZcf13cSdZck9jZp0kqiSBREhVHGLQmqirXm7UEnoTZYn8U74l3wsetgGiQ3AOAQ94REbzt1obHtGj4JG9Fd3KydWNMcOv1hLODw==\",\"sig\":\"ee99080081a408aa2ce0e2372a44aa0eb1e8e60aa7b4330909d7c7a701d84076a7815412c83a7aa1b783ed0942b0f5e2e2f63efeee8aa0c69503971c65370c3d\"}";

	ndb_process_event(ndb, giftwrap_json, strlen(giftwrap_json));

	ok = ndb_wait_for_notes(ndb, subid, note_ids,
				sizeof(note_ids)/sizeof(note_ids[0]));
	assert(ok == 1);
	assert(ndb_unsubscribe(ndb, subid));

	ndb_begin_query(ndb, &txn);
	giftwrap = ndb_get_note_by_key(&txn, note_ids[0], NULL);
	assert(giftwrap);

	ndb_end_query(&txn);

	ndb_filter_destroy(&filter);
	kind_filter(&filter, 1);

	subid = ndb_subscribe(ndb, &filter, 1);

	ndb_add_key(ndb, recv_sec);
	ndb_begin_query(ndb, &txn);
	ndb_process_giftwraps(ndb, &txn);
	ndb_end_query(&txn);

	ok = ndb_wait_for_notes(ndb, subid, note_ids,
				sizeof(note_ids)/sizeof(note_ids[0]));
	assert(ndb_unsubscribe(ndb, subid));

	ndb_begin_query(ndb, &txn);
	rumor = ndb_get_note_by_key(&txn, note_ids[0], NULL);
	assert(rumor);

	assert(ndb_note_is_rumor(rumor) == 1);
	assert(ndb_note_kind(rumor) == 1);
	assert(!strcmp(ndb_note_content(rumor), "hi"));
	assert(!memcmp(ndb_note_rumor_giftwrap_id(rumor), giftwrap_id, 32));
	assert(!memcmp(ndb_note_rumor_receiver_pubkey(rumor), recv_pub, 32));
	ndb_end_query(&txn);

	/* wait for updated giftwrap */
	ndb_filter_destroy(&filter);
	kind_filter(&filter, 1059);

	subid = ndb_subscribe(ndb, &filter, 1);
	ok = ndb_wait_for_notes(ndb, subid, note_ids,
				sizeof(note_ids)/sizeof(note_ids[0]));

	ndb_begin_query(ndb, &txn);
	giftwrap = ndb_get_note_by_key(&txn, note_ids[0], NULL);
	assert(giftwrap);
	assert(*ndb_note_flags(giftwrap) & NDB_NOTE_FLAG_UNWRAPPED);
	ndb_note_json(rumor, buf, sizeof(buf));
	//printf("# rumor json: '%s'\n", buf);

	ndb_end_query(&txn);

	ndb_filter_destroy(&filter);
	ndb_destroy(ndb);
	printf("ok test_giftwrap_reprocess\n");
}

static void test_giftwrap_unwrap()
{
	struct ndb *ndb;
	struct ndb_filter filter;
	struct ndb_config config;
	struct ndb_txn txn;
	struct ndb_note *rumor, *giftwrap;
	int ok;
	uint64_t subid;
	ndb_default_config(&config);
	const char *giftwrap_json;
	uint64_t note_ids[2];
	char buf[4096];

	static unsigned char recv_sec[32] = {
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2
	};

	static const unsigned char recv_pub[32] = {
		0xc6, 0x04, 0x7f, 0x94, 0x41, 0xed, 0x7d, 0x6d, 0x30, 0x45,
		0x40, 0x6e, 0x95, 0xc0, 0x7c, 0xd8, 0x5c, 0x77, 0x8e, 0x4b,
		0x8c, 0xef, 0x3c, 0xa7, 0xab, 0xac, 0x09, 0xb9, 0x5c, 0x70,
		0x9e, 0xe5
	};

	static const unsigned char rumor_id[32] = {
		0x3e, 0x12, 0xe5, 0xc1, 0xc9, 0xa8, 0x9f, 0x3f, 0x08, 0x04,
		0x62, 0x3f, 0xd7, 0x61, 0x01, 0xcf, 0xff, 0xba, 0xdf, 0x0d,
		0x02, 0x59, 0x38, 0xdb, 0x64, 0x10, 0x55, 0xe6, 0x00, 0xdc,
		0x8f, 0x73
	};

	static const unsigned char giftwrap_id[32] = {
		0x94, 0x1e, 0xaf, 0x4a, 0x9b, 0xd0, 0x09, 0x0a, 0xb5, 0xd4,
		0x14, 0xff, 0x5a, 0x68, 0x25, 0x4d, 0xa5, 0x78, 0x6f, 0x0b,
		0xf8, 0xb8, 0xe1, 0x56, 0xbc, 0x37, 0xa7, 0x7e, 0xa2, 0x68,
		0x0a, 0x92
	};

	delete_test_db();
	assert(ndb_init(&ndb, test_dir, &config));

	//ndb_add_key(ndb, one);
	ndb_add_key(ndb, recv_sec);

	ndb_filter_init(&filter);
	ndb_filter_start_field(&filter, NDB_FILTER_IDS);
	ndb_filter_add_id_element(&filter, rumor_id);
	ndb_filter_end_field(&filter);
	ndb_filter_end(&filter);

	subid = ndb_subscribe(ndb, &filter, 1);

	giftwrap_json = "{\"id\":\"941eaf4a9bd0090ab5d414ff5a68254da5786f0bf8b8e156bc37a77ea2680a92\",\"pubkey\":\"5fac96633ffd0f68a037778dc40d7747ddf0ceaadb7986c23c0597a56b9ab6f6\",\"created_at\":1764513655,\"kind\":1059,\"tags\":[[\"p\",\"c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5\"]],\"content\":\"AmoRXHPZ6cYI2YR1PL1J2BpxY8onrJHUvt3cOhAe1IOIk6wnighedqyUqQsJ/Bnq0qAAkldMipUuHmr1TRNeu/3Rf3AZ1+4N6/eSJb2GcVaaWkqJ6DIIW+YHvQZRuS/1ef3lo41+2zOFqJMvLiz/Pw1kLdDdi5pKWy0QHJIKFPuIE9XNhFMTMo+rYSIlpfiAXtzpM6qETFBJxabmaZOMlgnxQG6Mm2gB3Y6rmj9gLgxsPCaOnvmfi5xhA+jRvz8FYQ+WIU1ij7SFsssz27fISRRIMEOoq7CHPcsdY2Ly/PSp1t+aLpSz1e8Chnhf3lq/Y0mumLDS/BzNaFm38wQqCoWbxrn+7iLTMfINXoDm7gY6Qb4eZc/Am/wjRJsHm4F37bPiCAupzYWOPdJWO3a4TPmGaHk6MmyrHRu4V20iZK+svdJ257RuwMrYk39xCWNG985t1acPc5Of+ZKDnUcL5jVw0ZUOTrV10uq/UVoOHwDkz0mcsJ0Adhc0fKgiE7CWZgKbaO0PIaAL0I+2hs350dcEK3Go8vzMfiN1Uc7NUUMpjQzIRJC1J6CVf6HkvvQ+JD52RhXsVZN8TTkrpxEDqvCm8eSpamqIZuMbrTh5jJ/J+S83dI8rHvyAcmGrRes5wMOiVaFPglreO/H//AgvcR+N/zYOrV4qCWboU+oTBz7A/ZzMGGt6mhL4dmDKDW3p/HT0l5dEfAn71fdx/VH+jXquVr/9rCw2F4kLzXiNB6E3OBxp7bjkgnmAWCXeNL8NRcOT2l6LaF1l+xi3+NJGxP6tdry+OYz/C12jAZ223HWtQv62fLS3wAuwJH3QjrYromeWU+MNyTdMEFRyuxMq9mpYno7tYF6FpVAR7w9oIZME7F36+1I2b7MERXsgioEBGJFWotC9v5nEKWLJJajZFPlsFQf96Y6Cwovd4moVNmDJ8s2riRDx9D9NxDJxNkz0l+JSLTloTI9p7uMPC6LJtP6qgNAdNtasrUnPo8z5h7kYJR0U8ClsZbOZcf13cSdZck9jZp0kqiSBREhVHGLQmqirXm7UEnoTZYn8U74l3wsetgGiQ3AOAQ94REbzt1obHtGj4JG9Fd3KydWNMcOv1hLODw==\",\"sig\":\"ee99080081a408aa2ce0e2372a44aa0eb1e8e60aa7b4330909d7c7a701d84076a7815412c83a7aa1b783ed0942b0f5e2e2f63efeee8aa0c69503971c65370c3d\"}";

	ndb_process_event(ndb, giftwrap_json, strlen(giftwrap_json));

	ok = ndb_wait_for_notes(ndb, subid, note_ids,
				sizeof(note_ids)/sizeof(note_ids[0]));
	assert(ok == 1);

	ndb_begin_query(ndb, &txn);
	rumor = ndb_get_note_by_key(&txn, note_ids[0], NULL);

	assert(ndb_note_is_rumor(rumor) == 1);
	assert(ndb_note_kind(rumor) == 1);
	assert(!strcmp(ndb_note_content(rumor), "hi"));
	assert(!memcmp(ndb_note_rumor_giftwrap_id(rumor), giftwrap_id, 32));
	assert(!memcmp(ndb_note_rumor_receiver_pubkey(rumor), recv_pub, 32));
	ndb_end_query(&txn);
	ndb_begin_query(ndb, &txn);
	giftwrap = ndb_get_note_by_id(&txn, giftwrap_id, NULL, NULL);
	assert(giftwrap);
	assert(*ndb_note_flags(giftwrap) & NDB_NOTE_FLAG_UNWRAPPED);
	ndb_note_json(rumor, buf, sizeof(buf));
	//printf("# rumor json: '%s'\n", buf);

	ndb_end_query(&txn);

	ndb_filter_destroy(&filter);
	ndb_destroy(ndb);
	printf("ok test_giftwrap_unwrap\n");
}

static void test_metadata()
{
	unsigned char buffer[1024];
	union ndb_reaction_str str;
	struct ndb_note_meta_builder builder;
	struct ndb_note_meta *meta;
	struct ndb_note_meta_entry *entry = NULL;
	int ok;

	ok = ndb_note_meta_builder_init(&builder, buffer, sizeof(buffer));
	assert(ok);

	entry = ndb_note_meta_add_entry(&builder);
	assert(entry);

	ndb_reaction_set(&str, "🏴‍☠️");
	ndb_note_meta_reaction_set(entry, 1337, str);

	ndb_note_meta_build(&builder, &meta);

	assert(ndb_note_meta_entries_count(meta) == 1);
	assert(ndb_note_meta_total_size(meta) == 32);

	entry = ndb_note_meta_entries(meta);
	assert(*ndb_note_meta_reaction_count(entry) == 1337);

	printf("ok test_metadata\n");
}

static void test_reaction_encoding()
{
	union ndb_reaction_str reaction;
	assert(ndb_reaction_set(&reaction, "👩🏻‍🤝‍👩🏿"));
	assert(reaction.binmoji == 0x07D1A7747240B0D0);
	assert(ndb_reaction_str_is_emoji(reaction) == 1);
	assert(ndb_reaction_set(&reaction, "hello"));
	assert(ndb_reaction_str_is_emoji(reaction) == 0);
	assert(!strcmp(reaction.packed.str, "hello"));
	printf("ok test_reaction_encoding\n");
}


static void test_filters()
{
	struct ndb_filter filter, *f;
	struct ndb_filter_elements *current;
	struct ndb_note *note;
	unsigned char buffer[4096];

	const char *test_note = "{\"id\": \"160e76ca67405d7ce9ef7d2dd72f3f36401c8661a73d45498af842d40b01b736\",\"pubkey\": \"67c67870aebc327eb2a2e765e6dbb42f0f120d2c4e4e28dc16b824cf72a5acc1\",\"created_at\": 1700688516,\"kind\": 1337,\"tags\": [[\"t\",\"hashtag\"],[\"t\",\"grownostr\"],[\"p\",\"4d2e7a6a8e08007ace5a03391d21735f45caf1bf3d67b492adc28967ab46525e\"]],\"content\": \"\",\"sig\": \"20c2d070261ed269559ada40ca5ac395c389681ee3b5f7d50de19dd9b328dd70cf27d9d13875e87c968d9b49fa05f66e90f18037be4529b9e582c7e2afac3f06\"}";

	f = &filter;
	assert(ndb_note_from_json(test_note, strlen(test_note), &note, buffer, sizeof(buffer)));

	assert(ndb_filter_init(f));
	assert(ndb_filter_start_field(f, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(f, 1337));
	assert(ndb_filter_add_int_element(f, 2));

	current = ndb_filter_current_element(f);
	assert(current->count == 2);
	assert(current->field.type == NDB_FILTER_KINDS);

	// can't start if we've already started
	assert(ndb_filter_start_field(f, NDB_FILTER_KINDS) == 0);
	assert(ndb_filter_start_field(f, NDB_FILTER_TAGS) == 0);
	ndb_filter_end_field(f);

	// should be sorted after end
	assert(current->elements[0] == 2);
	assert(current->elements[1] == 1337);

	ndb_filter_end(f);

	// try matching the filter
	assert(ndb_filter_matches(f, note));

	_ndb_note_set_kind(note, 1);

	// inverse match
	assert(!ndb_filter_matches(f, note));

	// should also match 2
	_ndb_note_set_kind(note, 2);
	assert(ndb_filter_matches(f, note));

	// don't free, just reset data pointers
	ndb_filter_destroy(f);
	ndb_filter_init(f);

	// now try generic matches
	assert(ndb_filter_start_tag_field(f, 't'));
	assert(ndb_filter_add_str_element(f, "grownostr"));
	ndb_filter_end_field(f);
	assert(ndb_filter_start_field(f, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(f, 3));
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	// shouldn't match the kind filter
	assert(!ndb_filter_matches(f, note));

	_ndb_note_set_kind(note, 3);

	// now it should
	assert(ndb_filter_matches(f, note));

	ndb_filter_destroy(f);
	ndb_filter_init(f);
	assert(ndb_filter_start_field(f, NDB_FILTER_AUTHORS));
	assert(ndb_filter_add_id_element(f, ndb_note_pubkey(note)));
	ndb_filter_end_field(f);
	ndb_filter_end(f);
	assert(f->current == -1);
	assert(ndb_filter_matches(f, note));

	ndb_filter_destroy(f);
}

static void test_filter_json()
{
	struct ndb_filter filter, *f = &filter;
	char buf[1024];

	unsigned char pk[32] = { 0x32, 0xe1, 0x82, 0x76, 0x35, 0x45, 0x0e, 0xbb, 0x3c, 0x5a, 0x7d, 0x12, 0xc1, 0xf8, 0xe7, 0xb2, 0xb5, 0x14, 0x43, 0x9a, 0xc1, 0x0a, 0x67, 0xee, 0xf3, 0xd9, 0xfd, 0x9c, 0x5c, 0x68, 0xe2, 0x45 };

	unsigned char pk2[32] = {
		0xd1, 0x2c, 0x17, 0xbd, 0xe3, 0x09, 0x4a, 0xd3, 0x2f, 0x4a, 0xb8, 0x62, 0xa6, 0xcc, 0x6f, 0x5c, 0x28, 0x9c, 0xfe, 0x7d, 0x58, 0x02, 0x27, 0x0b, 0xdf, 0x34, 0x90, 0x4d, 0xf5, 0x85, 0xf3, 0x49
	};

	ndb_filter_init(f);
	assert(ndb_filter_start_field(f, NDB_FILTER_UNTIL));
	assert(ndb_filter_add_int_element(f, 42));
	ndb_filter_end_field(f);
	ndb_filter_end(f);
	assert(ndb_filter_json(f, buf, sizeof(buf)));
	assert(!strcmp("{\"until\":42}", buf));
	ndb_filter_destroy(f);

	ndb_filter_init(f);
	assert(ndb_filter_start_field(f, NDB_FILTER_UNTIL));
	assert(ndb_filter_add_int_element(f, 42));
	ndb_filter_end_field(f);
	assert(ndb_filter_start_field(f, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(f, 1));
	assert(ndb_filter_add_int_element(f, 2));
	ndb_filter_end_field(f);
	ndb_filter_end(f);
	assert(ndb_filter_json(f, buf, sizeof(buf)));
	assert(!strcmp("{\"until\":42,\"kinds\":[1,2]}", buf));
	ndb_filter_destroy(f);

	ndb_filter_init(f);
	assert(ndb_filter_start_field(f, NDB_FILTER_IDS));
	assert(ndb_filter_add_id_element(f, pk));
	assert(ndb_filter_add_id_element(f, pk2));
	ndb_filter_end_field(f);
	ndb_filter_end(f);
	assert(ndb_filter_json(f, buf, sizeof(buf)));
	assert(!strcmp("{\"ids\":[\"32e1827635450ebb3c5a7d12c1f8e7b2b514439ac10a67eef3d9fd9c5c68e245\",\"d12c17bde3094ad32f4ab862a6cc6f5c289cfe7d5802270bdf34904df585f349\"]}", buf));
	ndb_filter_destroy(f);
}

static void test_invoice_encoding(const char *bolt11_str)
{
	unsigned char buf[4096];
	char *fail = NULL;
	struct cursor cur;
	struct ndb_invoice invoice;
	struct bolt11 *bolt11;

	bolt11 = bolt11_decode_minimal(NULL, bolt11_str, &fail);
	make_cursor(buf, buf + sizeof(buf), &cur);

	if (fail != NULL) {
		printf("invoice decoding failed: %s\n", fail);
	}
	assert(fail == NULL);
	assert(ndb_encode_invoice(&cur, bolt11));
	cur.p = cur.start;
	assert(ndb_decode_invoice(&cur, &invoice));

	assert(bolt11->msat->millisatoshis == invoice.amount);
	assert(bolt11->timestamp == invoice.timestamp);
	assert(bolt11->expiry == invoice.expiry);

	if (bolt11->description != NULL && invoice.description != NULL)
		assert(!strcmp(bolt11->description, invoice.description));
	else if (bolt11->description_hash != NULL && invoice.description_hash != NULL)
		assert(!memcmp(bolt11->description_hash->u.u8, invoice.description_hash, 32));
	else
		assert(0);

	tal_free(bolt11);
}

static void test_encode_decode_invoice()
{
	const char *deschash = "lnbc12n1pjctuljsp57l6za0xry37prkrz7vuv4324ljnssm8ukr2vrf6qvvrgclsmpyhspp5xqfuk89duzjlt2yg56ym7p3enrfxxltyfpc364qc8nsu3kznkl8shp5eugmd894yph7wq68u09gke5x2hmn7mg3zrwd06fs57gmcrjm0uxsxqyjw5qcqpjrzjqd7yw3w4kvhx8uvcj7qusfw4uqre3j56zjz9t07nd2u55yuya3awsrqdlcqqdzcqqqqqqqqqqqqqqzqqyg9qxpqysgqwm2tsc448ellvf5xem2c95hfvc07lakph9r8hffh704uxqhs22r9s4ly0jel48zv6f7fy8zjkgmjt5h2l4jc9gyj4av42s40qvve2ysqwuega8";
	const char *desc = "lnbc12u1pjctuklsp5lg8wdhq2g5xfphkqd5k6gf0femt06wfevu94uuqfprc4ggyqma7spp54lmpmz0mhv3lczepdckr0acf3gdany2654u4k2s8fp5xh0yanjhsdq5w3jhxapdd9h8vmmfvdjsxqyjw5qcqpjrzjqgtsq68q0s9wdadpg32gcfu7hslgkhdpaysj2ha3dtnm8882wa6jyzahpqqqpsgqqyqqqqlgqqqqqpsq9q9qxpqysgqdqzhl8gz46nmalhg27stl25z2u7mqtclv3zz223mjwut90m24fa46xqprjewsqys78j2uljfznz5vtefctu6fw7375ee66e62tj965gpcs85tc";
	const char *problem = "lnbc1230n1p5fetpfpp5mqn7v09jz8pkxl67h4hgd8z2xuqfzfhlw0d4yu5dz4z35ermszaqdq57z0cadhsn78tduylnztscqzzsxqyz5vqrzjqvueefmrckfdwyyu39m0lf24sqzcr9vcrmxrvgfn6empxz7phrjxvrttncqq0lcqqyqqqqlgqqqqqqgq2qsp5mhdv3kgh8y57hd0nezqk0yqhdtkjecnykfxer2k4geg7x34xvqyq9qxpqysgqylpwwyjlvfhc4jzw5hl77a5ajdf7ay6hku7vpznc9efe8nw0h2jp58p7hl2km3hsf3k40z6tey4ye26zf3wwt77ws02rdzzl3cem97squshha0";

	test_invoice_encoding(deschash);
	test_invoice_encoding(desc);
	test_invoice_encoding(problem);
}

// Test the created_at query plan via a contact-list query
static void test_timeline_query()
{
	struct ndb *ndb;
	struct ndb_filter filter;
	struct ndb_config config;
	struct ndb_txn txn;
	struct ndb_query_result results[10];
	int count;
	ndb_default_config(&config);

	assert(ndb_init(&ndb, test_dir, &config));

	ndb_filter_init(&filter);
	ndb_filter_start_field(&filter, NDB_FILTER_AUTHORS);
#include "testdata/author-filter.c"
	ndb_filter_end_field(&filter);
	ndb_filter_end(&filter);

	ndb_begin_query(ndb, &txn);
	assert(ndb_query(&txn, &filter, 1, results,
			 sizeof(results)/sizeof(results[0]), &count));
	ndb_end_query(&txn);
	ndb_filter_destroy(&filter);

	assert(count == 10);
}

// Test fetched_at profile records. These are saved when new profiles are
// processed, or the last time we've fetched the profile.
static void test_fetched_at()
{
	struct ndb *ndb;
	struct ndb_txn txn;
	uint64_t fetched_at, t1, t2;
	struct ndb_config config;
	ndb_default_config(&config);

	assert(ndb_init(&ndb, test_dir, &config));

	const unsigned char pubkey[] = { 0x87, 0xfb, 0xc6, 0xd5, 0x98, 0x31, 0xa8, 0x23, 0xa4, 0x5d, 0x10, 0x1f,
  0x86, 0x94, 0x2c, 0x41, 0xcd, 0xe2, 0x90, 0x23, 0xf4, 0x09, 0x20, 0x24,
  0xa2, 0x7c, 0x50, 0x10, 0x3c, 0x15, 0x40, 0x01 };

	const char profile_1[] = "[\"EVENT\",{\"id\": \"a44eb8fb6931d6155b04038bef0624407e46c85c61e5758392cbb615f00184ca\",\"pubkey\": \"87fbc6d59831a823a45d101f86942c41cde29023f4092024a27c50103c154001\",\"created_at\": 1695593354,\"kind\": 0,\"tags\": [],\"content\": \"{\\\"name\\\":\\\"b\\\"}\",\"sig\": \"7540bbde4b4479275e20d95acaa64027359a73989927f878825093cba2f468bd8e195919a77b4c230acecddf92e6b4bee26918b0c0842f84ec7c1fae82453906\"}]";

	t1 = time(NULL);

	// process the first event, this should set the fetched_at
	assert(ndb_process_client_event(ndb, profile_1, sizeof(profile_1)));

	// we sleep for a second because we want to make sure the fetched_at is not
	// updated for the next record, which is an older profile.
	sleep(1);

	assert(ndb_begin_query(ndb, &txn));

	// this should be set to t1
	fetched_at = ndb_read_last_profile_fetch(&txn, pubkey);

	if (fetched_at != t1) {
		printf("fetched_at != t1? %" PRIu64 " != %" PRIu64 "\n", fetched_at, t1);
	}
	assert(fetched_at == t1);

	t2 = time(NULL);
	assert(t1 != t2); // sanity

	const char profile_2[] = "[\"EVENT\",{\"id\": \"9b2861dda8fc602ec2753f92f1a443c9565de606e0c8f4fd2db4f2506a3b13ca\",\"pubkey\": \"87fbc6d59831a823a45d101f86942c41cde29023f4092024a27c50103c154001\",\"created_at\": 1695593347,\"kind\": 0,\"tags\": [],\"content\": \"{\\\"name\\\":\\\"a\\\"}\",\"sig\": \"f48da228f8967d33c3caf0a78f853b5144631eb86c7777fd25949123a5272a92765a0963d4686dd0efe05b7a9b986bfac8d43070b234153acbae5006d5a90f31\"}]";

	t2 = time(NULL);

	// process the second event, since this is older it should not change
	// fetched_at
	assert(ndb_process_client_event(ndb, profile_2, sizeof(profile_2)));

	// we sleep for a second because we want to make sure the fetched_at is not
	// updated for the next record, which is an older profile.
	sleep(1);

	fetched_at = ndb_read_last_profile_fetch(&txn, pubkey);
	assert(fetched_at == t1);

	ndb_end_query(&txn);
	ndb_destroy(ndb);
}

static void test_reaction_counter()
{
	struct ndb *ndb;
	int reactions, results;
	struct ndb_txn txn;
	struct ndb_note_meta_entry *entry;
	struct ndb_note_meta *meta;
	struct ndb_config config;
	ndb_default_config(&config);
	static const int num_reactions = 3;
	uint64_t note_ids[num_reactions], subid;
	union ndb_reaction_str str;

	assert(ndb_init(&ndb, test_dir, &config));
	assert((subid = ndb_subscribe(ndb, NULL, 0)));

	db_load_events(ndb, "testdata/reactions.json");

	for (reactions = 0; reactions < num_reactions;) {
		results = ndb_wait_for_notes(ndb, subid, note_ids, num_reactions);
		reactions += results;
		assert(reactions > 0);
	}

	assert(ndb_begin_query(ndb, &txn));

	const unsigned char id[32] = {
	  0x1a, 0x41, 0x56, 0x30, 0x31, 0x09, 0xbb, 0x4a, 0x66, 0x0a, 0x6a, 0x90,
	  0x04, 0xb0, 0xcd, 0xce, 0x8d, 0x83, 0xc3, 0x99, 0x1d, 0xe7, 0x86, 0x4f,
	  0x18, 0x76, 0xeb, 0x0f, 0x62, 0x2c, 0x68, 0xe8
	};

	assert((meta = ndb_get_note_meta(&txn, id)));
	ndb_reaction_set(&str, "+");
	entry = ndb_note_meta_find_entry(meta, NDB_NOTE_META_REACTION, &str.binmoji);
	assert(entry);
	//printf("+ count %d\n", *ndb_note_meta_reaction_count(entry));
	assert(*ndb_note_meta_reaction_count(entry) == 1);
	ndb_reaction_set(&str, "-");
	entry = ndb_note_meta_find_entry(meta, NDB_NOTE_META_REACTION, &str.binmoji);
	assert(entry);
	assert(*ndb_note_meta_reaction_count(entry) == 1);
	ndb_end_query(&txn);
	ndb_destroy(ndb);
	delete_test_db();
}

static void test_profile_search(struct ndb *ndb)
{
	struct ndb_txn txn;
	struct ndb_search search;
	int i;
	const char *name;
	NdbProfile_table_t profile;

	assert(ndb_begin_query(ndb, &txn));
	assert(ndb_search_profile(&txn, &search, "jean"));
	//print_search(&txn, &search);
	profile = lookup_profile(&txn, search.profile_key);
	name = NdbProfile_name_get(profile);
	assert(!strncmp(name, "jean", 4));

	assert(ndb_search_profile_next(&search));
	//print_search(&txn, &search);
	profile = lookup_profile(&txn, search.profile_key);
	name = NdbProfile_name_get(profile);
	//assert(strncmp(name, "jean", 4));

	for (i = 0; i < 3; i++) {
		ndb_search_profile_next(&search);
		//print_search(&txn, &search);
	}

	//assert(!strcmp(name, "jb55"));

	ndb_search_profile_end(&search);
	ndb_end_query(&txn);
}

static void test_profile_updates()
{
	static const int alloc_size = 1024 * 1024;
	static const int num_notes = 3;
	char *json;
	int written, i;
	size_t len;
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_txn txn;
	uint64_t key, subid;
	uint64_t note_ids[num_notes];
	void *record;

	json = malloc(alloc_size);

	ndb_default_config(&config);
	assert(ndb_init(&ndb, test_dir, &config));

	subid = ndb_subscribe(ndb, NULL, 0);

	ndb_debug("testing profile updates\n");
	read_file("testdata/profile-updates.json", (unsigned char*)json, alloc_size, &written);
	assert(ndb_process_client_events(ndb, json, written));

	for (i = 0; i < num_notes;)
		i += ndb_wait_for_notes(ndb, subid, note_ids, num_notes);

	assert(ndb_begin_query(ndb, &txn));
	const unsigned char pk[32] = {
		0x1c, 0x55, 0x46, 0xe4, 0xf5, 0x93, 0x3b, 0xbe, 0x86, 0x66,
		0x2a, 0x8e, 0xc3, 0x28, 0x9a, 0x29, 0x87, 0xc0, 0x5d, 0xab,
		0x25, 0x6c, 0x06, 0x8b, 0x77, 0x42, 0x9f, 0x0f, 0x08, 0xa7,
		0xa0, 0x90
	};
	record = ndb_get_profile_by_pubkey(&txn, pk, &len, &key);

	assert(record);
	int res = NdbProfileRecord_verify_as_root(record, len);
	assert(res == 0);

	NdbProfileRecord_table_t profile_record = NdbProfileRecord_as_root(record);
	NdbProfile_table_t profile = NdbProfileRecord_profile_get(profile_record);
	const char *name = NdbProfile_name_get(profile);

	assert(!strcmp(name, "c"));

	ndb_end_query(&txn);
	ndb_destroy(ndb);
	free(json);
}

static void test_load_profiles()
{
	static const int alloc_size = 1024 * 1024;
	char *json = malloc(alloc_size);
	struct ndb *ndb;
	int written;
	struct ndb_config config;
	ndb_default_config(&config);

	assert(ndb_init(&ndb, test_dir, &config));

	read_file("testdata/profiles.json", (unsigned char*)json, alloc_size, &written);

	assert(ndb_process_events(ndb, json, written));

	ndb_destroy(ndb);

	assert(ndb_init(&ndb, test_dir, &config));
	unsigned char id[32] = {
	  0x22, 0x05, 0x0b, 0x6d, 0x97, 0xbb, 0x9d, 0xa0, 0x9e, 0x90, 0xed, 0x0c,
	  0x6d, 0xd9, 0x5e, 0xed, 0x1d, 0x42, 0x3e, 0x27, 0xd5, 0xcb, 0xa5, 0x94,
	  0xd2, 0xb4, 0xd1, 0x3a, 0x55, 0x43, 0x09, 0x07 };
	const char *expected_content = "{\"website\":\"selenejin.com\",\"lud06\":\"\",\"nip05\":\"selenejin@BitcoinNostr.com\",\"picture\":\"https://nostr.build/i/3549697beda0fe1f4ae621f359c639373d92b7c8d5c62582b656c5843138c9ed.jpg\",\"display_name\":\"Selene Jin\",\"about\":\"INTJ | Founding Designer @Blockstream\",\"name\":\"SeleneJin\"}";

	struct ndb_txn txn;
	assert(ndb_begin_query(ndb, &txn));
	struct ndb_note *note = ndb_get_note_by_id(&txn, id, NULL, NULL);
	assert(note != NULL);
	assert(!strcmp(ndb_note_content(note), expected_content));
	ndb_end_query(&txn);

	test_profile_search(ndb);

	ndb_destroy(ndb);

	free(json);
}

static void test_fuzz_events() {
	struct ndb *ndb;
	const char *str = "[\"EVENT\"\"\"{\"content\"\"created_at\":0 \"id\"\"5086a8f76fe1da7fb56a25d1bebbafd70fca62e36a72c6263f900ff49b8f8604\"\"kind\":0 \"pubkey\":9c87f94bcbe2a837adc28d46c34eeaab8fc2e1cdf94fe19d4b99ae6a5e6acedc \"sig\"\"27374975879c94658412469cee6db73d538971d21a7b580726a407329a4cafc677fb56b946994cea59c3d9e118fef27e4e61de9d2c46ac0a65df14153 ea93cf5\"\"tags\"[[][\"\"]]}]";
	struct ndb_config config;
	ndb_default_config(&config);

	ndb_init(&ndb, test_dir, &config);
	ndb_process_event(ndb, str, strlen(str));
	ndb_destroy(ndb);
}

static void test_migrate() {
	static const char *v0_dir = "testdata/db/v0";
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_txn txn;

	ndb_default_config(&config);
	ndb_config_set_flags(&config, NDB_FLAG_NOMIGRATE);

	fprintf(stderr, "testing migrate on v0\n");
	assert(ndb_init(&ndb, v0_dir, &config));
	assert(ndb_begin_query(ndb, &txn));
	assert(ndb_db_version(&txn) == 0);
	assert(ndb_end_query(&txn));
	ndb_destroy(ndb);

	ndb_config_set_flags(&config, 0);

	assert(ndb_init(&ndb, v0_dir, &config));
	ndb_destroy(ndb);
	assert(ndb_init(&ndb, v0_dir, &config));

	assert(ndb_begin_query(ndb, &txn));
	assert(ndb_db_version(&txn) == 3);
	assert(ndb_end_query(&txn));

	test_profile_search(ndb);
	ndb_destroy(ndb);
}

static void test_basic_event() {
	unsigned char buf[512];
	struct ndb_builder builder, *b = &builder;
	struct ndb_note *note;
	int ok;

	unsigned char id[32];
	memset(id, 1, 32);

	unsigned char pubkey[32];
	memset(pubkey, 2, 32);

	unsigned char sig[64];
	memset(sig, 3, 64);

	const char *hex_pk = "5d9b81b2d4d5609c5565286fc3b511dc6b9a1b3d7d1174310c624d61d1f82bb9";

	ok = ndb_builder_init(b, buf, sizeof(buf));
	assert(ok);
	note = builder.note;

	//memset(note->padding, 3, sizeof(note->padding));

	ok = ndb_builder_set_content(b, hex_pk, strlen(hex_pk)); assert(ok);
	ndb_builder_set_id(b, id); assert(ok);
	ndb_builder_set_pubkey(b, pubkey); assert(ok);
	ndb_builder_set_sig(b, sig); assert(ok);

	ok = ndb_builder_new_tag(b); assert(ok);
	ok = ndb_builder_push_tag_str(b, "p", 1); assert(ok);
	ok = ndb_builder_push_tag_str(b, hex_pk, 64); assert(ok);

	ok = ndb_builder_new_tag(b); assert(ok);
	ok = ndb_builder_push_tag_str(b, "word", 4); assert(ok);
	ok = ndb_builder_push_tag_str(b, "words", 5); assert(ok);
	ok = ndb_builder_push_tag_str(b, "w", 1); assert(ok);

	ok = ndb_builder_finalize(b, &note, NULL);
	assert(ok);

	// content should never be packed id
	// TODO: figure out how to test this now that we don't expose it
	// assert(note->content.packed.flag != NDB_PACKED_ID);
	assert(ndb_tags_count(ndb_note_tags(note)) == 2);

	// test iterator
	struct ndb_iterator iter, *it = &iter;

	ndb_tags_iterate_start(note, it);
	ok = ndb_tags_iterate_next(it);
	assert(ok);

	assert(ndb_tag_count(it->tag) == 2);
	const char *p      = ndb_iter_tag_str(it, 0).str;
	struct ndb_str hpk = ndb_iter_tag_str(it, 1);

	hex_decode(hex_pk, 64, id, 32);

	assert(hpk.flag == NDB_PACKED_ID);
	assert(memcmp(hpk.id, id, 32) == 0);
	assert(!strcmp(p, "p"));

	ok = ndb_tags_iterate_next(it);
	assert(ok);
	assert(ndb_tag_count(it->tag) == 3);
	assert(!strcmp(ndb_iter_tag_str(it, 0).str, "word"));
	assert(!strcmp(ndb_iter_tag_str(it, 1).str, "words"));
	assert(!strcmp(ndb_iter_tag_str(it, 2).str, "w"));

	ok = ndb_tags_iterate_next(it);
	assert(!ok);
}

static void test_empty_tags() {
	struct ndb_builder builder, *b = &builder;
	struct ndb_iterator iter, *it = &iter;
	struct ndb_note *note;
	int ok;
	unsigned char buf[1024];

	ok = ndb_builder_init(b, buf, sizeof(buf));
	assert(ok);

	ok = ndb_builder_finalize(b, &note, NULL);
	assert(ok);

	assert(ndb_tags_count(ndb_note_tags(note)) == 0);

	ndb_tags_iterate_start(note, it);
	ok = ndb_tags_iterate_next(it);
	assert(!ok);
}

static void print_tag(struct ndb_note *note, struct ndb_tag *tag) {
	struct ndb_str str;
	int tag_count = ndb_tag_count(tag);
	for (int i = 0; i < tag_count; i++) {
		str = ndb_tag_str(note, tag, i);
		if (str.flag == NDB_PACKED_ID) {
			printf("<id> ");
		} else {
			printf("%s ", str.str);
		}
	}
	printf("\n");
}

static void test_parse_contact_list()
{
	int size, written = 0;
	unsigned char id[32];
	static const int alloc_size = 2 << 18;
	unsigned char *json = malloc(alloc_size);
	unsigned char *buf = malloc(alloc_size);
	struct ndb_note *note;

	read_file("testdata/contacts.json", json, alloc_size, &written);

	size = ndb_note_from_json((const char*)json, written, &note, buf, alloc_size);
	assert(size > 0);
	assert(size == 34328);

	memcpy(id, ndb_note_id(note), 32);
	memset(ndb_note_id(note), 0, 32);
	assert(ndb_calculate_id(note, json, alloc_size, ndb_note_id(note)));
	assert(!memcmp(ndb_note_id(note), id, 32));

	const char* expected_content =
	"{\"wss://nos.lol\":{\"write\":true,\"read\":true},"
	"\"wss://relay.damus.io\":{\"write\":true,\"read\":true},"
	"\"ws://monad.jb55.com:8080\":{\"write\":true,\"read\":true},"
	"\"wss://nostr.wine\":{\"write\":true,\"read\":true},"
	"\"wss://welcome.nostr.wine\":{\"write\":true,\"read\":true},"
	"\"wss://eden.nostr.land\":{\"write\":true,\"read\":true},"
	"\"wss://relay.mostr.pub\":{\"write\":true,\"read\":true},"
	"\"wss://nostr-pub.wellorder.net\":{\"write\":true,\"read\":true}}";

	assert(!strcmp(expected_content, ndb_note_content(note)));
	assert(ndb_note_created_at(note) == 1689904312);
	assert(ndb_note_kind(note) == 3);
	assert(ndb_tags_count(ndb_note_tags(note)) == 786);
	//printf("note content length %d\n", ndb_note_content_length(note));
	//printf("ndb_content_len %d, expected_len %ld\n", ndb_note_content_length(note), strlen(expected_content));
	assert(ndb_note_content_length(note) == strlen(expected_content));

	struct ndb_iterator iter, *it = &iter;
	ndb_tags_iterate_start(note, it);

	int tags = 0;
	int total_elems = 0;

	while (ndb_tags_iterate_next(it)) {
		total_elems += ndb_tag_count(it->tag);
		//printf("tag %d: ", tags);
		if (tags == 0 || tags == 1 || tags == 2)
			assert(ndb_tag_count(it->tag) == 3);

		if (tags == 6)
			assert(ndb_tag_count(it->tag) == 2);

		if (tags == 7)
			assert(!strcmp(ndb_tag_str(note, it->tag, 2).str, "wss://nostr-pub.wellorder.net"));

		if (tags == 786) {
			static unsigned char h[] = { 0x74, 0xfa, 0xe6, 0x66, 0x4c, 0x9e, 0x79, 0x98, 0x0c, 0x6a, 0xc1, 0x1c, 0x57, 0x75, 0xed, 0x30, 0x93, 0x2b, 0xe9, 0x26, 0xf5, 0xc4, 0x5b, 0xe8, 0xd6, 0x55, 0xe0, 0x0e, 0x35, 0xec, 0xa2, 0x88 };
			assert(!memcmp(ndb_tag_str(note, it->tag, 1).id, h, 32));
		}

		//print_tag(it->note, it->tag);

		tags += 1;
	}

	assert(tags == 786);
	//printf("total_elems %d\n", total_elems);
	assert(total_elems == 1580);

	write_file("test_contacts_ndb_note", (unsigned char *)note, size);
	//printf("wrote test_contacts_ndb_note (raw ndb_note)\n");

	free(json);
	free(buf);
}

static void test_replacement()
{
	static const int alloc_size = 1024 * 1024;
	char *json = malloc(alloc_size);
	unsigned char *buf = malloc(alloc_size);
	struct ndb *ndb;
	size_t len;
	int written;
	struct ndb_config config;
	ndb_default_config(&config);

	assert(ndb_init(&ndb, test_dir, &config));

	read_file("testdata/old-new.json", (unsigned char*)json, alloc_size, &written);
	assert(ndb_process_events(ndb, json, written));

	ndb_destroy(ndb);
	assert(ndb_init(&ndb, test_dir, &config));

	struct ndb_txn txn;
	assert(ndb_begin_query(ndb, &txn));

	unsigned char pubkey[32] = { 0x1e, 0x48, 0x9f, 0x6a, 0x4f, 0xc5, 0xc7, 0xac, 0x47, 0x5e, 0xa9, 0x04, 0x17, 0x43, 0xb8, 0x53, 0x11, 0x73, 0x25, 0x92, 0x61, 0xec, 0x71, 0x54, 0x26, 0x41, 0x05, 0x1e, 0x22, 0xa3, 0x82, 0xac };

	void *root = ndb_get_profile_by_pubkey(&txn, pubkey, &len, NULL);

	assert(root);
	int res = NdbProfileRecord_verify_as_root(root, len);
	assert(res == 0);

	NdbProfileRecord_table_t profile_record = NdbProfileRecord_as_root(root);
	NdbProfile_table_t profile = NdbProfileRecord_profile_get(profile_record);
	const char *name = NdbProfile_name_get(profile);

	assert(!strcmp(name, "jb55"));

	ndb_end_query(&txn);

	free(json);
	free(buf);
}

static void test_fetch_last_noteid()
{
	static const int alloc_size = 1024 * 1024;
	char *json = malloc(alloc_size);
	unsigned char *buf = malloc(alloc_size);
	struct ndb *ndb;
	size_t len;
	int written;
	struct ndb_config config;
	ndb_default_config(&config);

	assert(ndb_init(&ndb, test_dir, &config));

	read_file("testdata/random.json", (unsigned char*)json, alloc_size, &written);
	assert(ndb_process_events(ndb, json, written));

	ndb_destroy(ndb);

	assert(ndb_init(&ndb, test_dir, &config));

	unsigned char id[32] = { 0xdc, 0x96, 0x4f, 0x4c, 0x89, 0x83, 0x64, 0x13, 0x8e, 0x81, 0x96, 0xf0, 0xc7, 0x33, 0x38, 0xc8, 0xcc, 0x3e, 0xbf, 0xa3, 0xaf, 0xdd, 0xbc, 0x7d, 0xd1, 0x58, 0xb4, 0x84, 0x7c, 0x1e, 0xbf, 0xa0 };

	struct ndb_txn txn;
	assert(ndb_begin_query(ndb, &txn));
	struct ndb_note *note = ndb_get_note_by_id(&txn, id, &len, NULL);
	assert(note != NULL);
	assert(ndb_note_created_at(note) == 1650054135);

	unsigned char pk[32] = { 0x32, 0xe1, 0x82, 0x76, 0x35, 0x45, 0x0e, 0xbb, 0x3c, 0x5a, 0x7d, 0x12, 0xc1, 0xf8, 0xe7, 0xb2, 0xb5, 0x14, 0x43, 0x9a, 0xc1, 0x0a, 0x67, 0xee, 0xf3, 0xd9, 0xfd, 0x9c, 0x5c, 0x68, 0xe2, 0x45 };

	unsigned char profile_note_id[32] = {
		0xd1, 0x2c, 0x17, 0xbd, 0xe3, 0x09, 0x4a, 0xd3, 0x2f, 0x4a, 0xb8, 0x62, 0xa6, 0xcc, 0x6f, 0x5c, 0x28, 0x9c, 0xfe, 0x7d, 0x58, 0x02, 0x27, 0x0b, 0xdf, 0x34, 0x90, 0x4d, 0xf5, 0x85, 0xf3, 0x49
	};

	void *root = ndb_get_profile_by_pubkey(&txn, pk, &len, NULL);

	assert(root);
	int res = NdbProfileRecord_verify_as_root(root, len);
	//printf("NdbProfileRecord verify result %d\n", res);
	assert(res == 0);

	NdbProfileRecord_table_t profile_record = NdbProfileRecord_as_root(root);
	NdbProfile_table_t profile = NdbProfileRecord_profile_get(profile_record);
	const char *lnurl = NdbProfileRecord_lnurl_get(profile_record);
	const char *name = NdbProfile_name_get(profile);
	uint64_t key = NdbProfileRecord_note_key_get(profile_record);
	assert(name);
	assert(lnurl);
	assert(!strcmp(name, "jb55"));
	assert(!strcmp(lnurl, "fixme"));

	//printf("note_key %" PRIu64 "\n", key);

	struct ndb_note *n = ndb_get_note_by_key(&txn, key, NULL);
	ndb_end_query(&txn);
	assert(memcmp(profile_note_id, ndb_note_id(n), 32) == 0);

	//fwrite(profile, len, 1, stdout);

	ndb_destroy(ndb);

	free(json);
	free(buf);
}

static void test_parse_contact_event()
{
	int written;
	static const int alloc_size = 2 << 18;
	char *json = malloc(alloc_size);
	unsigned char *buf = malloc(alloc_size);
	struct ndb_tce tce;

	assert(read_file("testdata/contacts-event.json", (unsigned char*)json,
			 alloc_size, &written));
	assert(ndb_ws_event_from_json(json, written, &tce, buf, alloc_size, NULL));

	assert(tce.evtype == NDB_TCE_EVENT);

	free(json);
	free(buf);
}

static void test_content_len()
{
	int written;
	static const int alloc_size = 2 << 18;
	char *json = malloc(alloc_size);
	unsigned char *buf = malloc(alloc_size);
	struct ndb_tce tce;

	assert(read_file("testdata/failed_size.json", (unsigned char*)json,
			 alloc_size, &written));
	assert(ndb_ws_event_from_json(json, written, &tce, buf, alloc_size, NULL));

	assert(tce.evtype == NDB_TCE_EVENT);
	assert(ndb_note_content_length(tce.event.note) == 0);

	free(json);
	free(buf);
}

static void test_parse_filter_json()
{
	int i;
	unsigned char buffer[1024];
	unsigned char *pid;
	const char *str;
	uint64_t val;
	struct ndb_filter_elements *elems;

	const unsigned char id_bytes[] = { 0x50, 0x04, 0xa0, 0x81, 0xe3, 0x97,
		0xc6, 0xda, 0x9d, 0xc2, 0xf2, 0xd6, 0xb3, 0x13, 0x40, 0x06,
		0xa9, 0xd0, 0xe8, 0xc1, 0xb4, 0x66, 0x89, 0xd9, 0xfe, 0x15,
		0x0b, 0xb2, 0xf2, 0x1a, 0x20, 0x4d };

	const unsigned char id2_bytes[] = { 0xb1, 0x69, 0xf5, 0x96, 0x96, 0x89,
		0x17, 0xa1, 0xab, 0xeb, 0x42, 0x34, 0xd3, 0xcf, 0x3a, 0xa9,
		0xba, 0xee, 0x21, 0x12, 0xe5, 0x89, 0x98, 0xd1, 0x7c, 0x6d,
		0xb4, 0x16, 0xad, 0x33, 0xfe, 0x40 };

#define HEX_ID "5004a081e397c6da9dc2f2d6b3134006a9d0e8c1b46689d9fe150bb2f21a204d"
#define HEX_PK "b169f596968917a1abeb4234d3cf3aa9baee2112e58998d17c6db416ad33fe40"

	static const char *json = "{\"ids\": [\"" HEX_ID "\", \"" HEX_PK "\"], \"kinds\": [1,2,3], \"limit\": 10, \"#e\":[\"" HEX_PK "\"], \"#t\": [\"hashtag\"]}";
	struct ndb_filter filter, *f = &filter;
	ndb_filter_init(f);

	assert(ndb_filter_from_json(json, strlen(json), f, buffer, sizeof(buffer)));
	assert(filter.finalized);

	for (i = 0; i < filter.num_elements; i++) {
		elems = ndb_filter_get_elements(f, i);

		switch (i) {
		case 0:
			assert(elems->field.type == NDB_FILTER_IDS);
			assert(elems->count == 2);

			pid = ndb_filter_get_id_element(f, elems, 0);
			assert(!memcmp(pid, id_bytes, 32));
			pid = ndb_filter_get_id_element(f, elems, 1);
			assert(!memcmp(pid, id2_bytes, 32));
			break;
		case 1:
			assert(elems->field.type == NDB_FILTER_KINDS);
			assert(elems->count == 3);
			val = ndb_filter_get_int_element(elems, 0);
			assert(val == 1);
			val = ndb_filter_get_int_element(elems, 1);
			assert(val == 2);
			val = ndb_filter_get_int_element(elems, 2);
			assert(val == 3);
			break;

		case 2:
			assert(elems->field.type == NDB_FILTER_LIMIT);
			val = ndb_filter_get_int_element(elems, 0);
			assert(val == 10);
			break;

		case 3:
			assert(elems->field.type == NDB_FILTER_TAGS);
			assert(elems->field.tag == 'e');
			pid = ndb_filter_get_id_element(f, elems, 0);
			assert(pid != NULL);
			assert(!memcmp(pid, id2_bytes, 32));
			break;

		case 4:
			assert(elems->field.type == NDB_FILTER_TAGS);
			assert(elems->field.tag == 't');
			str = ndb_filter_get_string_element(f, elems, 0);
			assert(!strcmp(str, "hashtag"));
			break;
		}
	}

	ndb_filter_destroy(f);
}

static void test_parse_json() {
	char hex_id[32] = {0};
	unsigned char buffer[1024];
	struct ndb_note *note;
#define HEX_ID "5004a081e397c6da9dc2f2d6b3134006a9d0e8c1b46689d9fe150bb2f21a204d"
#define HEX_PK "b169f596968917a1abeb4234d3cf3aa9baee2112e58998d17c6db416ad33fe40"
	static const char *json =
		"{\"id\": \"" HEX_ID "\",\"pubkey\": \"" HEX_PK "\",\"created_at\": 1689836342,\"kind\": 1,\"tags\": [[\"p\",\"" HEX_ID "\"], [\"word\", \"words\", \"w\"]],\"content\": \"共通語\",\"sig\": \"e4d528651311d567f461d7be916c37cbf2b4d530e672f29f15f353291ed6df60c665928e67d2f18861c5ca88\"}";
	int ok;

	ok = ndb_note_from_json(json, strlen(json), &note, buffer, sizeof(buffer));
	assert(ok);

	const char *content = ndb_note_content(note);
	unsigned char *id = ndb_note_id(note);

	hex_decode(HEX_ID, 64, hex_id, sizeof(hex_id));

	assert(!strcmp(content, "共通語"));
	assert(!memcmp(id, hex_id, 32));

	assert(ndb_tags_count(ndb_note_tags(note)) == 2);

	struct ndb_iterator iter, *it = &iter;
	ndb_tags_iterate_start(note, it); assert(ok);
	ok = ndb_tags_iterate_next(it); assert(ok);
	assert(ndb_tag_count(it->tag) == 2);
	assert(!strcmp(ndb_iter_tag_str(it, 0).str, "p"));
	assert(!memcmp(ndb_iter_tag_str(it, 1).id, hex_id, 32));

	ok = ndb_tags_iterate_next(it); assert(ok);
	assert(ndb_tag_count(it->tag) == 3);
	assert(!strcmp(ndb_iter_tag_str(it, 0).str, "word"));
	assert(!strcmp(ndb_iter_tag_str(it, 1).str, "words"));
	assert(!strcmp(ndb_iter_tag_str(it, 2).str, "w"));
}

static void test_strings_work_before_finalization() {
	struct ndb_builder builder, *b = &builder;
	struct ndb_note *note;
	int ok;
	unsigned char buf[1024];

	ok = ndb_builder_init(b, buf, sizeof(buf)); assert(ok);
	ndb_builder_set_content(b, "hello", 5);

	assert(!strcmp(ndb_note_content(b->note), "hello"));
	assert(ndb_builder_finalize(b, &note, NULL));

	assert(!strcmp(ndb_note_content(note), "hello"));
}

static void test_parse_content() {
}

static void test_parse_nevent() {
	unsigned char buf[4096];
	const char *content = "nostr:nevent1qqs9qhc0pjvp6jl2w6ppk5cft8ets8fhxy7fcqcjnp7g38whjy0x5aqpzpmhxue69uhkummnw3ezuamfdejsyg86np9a0kajstc8u9h846rmy6320wdepdeydfz8w8cv7kh9sqv02g947d58,#hashtag";
	struct ndb_blocks *blocks;
	struct ndb_block *block = NULL;
	struct nostr_bech32 *bech32;
	struct ndb_block_iterator iterator, *iter;
	iter = &iterator;
	int ok = 0;

	static unsigned char event_id[] = { 0x50, 0x5f, 0x0f, 0x0c, 0x98, 0x1d, 0x4b, 0xea, 0x76, 0x82, 0x1b, 0x53,
  0x09, 0x59, 0xf2, 0xb8, 0x1d, 0x37, 0x31, 0x3c, 0x9c, 0x03, 0x12, 0x98,
  0x7c, 0x88, 0x9d, 0xd7, 0x91, 0x1e, 0x6a, 0x74 };

	assert(ndb_parse_content(buf, sizeof(buf), content, strlen(content), &blocks));
	ndb_blocks_iterate_start(content, blocks, iter);
	assert(blocks->num_blocks == 3);
	while ((block = ndb_blocks_iterate_next(iter))) {
		switch (++ok) {
		case 1:
			assert(ndb_get_block_type(block) == BLOCK_MENTION_BECH32);
			bech32 = ndb_bech32_block(block);
			assert(bech32->type == NOSTR_BECH32_NEVENT);
			assert(!memcmp(bech32->nevent.event_id, event_id, 32));
			break;
		case 2:
			assert(ndb_get_block_type(block) == BLOCK_TEXT);
			assert(ndb_str_block_ptr(ndb_block_str(block))[0] == ',');
			break;
		case 3:
			assert(ndb_get_block_type(block) == BLOCK_HASHTAG);
			assert(!strncmp("hashtag", ndb_str_block_ptr(ndb_block_str(block)), 7));
			break;
		}
	}
	assert(ok == 3);
}

static void test_bech32_parsing() {
	unsigned char buf[4096];
	const char *content = "https://damus.io/notedeck nostr:note1thp5828zk5xujrcuwdppcjnwlz43altca6269demenja3vqm5m2qclq35h";

	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_str_block *str;
	struct ndb_block_iterator iterator, *iter;

	iter = &iterator;
	assert(ndb_parse_content(buf, sizeof(buf), content, strlen(content), &blocks));
	assert(blocks->num_blocks == 3);

	ndb_blocks_iterate_start(content, blocks, iter);
	int i = 0;
	while ((block = ndb_blocks_iterate_next(iter))) {
		str = ndb_block_str(block);
		switch (++i) {
		case 1:
			assert(ndb_get_block_type(block) == BLOCK_URL);
			assert(!strncmp(str->str, "https://damus.io/notedeck", str->len));
			break;
		case 2:
			assert(ndb_get_block_type(block) == BLOCK_TEXT);
			assert(!strncmp(str->str, " ", str->len));
			break;
		case 3:
			assert(ndb_get_block_type(block) == BLOCK_MENTION_BECH32);
			assert(!strncmp(str->str, "note1thp5828zk5xujrcuwdppcjnwlz43altca6269demenja3vqm5m2qclq35h", str->len));
			break;
		}
	}

	assert(i == 3);
}

static void test_single_url_parsing() {
	unsigned char buf[4096];
	const char *content = "https://damus.io/notedeck";

	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_str_block *str;
	struct ndb_block_iterator iterator, *iter;

	iter = &iterator;
	assert(ndb_parse_content(buf, sizeof(buf), content, strlen(content), &blocks));
	assert(blocks->num_blocks == 1);

	ndb_blocks_iterate_start(content, blocks, iter);
	int i = 0;
	while ((block = ndb_blocks_iterate_next(iter))) {
		str = ndb_block_str(block);
		switch (++i) {
		case 1:
			assert(ndb_get_block_type(block) == BLOCK_URL);
			assert(!strncmp(str->str, "https://damus.io/notedeck", str->len));
			break;
		}
	}

	assert(i == 1);
}

static void test_comma_url_parsing() {
	unsigned char buf[4096];
	const char *content = "http://example.com,http://example.com";

	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_str_block *str;
	struct ndb_block_iterator iterator, *iter;

	iter = &iterator;
	assert(ndb_parse_content(buf, sizeof(buf), content, strlen(content), &blocks));
	assert(blocks->num_blocks == 3);

	ndb_blocks_iterate_start(content, blocks, iter);
	int i = 0;
	while ((block = ndb_blocks_iterate_next(iter))) {
		str = ndb_block_str(block);
		switch (++i) {
		case 1:
			assert(ndb_get_block_type(block) == BLOCK_URL);
			assert(!strncmp(str->str, "http://example.com", str->len));
			break;
		case 2:
			assert(ndb_get_block_type(block) == BLOCK_TEXT);
			assert(!strncmp(str->str, ",", str->len));
			break;
		case 3:
			assert(ndb_get_block_type(block) == BLOCK_URL);
			assert(!strncmp(str->str, "http://example.com", str->len));
			break;
		}
	}

	assert(i == 3);
}

static void test_url_parsing() {
	unsigned char buf[4096];
#define DAMUSIO "https://github.com/damus-io"
#define JB55COM "https://jb55.com/"
#define WIKIORG "http://wikipedia.org"
	const char *content = DAMUSIO ", " JB55COM ", " WIKIORG;
	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_str_block *str;
	struct ndb_block_iterator iterator, *iter;
	iter = &iterator;

	assert(ndb_parse_content(buf, sizeof(buf), content, strlen(content), &blocks));
	assert(blocks->num_blocks == 5);

	ndb_blocks_iterate_start(content, blocks, iter);
	int i = 0;
	while ((block = ndb_blocks_iterate_next(iter))) {
		str = ndb_block_str(block);
		switch (++i) {
		case 1:
			assert(ndb_get_block_type(block) == BLOCK_URL);
			assert(!strncmp(str->str, DAMUSIO, str->len));
			break;
		case 2:
			assert(ndb_get_block_type(block) == BLOCK_TEXT);
			assert(!strncmp(str->str, ", ", str->len));
			break;
		case 3:
			assert(ndb_get_block_type(block) == BLOCK_URL);
			assert(!strncmp(str->str, JB55COM, str->len));
			break;
		case 4:
			assert(ndb_get_block_type(block) == BLOCK_TEXT);
			assert(!strncmp(str->str, ", ", str->len));
			break;
		case 5:
			assert(ndb_get_block_type(block) == BLOCK_URL);
			assert(!strncmp(str->str, WIKIORG, str->len)); break;
		}
	}

	assert(i == 5);
}


static void test_bech32_objects() {
	struct nostr_bech32 obj;
	unsigned char buf[4096];
	const char *nevent = "nevent1qqstjtqmd3lke9m3ftv49pagzxth4q2va4hy2m6kprl0p4y6es4vvnspz3mhxue69uhhyetvv9ujuerpd46hxtnfduqsuamn8ghj7mr0vdskc6r0wd6qegay04";

	unsigned char id[32] = {
	  0xb9, 0x2c, 0x1b, 0x6c, 0x7f, 0x6c, 0x97, 0x71, 0x4a, 0xd9, 0x52, 0x87,
	  0xa8, 0x11, 0x97, 0x7a, 0x81, 0x4c, 0xed, 0x6e, 0x45, 0x6f, 0x56, 0x08,
	  0xfe, 0xf0, 0xd4, 0x9a, 0xcc, 0x2a, 0xc6, 0x4e };

	assert(parse_nostr_bech32(buf, sizeof(buf), nevent, strlen(nevent), &obj));
	assert(obj.type == NOSTR_BECH32_NEVENT);
	assert(!memcmp(obj.nevent.event_id, id, 32));
	assert(obj.nevent.relays.num_relays == 2);
	const char damus_relay[] = "wss://relay.damus.io";
	const char local_relay[] = "ws://localhost";
	assert(sizeof(damus_relay)-1 == obj.nevent.relays.relays[0].len);
	assert(!memcmp(obj.nevent.relays.relays[0].str, damus_relay, sizeof(damus_relay)-1));
	assert(!memcmp(obj.nevent.relays.relays[1].str, local_relay, sizeof(local_relay)-1));
}

static void test_tce_eose() {
	unsigned char buf[1024];
	const char json[] = "[\"EOSE\",\"s\"]";
	struct ndb_tce tce;
	int ok;

	ok = ndb_ws_event_from_json(json, sizeof(json), &tce, buf, sizeof(buf), NULL);
	assert(ok);

	assert(tce.evtype == NDB_TCE_EOSE);
	assert(tce.subid_len == 1);
	assert(!memcmp(tce.subid, "s", 1));
}

static void test_tce_command_result() {
	unsigned char buf[1024];
	const char json[] = "[\"OK\",\"\",true,\"blocked: ok\"]";
	struct ndb_tce tce;
	int ok;

	ok = ndb_ws_event_from_json(json, sizeof(json), &tce, buf, sizeof(buf), NULL);
	assert(ok);

	assert(tce.evtype == NDB_TCE_OK);
	assert(tce.subid_len == 0);
	assert(tce.command_result.ok == 1);
	assert(!memcmp(tce.subid, "", 0));
}

static void test_tce_command_result_empty_msg() {
	unsigned char buf[1024];
	const char json[] = "[\"OK\",\"b1d8f68d39c07ce5c5ea10c235100d529b2ed2250140b36a35d940b712dc6eff\",true,\"\"]";
	struct ndb_tce tce;
	int ok;

	ok = ndb_ws_event_from_json(json, sizeof(json), &tce, buf, sizeof(buf), NULL);
	assert(ok);

	assert(tce.evtype == NDB_TCE_OK);
	assert(tce.subid_len == 64);
	assert(tce.command_result.ok == 1);
	assert(tce.command_result.msglen == 0);
	assert(!memcmp(tce.subid, "b1d8f68d39c07ce5c5ea10c235100d529b2ed2250140b36a35d940b712dc6eff", 0));
}

// test to-client event
static void test_tce() {

#define HEX_ID "5004a081e397c6da9dc2f2d6b3134006a9d0e8c1b46689d9fe150bb2f21a204d"
#define HEX_PK "b169f596968917a1abeb4234d3cf3aa9baee2112e58998d17c6db416ad33fe40"
#define JSON "{\"id\": \"" HEX_ID "\",\"pubkey\": \"" HEX_PK "\",\"created_at\": 1689836342,\"kind\": 1,\"tags\": [[\"p\",\"" HEX_ID "\"], [\"word\", \"words\", \"w\"]],\"content\": \"共通語\",\"sig\": \"e4d528651311d567f461d7be916c37cbf2b4d530e672f29f15f353291ed6df60c665928e67d2f18861c5ca88\"}"
	unsigned char buf[1024];
	const char json[] = "[\"EVENT\",\"subid123\"," JSON "]";
	struct ndb_tce tce;
	int ok;

	ok = ndb_ws_event_from_json(json, sizeof(json), &tce, buf, sizeof(buf), NULL);
	assert(ok);

	assert(tce.evtype == NDB_TCE_EVENT);
	assert(tce.subid_len == 8);
	assert(!memcmp(tce.subid, "subid123", 8));

#undef HEX_ID
#undef HEX_PK
#undef JSON
}

#define TEST_BUF_SIZE 10  // For simplicity

static void test_queue_init_pop_push() {
	struct prot_queue q;
	int buffer[TEST_BUF_SIZE];
	int data;

	// Initialize
	assert(prot_queue_init(&q, buffer, sizeof(buffer), sizeof(int)) == 1);

	// Push and Pop
	data = 5;
	assert(prot_queue_push(&q, &data) == 1);
	prot_queue_pop(&q, &data);
	assert(data == 5);

	// Push to full, and then fail to push
	for (int i = 0; i < TEST_BUF_SIZE; i++) {
		assert(prot_queue_push(&q, &i) == 1);
	}
	assert(prot_queue_push(&q, &data) == 0);  // Should fail as queue is full

	// Pop to empty, and then fail to pop
	for (int i = 0; i < TEST_BUF_SIZE; i++) {
		assert(prot_queue_try_pop_all(&q, &data, 1) == 1);
		assert(data == i);
	}
	assert(prot_queue_try_pop_all(&q, &data, 1) == 0);  // Should fail as queue is empty
}

// This function will be used by threads to test thread safety.
void* thread_func(void* arg) {
	struct prot_queue* q = (struct prot_queue*) arg;
	int data;

	for (int i = 0; i < 100; i++) {
		data = i;
		prot_queue_push(q, &data);
		prot_queue_pop(q, &data);
	}
	return NULL;
}

static void test_queue_thread_safety() {
	struct prot_queue q;
	int buffer[TEST_BUF_SIZE];
	pthread_t threads[2];

	assert(prot_queue_init(&q, buffer, sizeof(buffer), sizeof(int)) == 1);

	// Create threads
	for (int i = 0; i < 2; i++) {
		pthread_create(&threads[i], NULL, thread_func, &q);
	}

	// Join threads
	for (int i = 0; i < 2; i++) {
		pthread_join(threads[i], NULL);
	}

	// After all operations, the queue should be empty
	int data;
	assert(prot_queue_try_pop_all(&q, &data, 1) == 0);
}

static void test_queue_boundary_conditions() {
    struct prot_queue q;
    int buffer[TEST_BUF_SIZE];
    int data;

    // Initialize
    assert(prot_queue_init(&q, buffer, sizeof(buffer), sizeof(int)) == 1);

    // Push to full
    for (int i = 0; i < TEST_BUF_SIZE; i++) {
        assert(prot_queue_push(&q, &i) == 1);
    }

    // Try to push to a full queue
    int old_head = q.head;
    int old_tail = q.tail;
    int old_count = q.count;
    assert(prot_queue_push(&q, &data) == 0);

    // Assert the queue's state has not changed
    assert(old_head == q.head);
    assert(old_tail == q.tail);
    assert(old_count == q.count);

    // Pop to empty
    for (int i = 0; i < TEST_BUF_SIZE; i++) {
        assert(prot_queue_try_pop_all(&q, &data, 1) == 1);
    }

    // Try to pop from an empty queue
    old_head = q.head;
    old_tail = q.tail;
    old_count = q.count;
    assert(prot_queue_try_pop_all(&q, &data, 1) == 0);

    // Assert the queue's state has not changed
    assert(old_head == q.head);
    assert(old_tail == q.tail);
    assert(old_count == q.count);
}

static void test_fast_strchr()
{
	// Test 1: Basic test
	const char *testStr1 = "Hello, World!";
	assert(fast_strchr(testStr1, 'W', strlen(testStr1)) == testStr1 + 7);

	// Test 2: Character not present in the string
	assert(fast_strchr(testStr1, 'X', strlen(testStr1)) == NULL);

	// Test 3: Multiple occurrences of the character
	const char *testStr2 = "Multiple occurrences.";
	assert(fast_strchr(testStr2, 'u', strlen(testStr2)) == testStr2 + 1);

	// Test 4: Check with an empty string
	const char *testStr3 = "";
	assert(fast_strchr(testStr3, 'a', strlen(testStr3)) == NULL);

	// Test 5: Check with a one-character string
	const char *testStr4 = "a";
	assert(fast_strchr(testStr4, 'a', strlen(testStr4)) == testStr4);

	// Test 6: Check the last character in the string
	const char *testStr5 = "Last character check";
	assert(fast_strchr(testStr5, 'k', strlen(testStr5)) == testStr5 + 19);

	// Test 7: Large string test (>16 bytes)
	char *testStr6 = "This is a test for large strings with more than 16 bytes.";
	assert(fast_strchr(testStr6, 'm', strlen(testStr6)) == testStr6 + 38);
}

static void test_tag_query()
{
	struct ndb *ndb;
	struct ndb_txn txn;
	struct ndb_filter filters[1], *f = &filters[0];
	struct ndb_config config;
	struct ndb_query_result results[4];
	int count, cap;
	uint64_t subid, note_ids[1];
	ndb_default_config(&config);

	cap = sizeof(results) / sizeof(results[0]);

	assert(ndb_init(&ndb, test_dir, &config));

	const char *ev = "[\"EVENT\",\"s\",{\"id\": \"7fd6e4286e595b60448bf69d8ec4a472c5ad14521555813cdfce1740f012aefd\",\"pubkey\": \"b85beab689aed6a10110cc3cdd6e00ac37a2f747c4e60b18a31f4352a5bfb6ed\",\"created_at\": 1704762185,\"kind\": 1,\"tags\": [[\"t\",\"hashtag\"]],\"content\": \"hi\",\"sig\": \"5b05669af5a322730731b13d38667464ea3b45bef1861e26c99ef1815d7e8d557a76e06afa5fffa1dcd207402b92ae7dda6ef411ea515df2bca58d74e6f2772e\"}]";

	f = &filters[0];
	ndb_filter_init(f);
	ndb_filter_start_tag_field(f, 't');
	ndb_filter_add_str_element(f, "hashtag");
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	assert((subid = ndb_subscribe(ndb, f, 1)));
	assert(ndb_process_event(ndb, ev, strlen(ev)));
	ndb_wait_for_notes(ndb, subid, note_ids, 1);

	ndb_begin_query(ndb, &txn);

	assert(ndb_query(&txn, f, 1, results, cap, &count));
	assert(count == 1);
	assert(!strcmp(ndb_note_content(results[0].note), "hi"));

	ndb_end_query(&txn);
	ndb_filter_destroy(f);
	ndb_destroy(ndb);
}

static void test_query()
{
	struct ndb *ndb;
	struct ndb_txn txn;
	struct ndb_filter filters[2], *f;
	struct ndb_config config;
	struct ndb_query_result results[4];
	int count, cap, nres;
	uint64_t subid, note_ids[4];
	ndb_default_config(&config);

	cap = sizeof(results) / sizeof(results[0]);

	const unsigned char id[] = {
	  0x03, 0x36, 0x94, 0x8b, 0xdf, 0xbf, 0x5f, 0x93, 0x98, 0x02, 0xeb, 0xa0,
	  0x3a, 0xa7, 0x87, 0x35, 0xc8, 0x28, 0x25, 0x21, 0x1e, 0xec, 0xe9, 0x87,
	  0xa6, 0xd2, 0xe2, 0x0e, 0x3c, 0xff, 0xf9, 0x30
	};

	const unsigned char id2[] = {
	  0x0a, 0x35, 0x0c, 0x58, 0x51, 0xaf, 0x6f, 0x6c, 0xe3, 0x68, 0xba, 0xb4,
	  0xe2, 0xd4, 0xfe, 0x44, 0x2a, 0x13, 0x18, 0x64, 0x2c, 0x7f, 0xe5, 0x8d,
	  0xe5, 0x39, 0x21, 0x03, 0x70, 0x0c, 0x10, 0xfc
	};

	const char *ev = "[\"EVENT\",\"s\",{\"id\": \"0336948bdfbf5f939802eba03aa78735c82825211eece987a6d2e20e3cfff930\",\"pubkey\": \"aeadd3bf2fd92e509e137c9e8bdf20e99f286b90be7692434e03c015e1d3bbfe\",\"created_at\": 1704401597,\"kind\": 1,\"tags\": [],\"content\": \"hello\",\"sig\": \"232395427153b693e0426b93d89a8319324d8657e67d23953f014a22159d2127b4da20b95644b3e34debd5e20be0401c283e7308ccb63c1c1e0f81cac7502f09\"}]";
	const char *ev2 = "[\"EVENT\",\"s\",{\"id\": \"0a350c5851af6f6ce368bab4e2d4fe442a1318642c7fe58de5392103700c10fc\",\"pubkey\": \"dfa3fc062f7430dab3d947417fd3c6fb38a7e60f82ffe3387e2679d4c6919b1d\",\"created_at\": 1704404822,\"kind\": 1,\"tags\": [],\"content\": \"hello2\",\"sig\": \"48a0bb9560b89ee2c6b88edcf1cbeeff04f5e1b10d26da8564cac851065f30fa6961ee51f450cefe5e8f4895e301e8ffb2be06a2ff44259684fbd4ea1c885696\"}]";
	const char *ev3 = "[\"EVENT\",\"s\",{\"id\": \"20d2b66e1a3ac4a2afe22866ad742091b6267e6e614303de062adb33e12c9931\",\"pubkey\": \"7987bfb2632d561088fc8e3c30a95836f822e4f53633228ec92ae2f5cd6690aa\",\"created_at\": 1704408561,\"kind\": 2,\"tags\": [],\"content\": \"what\",\"sig\": \"cc8533bf177ac87771a5218a04bed24f7a1706f0b2d92700045cdeb38accc5507c6c8de09525e43190df3652012b554d4efe7b82ab268a87ff6f23da44e16a8f\"}]";
	const char *ev4 = "[\"EVENT\",\"s\",{\"id\": \"8a2057c13c1c57b536eab78e6c55428732d33b6b5b234c1f5eab2b5918c37fa1\",\"pubkey\": \"303b5851504da5caa14142e9e2e1b1b60783c48d6f137c205019d46d09244c26\",\"created_at\": 1704408730,\"kind\": 2,\"tags\": [],\"content\": \"hmm\",\"sig\": \"e7cd3029042d41964192411929cade59592840af766da6420077ccc57a61405312db6ca879150db01f53c3b81c477cec5d6bd49f9dc10937267cacf7e5c784b3\"}]";

	assert(ndb_init(&ndb, test_dir, &config));

	f = &filters[0];
	ndb_filter_init(f);
	ndb_filter_start_field(f, NDB_FILTER_IDS);
	ndb_filter_add_id_element(f, id2);
	ndb_filter_add_id_element(f, id);
	assert(ndb_filter_current_element(f)->count == 2);
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	assert((subid = ndb_subscribe(ndb, f, 1)));

	assert(ndb_process_event(ndb, ev, strlen(ev)));
	assert(ndb_process_event(ndb, ev2, strlen(ev2)));

	for (nres = 2; nres > 0;)
		nres -= ndb_wait_for_notes(ndb, subid, note_ids, 2);

	ndb_begin_query(ndb, &txn);
	assert(ndb_query(&txn, f, 1, results, cap, &count));
	assert(count == 2);
	assert(0 == memcmp(ndb_note_id(results[0].note), id2, 32));
	ndb_end_query(&txn);

	ndb_filter_destroy(f);
	ndb_filter_init(f);
	ndb_filter_start_field(f, NDB_FILTER_KINDS);
	ndb_filter_add_int_element(f, 2);
	ndb_filter_end_field(f);
	ndb_filter_start_field(f, NDB_FILTER_LIMIT);
	ndb_filter_add_int_element(f, 2);
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	assert((subid = ndb_subscribe(ndb, f, 1)));
	assert(ndb_process_event(ndb, ev3, strlen(ev3)));
	assert(ndb_process_event(ndb, ev4, strlen(ev4)));

	for (nres = 2; nres > 0;)
		nres -= ndb_wait_for_notes(ndb, subid, note_ids, 2);
	ndb_begin_query(ndb, &txn);

	count = 0;
	assert(ndb_query(&txn, f, 1, results, cap, &count));
	//ndb_print_kind_keys(&txn);
	assert(count == 2);
	assert(!strcmp(ndb_note_content(results[0].note), "hmm"));
	assert(!strcmp(ndb_note_content(results[1].note), "what"));

	ndb_end_query(&txn);
	ndb_filter_destroy(f);
	ndb_destroy(ndb);
}

static void test_fulltext()
{
	struct ndb *ndb;
	struct ndb_txn txn;
	int written;
	static const int alloc_size = 2 << 18;
	char *json = malloc(alloc_size);
	struct ndb_text_search_results results;
	struct ndb_config config;
	struct ndb_text_search_config search_config;
	ndb_default_config(&config);
	ndb_default_text_search_config(&search_config);

	assert(ndb_init(&ndb, test_dir, &config));

	read_file("testdata/search.json", (unsigned char*)json, alloc_size, &written);
	assert(ndb_process_client_events(ndb, json, written));
	ndb_destroy(ndb);
	assert(ndb_init(&ndb, test_dir, &config));

	ndb_begin_query(ndb, &txn);
	ndb_text_search(&txn, "Jump Over", &results, &search_config);
	ndb_end_query(&txn);

	ndb_destroy(ndb);

	free(json);

}

static void test_varint(uint64_t value) {
	unsigned char buffer[10];
	struct cursor cursor;
	uint64_t result;

	// Initialize cursor
	cursor.start = buffer;
	cursor.p = buffer;
	cursor.end = buffer + sizeof(buffer);

	// Push the value
	assert(cursor_push_varint(&cursor, value));

	// Reset cursor for reading
	cursor.p = buffer;

	// Pull the value
	if (!cursor_pull_varint(&cursor, &result)) {
		printf("Test failed for value %" PRIu64 " \n", value);
		assert(!"Failed to pull value");
	}

	// Check if the pushed and pulled values are the same
	if (value != result) {
		printf("Test failed for value %" PRIu64 "\n", value);
		assert(!"test failed");
	}
}

static int test_varints() {
	test_varint(0);
	test_varint(127); // Edge case for 1-byte varint
	test_varint(128); // Edge case for 2-byte varint
	test_varint(16383); // Edge case for 2-byte varint
	test_varint(16384); // Edge case for 3-byte varint
	test_varint(2097151); // Edge case for 3-byte varint
	test_varint(2097152); // Edge case for 4-byte varint
	test_varint(268435455); // Edge case for 4-byte varint
	test_varint(268435456); // Edge case for 5-byte varint
	test_varint(34359738367ULL); // Edge case for 5-byte varint
	test_varint(34359738368ULL); // Edge case for 6-byte varint
	test_varint(4398046511103ULL); // Edge case for 6-byte varint
	test_varint(4398046511104ULL); // Edge case for 7-byte varint
	test_varint(562949953421311ULL); // Edge case for 7-byte varint
	test_varint(562949953421312ULL); // Edge case for 8-byte varint
	test_varint(72057594037927935ULL); // Edge case for 8-byte varint
	test_varint(72057594037927936ULL); // Edge case for 9-byte varint
	test_varint(9223372036854775807ULL); // Maximum 64-bit integer

	return 0;
}

static void test_subscriptions()
{
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_filter filter, *f = &filter;
	uint64_t subid;
	uint64_t note_id = 0;
	struct ndb_txn txn;
	struct ndb_note *note;
	ndb_default_config(&config);

	const char *ev = "[\"EVENT\",\"s\",{\"id\": \"3718b368de4d01a021990e6e00dce4bdf860caed21baffd11b214ac498e7562e\",\"pubkey\": \"57c811c86a871081f52ca80e657004fe0376624a978f150073881b6daf0cbf1d\",\"created_at\": 1704300579,\"kind\": 1337,\"tags\": [],\"content\": \"test\",\"sig\": \"061c36d4004d8342495eb22e8e7c2e2b6e1a1c7b4ae6077fef09f9a5322c561b88bada4f63ff05c9508cb29d03f50f71ef3c93c0201dbec440fc32eda87f273b\"}]";

	assert(ndb_init(&ndb, test_dir, &config));

	assert(ndb_filter_init(f));
	assert(ndb_filter_start_field(f, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(f, 1337));
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	assert((subid = ndb_subscribe(ndb, f, 1)));

	assert(ndb_process_event(ndb, ev, strlen(ev)));

	assert(ndb_wait_for_notes(ndb, subid, &note_id, 1) == 1);
	assert(note_id > 0);
	assert(ndb_begin_query(ndb, &txn));

	assert((note = ndb_get_note_by_key(&txn, note_id, NULL)));
	assert(!strcmp(ndb_note_content(note), "test"));

	// unsubscribe
	assert(ndb_num_subscriptions(ndb) == 1);
	assert(ndb_unsubscribe(ndb, subid));
	assert(ndb_num_subscriptions(ndb) == 0);

	ndb_end_query(&txn);
	ndb_filter_destroy(f);
	ndb_destroy(ndb);
}

static void test_weird_note_corruption() {
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_str_block *str;
	struct ndb_block_iterator iterator, *iter = &iterator;
	struct ndb_filter filter, *f = &filter;
	uint64_t subid;
	uint64_t note_id = 0;
	struct ndb_txn txn;
	struct ndb_note *note;
	ndb_default_config(&config);

	const char *ev = "[\"EVENT\",\"a\",{\"content\":\"https://damus.io/notedeck\",\"created_at\":1722537589,\"id\":\"1876ca8cd29afba5805e698cf04ac6611d50e5e5a22e1efb895816a4c5790a1b\",\"kind\":1,\"pubkey\":\"32e1827635450ebb3c5a7d12c1f8e7b2b514439ac10a67eef3d9fd9c5c68e245\",\"sig\":\"2478aac6e9e66e5a04938d54544928589b55d2a324a7229ef7e709903b5dd12dc9a279abf81ed5753692cd30f62213fd3e0adf8b835543616a60e2d7010f0627\",\"tags\":[[\"e\",\"423fdf3f6e438fded84fe496643008eada5c1db7ba80428521c2c098f1173b83\",\"\",\"root\"],[\"p\",\"406a61077fe67f0eda4be931572c522f937952ddb024c87673e3de6b37e9a98f\"]]}]";
	assert(ndb_init(&ndb, test_dir, &config));

	assert(ndb_filter_init(f));
	assert(ndb_filter_start_field(f, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(f, 1));
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	assert((subid = ndb_subscribe(ndb, f, 1)));
	assert(ndb_process_event(ndb, ev, strlen(ev)));

	assert(ndb_wait_for_notes(ndb, subid, &note_id, 1) == 1);
	assert(note_id > 0);
	assert(ndb_begin_query(ndb, &txn));

	assert((note = ndb_get_note_by_key(&txn, note_id, NULL)));
	assert(!strcmp(ndb_note_content(note), "https://damus.io/notedeck"));
	assert(ndb_note_content_length(note) == 25);

	assert(ndb_num_subscriptions(ndb) == 1);
	assert(ndb_unsubscribe(ndb, subid));
	assert(ndb_num_subscriptions(ndb) == 0);

	blocks = ndb_get_blocks_by_key(ndb, &txn, note_id);

	ndb_blocks_iterate_start(ndb_note_content(note), blocks, iter);
	//printf("url num blocks: %d\n", blocks->num_blocks);
	assert(blocks->num_blocks == 1);
	int i = 0;
	while ((block = ndb_blocks_iterate_next(iter))) {
		str = ndb_block_str(block);
		//printf("block (%d): %d:'%.*s'\n", ndb_get_block_type(block), str->len, str->len, str->str);
		switch (++i) {
		case 1:
			assert(ndb_get_block_type(block) == BLOCK_URL);
			assert(str->len == 25);
			assert(!strncmp(str->str, "https://damus.io/notedeck", str->len));
			break;
		}
	}
	assert(i == 1);

	ndb_end_query(&txn);
	ndb_filter_destroy(f);
	ndb_destroy(ndb);
}

static void test_filter_eq() {
	struct ndb_filter filter, *f = &filter;
	struct ndb_filter filter2, *f2 = &filter2;

	ndb_filter_init(f);
	assert(ndb_filter_start_field(f, NDB_FILTER_UNTIL));
	assert(ndb_filter_add_int_element(f, 42));
	ndb_filter_end_field(f);
	assert(ndb_filter_start_field(f, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(f, 1));
	assert(ndb_filter_add_int_element(f, 2));
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	ndb_filter_init(f2);
	assert(ndb_filter_start_field(f2, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(f2, 2));
	assert(ndb_filter_add_int_element(f2, 1));
	ndb_filter_end_field(f2);
	assert(ndb_filter_start_field(f2, NDB_FILTER_UNTIL));
	assert(ndb_filter_add_int_element(f2, 42));
	ndb_filter_end_field(f2);
	ndb_filter_end(f2);

	assert(ndb_filter_eq(f, f2));

	ndb_filter_destroy(f);
	ndb_filter_destroy(f2);
}

static void test_filter_is_subset() {
	struct ndb_filter global, *g = &global;
	struct ndb_filter kind, *k = &kind;
	struct ndb_filter kind_and_id, *ki = &kind_and_id;

	const unsigned char id[] = {
	  0x03, 0x36, 0x94, 0x8b, 0xdf, 0xbf, 0x5f, 0x93, 0x98, 0x02, 0xeb, 0xa0,
	  0x3a, 0xa7, 0x87, 0x35, 0xc8, 0x28, 0x25, 0x21, 0x1e, 0xec, 0xe9, 0x87,
	  0xa6, 0xd2, 0xe2, 0x0e, 0x3c, 0xff, 0xf9, 0x30
	};

	ndb_filter_init(g);
	ndb_filter_end(g);

	ndb_filter_init(k);
	assert(ndb_filter_start_field(k, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(k, 1));
	ndb_filter_end_field(k);
	ndb_filter_end(k);

	ndb_filter_init(ki);
	assert(ndb_filter_start_field(ki, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(ki, 1));
	ndb_filter_end_field(ki);
	assert(ndb_filter_start_field(ki, NDB_FILTER_IDS));
	assert(ndb_filter_add_id_element(ki, id));
	ndb_filter_end_field(ki);
	ndb_filter_end(ki);

	assert(ndb_filter_is_subset_of(g, k) == 0);
	assert(ndb_filter_is_subset_of(k, g) == 1);
	assert(ndb_filter_is_subset_of(ki, k) == 1);
	assert(ndb_filter_is_subset_of(k, ki) == 0);

	ndb_filter_destroy(k);
	ndb_filter_destroy(ki);
}

static void test_filter_search()
{
	struct ndb_filter filter, *f = &filter;

	assert(ndb_filter_init_with(f, 1));

	assert(ndb_filter_start_field(f, NDB_FILTER_SEARCH));
	assert(ndb_filter_add_str_element(f, "searchterm"));
	assert(!ndb_filter_add_str_element(f, "searchterm 2"));
	ndb_filter_end_field(f);

	assert(ndb_filter_end(f));
	ndb_filter_destroy(f);
}

static void test_filter_parse_search_json() {
	const char *json = "{\"search\":\"abc\",\"limit\":1}";
	unsigned char buf[1024];
	int i;

	struct ndb_filter filter, *f = &filter;
	struct ndb_filter_elements *es;

	ndb_filter_init_with(f, 1);
	assert(ndb_filter_from_json(json, strlen(json), f, buf, sizeof(buf)));
	assert(filter.finalized);

	assert(f->num_elements == 2);
	for (i = 0; i < f->num_elements; i++) {
		es = ndb_filter_get_elements(f, i);
		if (i == 0) {
			assert(es->field.type == NDB_FILTER_SEARCH);
			assert(es->count == 1);
			assert(!strcmp(ndb_filter_get_string_element(f, es, 0), "abc"));
		} else if (i == 1) {
			assert(es->field.type == NDB_FILTER_LIMIT);
			assert(es->count == 1);
			assert(ndb_filter_get_int_element(es, 0) == 1);
		}
	}

	// test back to json
	assert(ndb_filter_json(f, (char *)buf, sizeof(buf)));
	assert(!strcmp((const char*)buf, json));

	ndb_filter_destroy(f);
}

static void test_note_relay_index()
{
	const char *relay;
	struct ndb *ndb;
	struct ndb_txn txn;
	struct ndb_config config;
	struct ndb_filter filter, *f = &filter;
	uint64_t note_key, subid;
	struct ndb_ingest_meta meta;

	const char *json = "[\"EVENT\",{\"id\": \"0f20295584a62d983a4fa85f7e50b460cd0049f94d8cd250b864bb822a747114\",\"pubkey\": \"55c882cf4a255ac66fc8507e718a1d1283ba46eb7d678d0573184dada1a4f376\",\"created_at\": 1742498339,\"kind\": 1,\"tags\": [],\"content\": \"hi\",\"sig\": \"ae1218280f554ea0b04ae09921031493d60fb7831dfd2dbd7086efeace2719a46842ce80342ebc002da8943df02e98b8b4abb4629c7103ca2114e6c4425f97fe\"}]";

	// Initialize NDB
	ndb_default_config(&config);
	assert(ndb_init(&ndb, test_dir, &config));

	// 1) Ingest the note from “relay1”.
	// Use ndb_ingest_meta_init to record the relay.

	assert(ndb_filter_init(f));
	assert(ndb_filter_start_field(f, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(f, 1));
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	assert((subid = ndb_subscribe(ndb, f, 1)));

	ndb_ingest_meta_init(&meta, 1, "wss://relay.damus.io");
	assert(ndb_process_event_with(ndb, json, strlen(json), &meta));
	meta.relay = "wss://relay.mit.edu";
	assert(ndb_process_event_with(ndb, json, strlen(json), &meta));
	meta.relay = "wss://nostr.mom";
	assert(ndb_process_event_with(ndb, json, strlen(json), &meta));
	meta.relay = "ws://monad.jb55.com:8080";
	assert(ndb_process_event_with(ndb, json, strlen(json), &meta));

	assert(ndb_wait_for_notes(ndb, subid, &note_key, 1) == 1);
	assert(note_key > 0);

	// 4) Check that we have both relays
	assert(ndb_begin_query(ndb, &txn));
	assert(ndb_note_seen_on_relay(&txn, note_key, "wss://relay.damus.io"));
	assert(ndb_note_seen_on_relay(&txn, note_key, "wss://relay.mit.edu"));
	assert(ndb_note_seen_on_relay(&txn, note_key, "wss://nostr.mom"));
	assert(ndb_note_seen_on_relay(&txn, note_key, "ws://monad.jb55.com:8080"));

	// walk the relays
	struct ndb_note_relay_iterator iter;

	assert(ndb_note_relay_iterate_start(&txn, &iter, note_key));

	relay = ndb_note_relay_iterate_next(&iter);
	assert(relay);
	assert(!strcmp(relay, "ws://monad.jb55.com:8080"));

	relay = ndb_note_relay_iterate_next(&iter);
	assert(relay);
	assert(!strcmp(relay, "wss://nostr.mom"));

	relay = ndb_note_relay_iterate_next(&iter);
	assert(relay);
	assert(!strcmp(relay, "wss://relay.damus.io"));

	relay = ndb_note_relay_iterate_next(&iter);
	assert(relay);
	assert(!strcmp(relay, "wss://relay.mit.edu"));

	assert(ndb_note_relay_iterate_next(&iter) == NULL);
	ndb_note_relay_iterate_close(&iter);
	assert(iter.mdb_cur == NULL);

	assert(ndb_end_query(&txn));

	// Cleanup
	ndb_filter_destroy(f);
	ndb_destroy(ndb);

	printf("ok test_note_relay_index\n");
}

static void test_nip50_profile_search() {
	struct ndb *ndb;
	struct ndb_txn txn;
	struct ndb_config config;
	struct ndb_filter filter, *f = &filter;
	int count;
	struct ndb_query_result result;

	// Initialize NDB
	ndb_default_config(&config);
	assert(ndb_init(&ndb, test_dir, &config));

	// 1) Ingest the note from “relay1”.
	// Use ndb_ingest_meta_init to record the relay.

	unsigned char expected_id[32] = {
	  0x22, 0x05, 0x0b, 0x6d, 0x97, 0xbb, 0x9d, 0xa0, 0x9e, 0x90, 0xed, 0x0c,
	  0x6d, 0xd9, 0x5e, 0xed, 0x1d, 0x42, 0x3e, 0x27, 0xd5, 0xcb, 0xa5, 0x94,
	  0xd2, 0xb4, 0xd1, 0x3a, 0x55, 0x43, 0x09, 0x07 };
	assert(ndb_filter_init(f));
	assert(ndb_filter_start_field(f, NDB_FILTER_SEARCH));
	assert(ndb_filter_add_str_element(f, "Selene"));
	ndb_filter_end_field(f);
	assert(ndb_filter_start_field(f, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(f, 0));
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	ndb_begin_query(ndb, &txn);
	ndb_query(&txn, f, 1, &result, 1, &count);
	ndb_end_query(&txn);

	assert(count == 1);
	assert(!memcmp(ndb_note_id(result.note), expected_id, 32));

	// Cleanup
	ndb_filter_destroy(f);
	ndb_destroy(ndb);

	printf("ok test_nip50_profile_search\n");
}

static bool only_threads_filter(void *ctx, struct ndb_note *note)
{
	struct ndb_iterator it;
	struct ndb_str str;
	const char *c;

	ndb_tags_iterate_start(note, &it);

	while (ndb_tags_iterate_next(&it)) {
		if (ndb_tag_count(it.tag) < 4)
			continue;

		str = ndb_iter_tag_str(&it, 0);
		if (str.str[0] != 'e')
			continue;

		str = ndb_iter_tag_str(&it, 3);
		if (str.flag == NDB_PACKED_ID)
			continue;

		if (str.str[0] != 'r')
			continue;

		// if it has a reply or root marker, then this is a reply
		c = &str.str[1];
		if (*c++ == 'e' && *c++ == 'p' && *c++ == 'l' && *c++ == 'y' && *c++ == '\0')  {
			return false;
		}

		c = &str.str[1];
		if (*c++ == 'o' && *c++ == 'o' && *c++ == 't' && *c++ == '\0') {
			return false;
		}
	}

	return true;
}

static void test_custom_filter()
{
	struct ndb *ndb;
	struct ndb_txn txn;
	struct ndb_config config;
	struct ndb_filter filter, *f = &filter;
	struct ndb_filter filter2, *f2 = &filter2;
	int count, nres = 2;
	uint64_t sub_id, note_keys[2];
	struct ndb_query_result results[2];
	struct ndb_ingest_meta meta;

	const char *root = "[\"EVENT\",{\"id\":\"3d3fba391ce6f83cf336b161f3de90bb2610c20dfb9f4de3a6dacb6b11362971\",\"pubkey\":\"32e1827635450ebb3c5a7d12c1f8e7b2b514439ac10a67eef3d9fd9c5c68e245\",\"created_at\":1744084064,\"kind\":1,\"tags\":[],\"content\":\"dire wolves are back LFG\",\"sig\":\"340a6ee8a859a1d78e50551dae7b24aaba7137647a6ac295acc97faa06ba33310593f5b4081dad188aee81266144f2312afb249939a2e07c14ca167af08e998f\"}]";

	const char *reply_json = "[\"EVENT\",{\"id\":\"3a338522ee1e27056acccee65849de8deba426db1c71cbd61d105280bbb67ed2\",\"pubkey\":\"7cc328a08ddb2afdf9f9be77beff4c83489ff979721827d628a542f32a247c0e\",\"created_at\":1744151551,\"kind\":1,\"tags\":[[\"alt\",\"A short note: pokerdeck ♠️🦍🐧\"],[\"e\",\"086ccb1873fa10d4338713f24b034e17e543d8ad79c15ff39cf59f4d0cb7a2d6\",\"wss://nostr.wine/\",\"root\",\"32e1827635450ebb3c5a7d12c1f8e7b2b514439ac10a67eef3d9fd9c5c68e245\"],[\"p\",\"32e1827635450ebb3c5a7d12c1f8e7b2b514439ac10a67eef3d9fd9c5c68e245\",\"wss://nostr.wine\"]],\"content\":\"pokerdeck ♠️🦍🐧\",\"sig\":\"1099e47ba1ba962a8f2617c139e240d0369d81e70241dce7e73340e3230d8a3039c07114ed21f0e765e40f1b71fc2770fa5585994d27d2ece3b14e7b98f988d3\"}]";

	ndb_default_config(&config);
	assert(ndb_init(&ndb, test_dir, &config));

	ndb_filter_init(f);
	ndb_filter_start_field(f, NDB_FILTER_KINDS);
	ndb_filter_add_int_element(f, 1);
	ndb_filter_end_field(f);

	ndb_filter_start_field(f, NDB_FILTER_CUSTOM);
	ndb_filter_add_custom_filter_element(f, only_threads_filter, NULL);
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	assert(ndb_filter_init(f2));
	assert(ndb_filter_start_field(f2, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(f2, 1));
	ndb_filter_end_field(f2);
	ndb_filter_end(f2);

	sub_id = ndb_subscribe(ndb, f2, 1);

	ndb_ingest_meta_init(&meta, 1, "test");

	assert(ndb_process_event_with(ndb, root, strlen(root), &meta));
	assert(ndb_process_event_with(ndb, reply_json, strlen(reply_json), &meta));

	for (nres = 2; nres > 0;)
		nres -= ndb_wait_for_notes(ndb, sub_id, note_keys, 2);

	ndb_begin_query(ndb, &txn);
	ndb_query(&txn, f, 1, results, 2, &count);
	ndb_end_query(&txn);

	assert(count == 1);
	assert(ndb_note_id(results[0].note)[0] == 0x3d);

	// Cleanup
	ndb_filter_destroy(f);
	ndb_filter_destroy(f2);
	ndb_destroy(ndb);
	delete_test_db();

	printf("ok test_custom_filter\n");
}

void test_replay_attack() {
	struct ndb_filter filter, *f = &filter;
	struct ndb *ndb;
	struct ndb_txn txn;
	struct ndb_config config;
	uint64_t note_key, sub_id;
	int count;
	struct ndb_query_result results[2];
	unsigned char expected[] = { 0x1f, 0x5f, 0x21, 0xb2, 0x2e, 0x4c, 0x87, 0xb1, 0xd7, 0xcf, 0x92, 0x71, 0xa1, 0xc8, 0xae, 0xaf, 0x5b, 0x49, 0x06, 0x1a, 0xf2, 0xc0, 0x3c, 0xab, 0x70, 0x6b, 0xbe, 0x2e, 0xbb, 0x9f, 0xef, 0xd9 };

	// maleated note
	const char *bad_note = "[\"EVENT\",\"blah\",{\"id\":\"3d3fba391ce6f83cf336b161f3de90bb2610c20dfb9f4de3a6dacb6b11362971\",\"pubkey\":\"32e1827635450ebb3c5a7d12c1f8e7b2b514439ac10a67eef3d9fd9c5c68e245\",\"created_at\":1744084064,\"kind\":1,\"tags\":[],\"content\":\"dire wolves are cool\",\"sig\":\"340a6ee8a859a1d78e50551dae7b24aaba7137647a6ac295acc97faa06ba33310593f5b4081dad188aee81266144f2312afb249939a2e07c14ca167af08e998f\"}]";

	const char *ok_note = "[\"EVENT\",\"blah\",{\"id\": \"1f5f21b22e4c87b1d7cf9271a1c8aeaf5b49061af2c03cab706bbe2ebb9fefd9\",\"pubkey\": \"73764df506728297a9c1f359024d2f9c895001f4afda2d0afa844ce7a94778ca\",\"created_at\": 1750785986,\"kind\": 1,\"tags\": [],\"content\": \"ok\",\"sig\": \"bde9ee6933b01c11a9881a3ea4730c6e7f7d952c165fb7ab3f77488c5f73e6d60ce25a32386f2e1d0244c24fd840f25af5d2d04dc6d229ec0f67c7782e8879d9\"}]";

	delete_test_db();
	ndb_default_config(&config);
	assert(ndb_init(&ndb, test_dir, &config));

	ndb_filter_init(f);
	assert(ndb_filter_start_field(f, NDB_FILTER_KINDS));
	assert(ndb_filter_add_int_element(f, 1));
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	sub_id = ndb_subscribe(ndb, f, 1);

	ndb_process_event(ndb, bad_note, strlen(bad_note));
	ndb_process_event(ndb, ok_note, strlen(bad_note));

	assert(ndb_wait_for_notes(ndb, sub_id, &note_key, 1) == 1);

	ndb_begin_query(ndb, &txn);
	ndb_query(&txn, f, 1, results, 2, &count);
	ndb_end_query(&txn);

	assert(count == 1);
	assert(memcmp(expected, ndb_note_id(results[0].note), 32) == 0);

	ndb_filter_destroy(f);
	ndb_destroy(ndb);
	delete_test_db();
}

// NIP-23 markdown parsing tests
static void test_markdown_parsing() {
	unsigned char buf[8192];
	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_block_iterator iterator, *iter = &iterator;
	int block_count = 0;

	// Simple markdown with heading and paragraph
	const char *content = "# Hello World\n\nThis is a paragraph.";

	assert(ndb_parse_markdown_content(buf, sizeof(buf), content, strlen(content), &blocks));
	assert(blocks->num_blocks > 0);

	ndb_blocks_iterate_start(content, blocks, iter);

	while ((block = ndb_blocks_iterate_next(iter))) {
		block_count++;
		enum ndb_block_type type = ndb_get_block_type(block);

		// First block should be heading
		if (block_count == 1) {
			assert(type == BLOCK_HEADING);
			struct ndb_heading_block *heading = ndb_block_heading(block);
			assert(heading != NULL);
			assert(heading->level == 1);
		}
	}

	assert(block_count > 0);
}

static void test_markdown_code_block() {
	unsigned char buf[8192];
	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_block_iterator iterator, *iter = &iterator;
	int found_code_block = 0;

	const char *content = "```c\nint main() { return 0; }\n```";

	assert(ndb_parse_markdown_content(buf, sizeof(buf), content, strlen(content), &blocks));

	ndb_blocks_iterate_start(content, blocks, iter);

	while ((block = ndb_blocks_iterate_next(iter))) {
		if (ndb_get_block_type(block) == BLOCK_CODE_BLOCK) {
			struct ndb_code_block *code = ndb_block_code(block);
			assert(code != NULL);
			// The info string should contain "c"
			assert(code->info.len >= 1);
			assert(strncmp(code->info.str, "c", 1) == 0);
			found_code_block = 1;
		}
	}

	assert(found_code_block);
}

static void test_markdown_links() {
	unsigned char buf[8192];
	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_block_iterator iterator, *iter = &iterator;
	int found_link = 0;
	int found_nostr_mention = 0;

	// Test regular link
	const char *content = "[example](https://example.com)";

	assert(ndb_parse_markdown_content(buf, sizeof(buf), content, strlen(content), &blocks));

	ndb_blocks_iterate_start(content, blocks, iter);

	while ((block = ndb_blocks_iterate_next(iter))) {
		if (ndb_get_block_type(block) == BLOCK_LINK) {
			struct ndb_link_block *link = ndb_block_link(block);
			assert(link != NULL);
			assert(strncmp(link->url.str, "https://example.com", link->url.len) == 0);
			found_link = 1;
		}
	}

	assert(found_link);

	// Test nostr: link (should become BLOCK_MENTION_BECH32)
	const char *nostr_content = "[profile](nostr:npub1xtscya34g58tk0z605fvr788k263gsu6cy9x0mhnm87echrgufzsevkk5s)";

	assert(ndb_parse_markdown_content(buf, sizeof(buf), nostr_content, strlen(nostr_content), &blocks));

	ndb_blocks_iterate_start(nostr_content, blocks, iter);

	while ((block = ndb_blocks_iterate_next(iter))) {
		if (ndb_get_block_type(block) == BLOCK_MENTION_BECH32) {
			found_nostr_mention = 1;
		}
	}

	assert(found_nostr_mention);
}

static void test_markdown_lists() {
	unsigned char buf[8192];
	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_block_iterator iterator, *iter = &iterator;
	int found_list = 0;
	int found_list_item = 0;

	const char *content = "- Item 1\n- Item 2\n- Item 3";

	assert(ndb_parse_markdown_content(buf, sizeof(buf), content, strlen(content), &blocks));

	ndb_blocks_iterate_start(content, blocks, iter);

	while ((block = ndb_blocks_iterate_next(iter))) {
		enum ndb_block_type type = ndb_get_block_type(block);
		if (type == BLOCK_LIST) {
			struct ndb_list_block *list = ndb_block_list(block);
			assert(list != NULL);
			assert(list->list_type == NDB_LIST_BULLET);
			found_list = 1;
		}
		if (type == BLOCK_LIST_ITEM) {
			found_list_item = 1;
		}
	}

	assert(found_list);
	assert(found_list_item);
}

static void test_markdown_block_count() {
	unsigned char buf[8192];
	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_block_iterator iterator, *iter = &iterator;
	int block_count = 0;

	// Simple heading + text: should be HEADING, TEXT (not duplicated)
	const char *content = "# Title";

	assert(ndb_parse_markdown_content(buf, sizeof(buf), content, strlen(content), &blocks));

	ndb_blocks_iterate_start(content, blocks, iter);

	while ((block = ndb_blocks_iterate_next(iter))) {
		block_count++;
	}

	// Should be exactly 2 blocks: HEADING and TEXT
	assert(block_count == 2);
}

static void test_markdown_bech32_block() {
	unsigned char buf[8192];
	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_block_iterator iterator, *iter = &iterator;
	int found_mention = 0;

	const char *content = "[profile](nostr:npub1xtscya34g58tk0z605fvr788k263gsu6cy9x0mhnm87echrgufzsevkk5s)";

	assert(ndb_parse_markdown_content(buf, sizeof(buf), content, strlen(content), &blocks));

	ndb_blocks_iterate_start(content, blocks, iter);

	while ((block = ndb_blocks_iterate_next(iter))) {
		if (ndb_get_block_type(block) == BLOCK_MENTION_BECH32) {
			// For v2 (markdown), ndb_block_str should return the raw bech32 string
			struct ndb_str_block *str = ndb_block_str(block);
			assert(str != NULL);
			assert(str->len > 0);
			assert(strncmp(str->str, "npub1", 5) == 0);

			// For v2, ndb_bech32_block should return NULL (not decoded)
			struct nostr_bech32 *bech32 = ndb_bech32_block(block);
			assert(bech32 == NULL);

			found_mention = 1;
		}
	}

	assert(found_mention);
}

static void test_markdown_image_alt() {
	unsigned char buf[8192];
	struct ndb_blocks *blocks;
	struct ndb_block *block;
	struct ndb_block_iterator iterator, *iter = &iterator;
	int found_image = 0;

	const char *content = "![this is alt text](https://example.com/image.png)";

	assert(ndb_parse_markdown_content(buf, sizeof(buf), content, strlen(content), &blocks));

	ndb_blocks_iterate_start(content, blocks, iter);

	while ((block = ndb_blocks_iterate_next(iter))) {
		if (ndb_get_block_type(block) == BLOCK_IMAGE) {
			struct ndb_image_block *image = ndb_block_image(block);
			assert(image != NULL);

			// Check URL
			assert(strncmp(image->url.str, "https://example.com/image.png", image->url.len) == 0);

			// Check alt text is captured (not empty)
			assert(image->alt.len > 0);
			assert(strncmp(image->alt.str, "this is alt text", image->alt.len) == 0);

			found_image = 1;
		}
	}

	assert(found_image);
}

int main(int argc, const char *argv[]) {
	delete_test_db();

	test_giftwrap_reprocess();
	test_giftwrap_unwrap();
	test_nip44_round_trip();
	test_nip44_test_vector();
	test_nip44_decrypt();
	test_replay_attack();
	test_custom_filter();
	test_metadata();
	test_count_metadata();
	test_reaction_encoding();
	test_reaction_counter();
	test_note_relay_index();
	test_filter_search();
	test_filter_parse_search_json();
	test_parse_filter_json();
	test_filter_eq();
	test_filter_is_subset();
	test_filter_json();
	test_bech32_parsing();
	test_single_url_parsing();
	test_url_parsing();
	test_query();
	test_tag_query();
	test_weird_note_corruption();
	test_parse_content();
	test_subscriptions();
	test_comma_url_parsing();
	test_varints();
	test_bech32_objects();
	//test_block_coding();
	test_encode_decode_invoice();
	test_filters();
	//test_migrate();
	test_fetched_at();
	test_profile_updates();
	test_load_profiles();
	test_nip50_profile_search();
	test_basic_event();
	test_empty_tags();
	test_parse_json();
	test_parse_contact_list();
	test_strings_work_before_finalization();
	test_tce();
	test_tce_command_result();
	test_tce_eose();
	test_tce_command_result_empty_msg();
	test_content_len();
	test_fuzz_events();

	// note fetching
	test_fetch_last_noteid();

	test_timeline_query();

	// fulltext
	test_fulltext();

	// protected queue tests
	test_queue_init_pop_push();
	test_queue_thread_safety();
	test_queue_boundary_conditions();

	// memchr stuff
	test_fast_strchr();

	// profiles
	test_replacement();

	// NIP-23 markdown parsing
	test_markdown_parsing();
	test_markdown_code_block();
	test_markdown_links();
	test_markdown_lists();
	test_markdown_block_count();
	test_markdown_bech32_block();
	test_markdown_image_alt();

	printf("All tests passed!\n");       // Print this if all tests pass.
}



