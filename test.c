
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

#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

int ndb_print_kind_keys(struct ndb_txn *txn);
static const char *test_dir = "./testdata/db";

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
	ndb_filter_end(f);

	// should be sorted after end
	assert(current->elements[0] == 2);
	assert(current->elements[1] == 1337);

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

	test_invoice_encoding(deschash);
	test_invoice_encoding(desc);
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
}

static void test_reaction_counter()
{
	static const int alloc_size = 1024 * 1024;
	char *json = malloc(alloc_size);
	struct ndb *ndb;
	size_t len;
	void *root;
	int written, reactions, results;
	NdbEventMeta_table_t meta;
	struct ndb_txn txn;
	struct ndb_config config;
	ndb_default_config(&config);
	static const int num_reactions = 3;
	uint64_t note_ids[num_reactions], subid;

	assert(ndb_init(&ndb, test_dir, &config));

	read_file("testdata/reactions.json", (unsigned char*)json, alloc_size, &written);

	assert((subid = ndb_subscribe(ndb, NULL, 0)));

	assert(ndb_process_client_events(ndb, json, written));

	for (reactions = 0; reactions < num_reactions;) {
		results = ndb_wait_for_notes(ndb, subid, note_ids, num_reactions);
		reactions += results;
		fprintf(stderr, "got %d notes, total %d\n", results, reactions);
		assert(reactions > 0);
	}

	assert(ndb_begin_query(ndb, &txn));

	const unsigned char id[32] = {
	  0x1a, 0x41, 0x56, 0x30, 0x31, 0x09, 0xbb, 0x4a, 0x66, 0x0a, 0x6a, 0x90,
	  0x04, 0xb0, 0xcd, 0xce, 0x8d, 0x83, 0xc3, 0x99, 0x1d, 0xe7, 0x86, 0x4f,
	  0x18, 0x76, 0xeb, 0x0f, 0x62, 0x2c, 0x68, 0xe8
	};

	assert((root = ndb_get_note_meta(&txn, id, &len)));
	assert(0 == NdbEventMeta_verify_as_root(root, len));
	assert((meta = NdbEventMeta_as_root(root)));

	reactions = NdbEventMeta_reactions_get(meta);
	//printf("counted reactions: %d\n", reactions);
	assert(reactions == 2);
	ndb_end_query(&txn);
	ndb_destroy(ndb);
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

	ndb_destroy(ndb);
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
	ndb_default_config(&config);
	ndb_config_set_flags(&config, NDB_FLAG_NOMIGRATE);

	fprintf(stderr, "testing migrate on v0\n");
	assert(ndb_init(&ndb, v0_dir, &config));
	assert(ndb_db_version(ndb) == 0);
	ndb_destroy(ndb);

	ndb_config_set_flags(&config, 0);

	assert(ndb_init(&ndb, v0_dir, &config));
	ndb_destroy(ndb);
	assert(ndb_init(&ndb, v0_dir, &config));
	assert(ndb_db_version(ndb) == 3);

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
	printf("ndb_note_from_json size %d\n", size);
	assert(size > 0);
	assert(size == 34328);

	memcpy(id, ndb_note_id(note), 32);
	memset(ndb_note_id(note), 0, 32);
	assert(ndb_calculate_id(note, json, alloc_size));
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
	printf("ndb_content_len %d, expected_len %ld\n",
			ndb_note_content_length(note),
			strlen(expected_content));
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
	printf("wrote test_contacts_ndb_note (raw ndb_note)\n");

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
	printf("NdbProfileRecord verify result %d\n", res);
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

	printf("note_key %" PRIu64 "\n", key);

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
//	assert(prot_queue_push(&q, &data) == 0);  // Should fail as queue is full

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
//    assert(prot_queue_push(&q, &data) == 0);
    
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
	ndb_destroy(ndb);
}

static void test_query()
{
	struct ndb *ndb;
	struct ndb_txn txn;
	struct ndb_filter filters[2], *f;
	struct ndb_config config;
	struct ndb_query_result results[4];
	int count, cap;
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
	assert(ndb_process_event(ndb, ev3, strlen(ev3)));
	assert(ndb_process_event(ndb, ev4, strlen(ev4)));

	for (count = 0; count < 2;)
		count += ndb_wait_for_notes(ndb, subid, note_ids+count, 4-count);

	ndb_begin_query(ndb, &txn);
	assert(ndb_query(&txn, f, 1, results, cap, &count));
	assert(count == 2);
	assert(0 == memcmp(ndb_note_id(results[0].note), id2, 32));

	ndb_filter_destroy(f);
	ndb_filter_init(f);
	ndb_filter_start_field(f, NDB_FILTER_KINDS);
	ndb_filter_add_int_element(f, 2);
	ndb_filter_end_field(f);
	ndb_filter_start_field(f, NDB_FILTER_LIMIT);
	ndb_filter_add_int_element(f, 2);
	ndb_filter_end_field(f);
	ndb_filter_end(f);

	count = 0;
	assert(ndb_query(&txn, f, 1, results, cap, &count));
	ndb_print_kind_keys(&txn);
	printf("count %d\n", count);
	assert(count == 2);
	assert(!strcmp(ndb_note_content(results[0].note), "hmm"));
	assert(!strcmp(ndb_note_content(results[1].note), "what"));

	ndb_end_query(&txn);
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
	ndb_destroy(ndb);
}

int main(int argc, const char *argv[]) {
	// GCC's warn_unused_result is fascist bullshit.
	if (system("rm -rf testdata/db/*.mdb"));
	test_parse_filter_json();
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
	test_reaction_counter();
	test_load_profiles();
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

	printf("All tests passed!\n");       // Print this if all tests pass.
}



