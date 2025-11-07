#include "nostrdb.h"
#include "hex.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_DB_DIR "./testdata/sg_test_db"

static void delete_test_db() {
	unlink(TEST_DB_DIR "/data.mdb");
	unlink(TEST_DB_DIR "/lock.mdb");
}

static void test_socialgraph_basic() {
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_txn txn;

	ndb_default_config(&config);
	config.flags |= NDB_FLAG_SKIP_NOTE_VERIFY;
	delete_test_db();
	mkdir(TEST_DB_DIR, 0755);

	assert(ndb_init(&ndb, TEST_DB_DIR, &config));

	// Create some test pubkeys
	unsigned char alice_pk[32], bob_pk[32], charlie_pk[32];
	memset(alice_pk, 0xAA, 32);
	memset(bob_pk, 0xBB, 32);
	memset(charlie_pk, 0xCC, 32);

	// Build a contact list where Alice follows Bob and Charlie
	// kind 3 event
	const char *contact_list_json =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000001\","
		" \"pubkey\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		" \"created_at\":1234567890,"
		" \"kind\":3,"
		" \"tags\":["
		"  [\"p\",\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"],"
		"  [\"p\",\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"]"
		" ],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	// Process the event
	assert(ndb_process_event(ndb, contact_list_json, strlen(contact_list_json)));

	// Give it more time to process through the ingester pipeline
	usleep(500000); // 500ms

	// Query the social graph
	assert(ndb_begin_query(ndb, &txn));

	// Check if Alice follows Bob
	int follows = ndb_socialgraph_is_following(&txn, ndb, alice_pk, bob_pk);
	assert(follows == 1);

	// Check if Alice follows Charlie
	follows = ndb_socialgraph_is_following(&txn, ndb, alice_pk, charlie_pk);
	assert(follows == 1);

	// Check if Bob follows Alice (should be false)
	follows = ndb_socialgraph_is_following(&txn, ndb, bob_pk, alice_pk);
	assert(follows == 0);

	// Check follower count
	int count = ndb_socialgraph_follower_count(&txn, ndb, bob_pk);
	assert(count == 1); // Bob has 1 follower (Alice)

	ndb_end_query(&txn);

	ndb_destroy(ndb);
	printf("✓ test_socialgraph_basic passed\n");
}

static void test_socialgraph_follow_distance() {
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_txn txn;

	ndb_default_config(&config);
	config.flags |= NDB_FLAG_SKIP_NOTE_VERIFY;
	delete_test_db();
	mkdir(TEST_DB_DIR, 0755);

	assert(ndb_init(&ndb, TEST_DB_DIR, &config));

	// Setup: Root (00..) follows Alice (AA..), Alice follows Bob (BB..)
	// Expected distances: Root=0, Alice=1, Bob=2

	unsigned char root_pk[32], alice_pk[32], bob_pk[32];
	memset(root_pk, 0x00, 32);
	memset(alice_pk, 0xAA, 32);
	memset(bob_pk, 0xBB, 32);

	// Root follows Alice
	const char *root_contact_list =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000001\","
		" \"pubkey\":\"0000000000000000000000000000000000000000000000000000000000000000\","
		" \"created_at\":1234567890,"
		" \"kind\":3,"
		" \"tags\":[[\"p\",\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"]],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	// Alice follows Bob
	const char *alice_contact_list =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000002\","
		" \"pubkey\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		" \"created_at\":1234567891,"
		" \"kind\":3,"
		" \"tags\":[[\"p\",\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"]],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	assert(ndb_process_event(ndb, root_contact_list, strlen(root_contact_list)));
	assert(ndb_process_event(ndb, alice_contact_list, strlen(alice_contact_list)));

	usleep(200000); // 200ms for processing

	assert(ndb_begin_query(ndb, &txn));

	// Check distances
	uint32_t distance = ndb_socialgraph_get_follow_distance(&txn, ndb, root_pk);
	assert(distance == 0); // Root is distance 0

	distance = ndb_socialgraph_get_follow_distance(&txn, ndb, alice_pk);
	assert(distance == 1); // Alice followed by root

	distance = ndb_socialgraph_get_follow_distance(&txn, ndb, bob_pk);
	assert(distance == 2); // Bob followed by Alice (distance 1)

	ndb_end_query(&txn);

	ndb_destroy(ndb);
	printf("✓ test_socialgraph_follow_distance passed\n");
}

static void test_socialgraph_mute_list() {
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_txn txn;

	ndb_default_config(&config);
	config.flags |= NDB_FLAG_SKIP_NOTE_VERIFY;
	delete_test_db();
	mkdir(TEST_DB_DIR, 0755);

	assert(ndb_init(&ndb, TEST_DB_DIR, &config));

	// Create test pubkeys
	unsigned char alice_pk[32], bob_pk[32], charlie_pk[32];
	memset(alice_pk, 0xAA, 32);
	memset(bob_pk, 0xBB, 32);
	memset(charlie_pk, 0xCC, 32);

	// Build a mute list where Alice mutes Bob and Charlie
	// kind 10000 event
	const char *mute_list_json =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000001\","
		" \"pubkey\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		" \"created_at\":1234567890,"
		" \"kind\":10000,"
		" \"tags\":["
		"  [\"p\",\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"],"
		"  [\"p\",\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"]"
		" ],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	// Process the event
	assert(ndb_process_event(ndb, mute_list_json, strlen(mute_list_json)));

	// Give it time to process
	usleep(500000); // 500ms

	// Query the social graph
	assert(ndb_begin_query(ndb, &txn));

	// Check if Alice mutes Bob
	int mutes = ndb_socialgraph_is_muting(&txn, ndb, alice_pk, bob_pk);
	assert(mutes == 1);

	// Check if Alice mutes Charlie
	mutes = ndb_socialgraph_is_muting(&txn, ndb, alice_pk, charlie_pk);
	assert(mutes == 1);

	// Check if Bob mutes Alice (should be false)
	mutes = ndb_socialgraph_is_muting(&txn, ndb, bob_pk, alice_pk);
	assert(mutes == 0);

	// Get muted list
	unsigned char muted_out[64];
	int count = ndb_socialgraph_get_muted(&txn, ndb, alice_pk, muted_out, 2);
	assert(count == 2); // Alice mutes 2 users

	// Get muters of Bob
	unsigned char muters_out[32];
	count = ndb_socialgraph_get_muters(&txn, ndb, bob_pk, muters_out, 1);
	assert(count == 1); // Bob is muted by 1 user (Alice)
	assert(memcmp(muters_out, alice_pk, 32) == 0);

	ndb_end_query(&txn);

	ndb_destroy(ndb);
	printf("✓ test_socialgraph_mute_list passed\n");
}

static void test_socialgraph_mute_update() {
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_txn txn;

	ndb_default_config(&config);
	config.flags |= NDB_FLAG_SKIP_NOTE_VERIFY;
	delete_test_db();
	mkdir(TEST_DB_DIR, 0755);

	assert(ndb_init(&ndb, TEST_DB_DIR, &config));

	unsigned char alice_pk[32], bob_pk[32], charlie_pk[32];
	memset(alice_pk, 0xAA, 32);
	memset(bob_pk, 0xBB, 32);
	memset(charlie_pk, 0xCC, 32);

	// Alice initially mutes Bob and Charlie
	const char *mute_list_v1 =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000001\","
		" \"pubkey\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		" \"created_at\":1234567890,"
		" \"kind\":10000,"
		" \"tags\":["
		"  [\"p\",\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"],"
		"  [\"p\",\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"]"
		" ],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	assert(ndb_process_event(ndb, mute_list_v1, strlen(mute_list_v1)));
	usleep(200000);

	// Alice updates to only mute Charlie (unmutes Bob)
	const char *mute_list_v2 =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000002\","
		" \"pubkey\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		" \"created_at\":1234567900,"
		" \"kind\":10000,"
		" \"tags\":["
		"  [\"p\",\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"]"
		" ],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	assert(ndb_process_event(ndb, mute_list_v2, strlen(mute_list_v2)));
	usleep(200000);

	assert(ndb_begin_query(ndb, &txn));

	// Alice should no longer mute Bob
	int mutes = ndb_socialgraph_is_muting(&txn, ndb, alice_pk, bob_pk);
	assert(mutes == 0);

	// Alice should still mute Charlie
	mutes = ndb_socialgraph_is_muting(&txn, ndb, alice_pk, charlie_pk);
	assert(mutes == 1);

	// Bob should have 0 muters now
	unsigned char muters_out[32];
	int count = ndb_socialgraph_get_muters(&txn, ndb, bob_pk, muters_out, 1);
	assert(count == 0);

	// Charlie should have 1 muter (Alice)
	count = ndb_socialgraph_get_muters(&txn, ndb, charlie_pk, muters_out, 1);
	assert(count == 1);
	assert(memcmp(muters_out, alice_pk, 32) == 0);

	ndb_end_query(&txn);

	ndb_destroy(ndb);
	printf("✓ test_socialgraph_mute_update passed\n");
}

static void test_socialgraph_set_root() {
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_txn txn;

	ndb_default_config(&config);
	config.flags |= NDB_FLAG_SKIP_NOTE_VERIFY;
	delete_test_db();
	mkdir(TEST_DB_DIR, 0755);

	assert(ndb_init(&ndb, TEST_DB_DIR, &config));

	// Create pubkeys: root (00), alice (AA), bob (BB), charlie (CC)
	unsigned char root_pk[32], alice_pk[32], bob_pk[32], charlie_pk[32];
	memset(root_pk, 0x00, 32);
	memset(alice_pk, 0xAA, 32);
	memset(bob_pk, 0xBB, 32);
	memset(charlie_pk, 0xCC, 32);

	// Root follows Alice
	const char *root_contact_list =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000001\","
		" \"pubkey\":\"0000000000000000000000000000000000000000000000000000000000000000\","
		" \"created_at\":1234567890,"
		" \"kind\":3,"
		" \"tags\":[[\"p\",\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"]],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	// Alice follows Bob
	const char *alice_contact_list =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000002\","
		" \"pubkey\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		" \"created_at\":1234567891,"
		" \"kind\":3,"
		" \"tags\":[[\"p\",\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"]],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	assert(ndb_process_event(ndb, root_contact_list, strlen(root_contact_list)));
	assert(ndb_process_event(ndb, alice_contact_list, strlen(alice_contact_list)));
	usleep(200000);

	// Initial state: root=0, alice=1, bob=2, charlie=1000
	assert(ndb_begin_query(ndb, &txn));
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, root_pk) == 0);
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, alice_pk) == 1);
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, bob_pk) == 2);
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, charlie_pk) == 1000);
	ndb_end_query(&txn);

	// Change root to Alice
	ndb_socialgraph_set_root(ndb, alice_pk);
	usleep(100000); // Wait for writer thread

	// After set_root: alice=0, bob=1, root=1000, charlie=1000
	assert(ndb_begin_query(ndb, &txn));
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, alice_pk) == 0);
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, bob_pk) == 1);
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, root_pk) == 1000);
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, charlie_pk) == 1000);
	ndb_end_query(&txn);

	// Change root back to original
	ndb_socialgraph_set_root(ndb, root_pk);
	usleep(100000); // Wait for writer thread

	// Back to initial: root=0, alice=1, bob=2, charlie=1000
	assert(ndb_begin_query(ndb, &txn));
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, root_pk) == 0);
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, alice_pk) == 1);
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, bob_pk) == 2);
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, charlie_pk) == 1000);
	ndb_end_query(&txn);

	ndb_destroy(ndb);
	printf("✓ test_socialgraph_set_root passed\n");
}

static void test_socialgraph_root_persistence() {
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_txn txn;

	ndb_default_config(&config);
	config.flags |= NDB_FLAG_SKIP_NOTE_VERIFY;
	delete_test_db();
	mkdir(TEST_DB_DIR, 0755);

	// First init with zero pubkey default
	assert(ndb_init(&ndb, TEST_DB_DIR, &config));

	unsigned char alice_pk[32], bob_pk[32];
	memset(alice_pk, 0xAA, 32);
	memset(bob_pk, 0xBB, 32);

	// Alice follows Bob
	const char *alice_contact_list =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000001\","
		" \"pubkey\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		" \"created_at\":1234567890,"
		" \"kind\":3,"
		" \"tags\":[[\"p\",\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"]],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	assert(ndb_process_event(ndb, alice_contact_list, strlen(alice_contact_list)));
	usleep(200000);

	// Set root to Alice
	ndb_socialgraph_set_root(ndb, alice_pk);
	usleep(100000);

	// Verify Alice is root
	assert(ndb_begin_query(ndb, &txn));
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, alice_pk) == 0);
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, bob_pk) == 1);
	ndb_end_query(&txn);

	ndb_destroy(ndb);

	// Restart ndb - root should persist
	assert(ndb_init(&ndb, TEST_DB_DIR, &config));

	// Verify Alice is still root without calling set_root
	assert(ndb_begin_query(ndb, &txn));
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, alice_pk) == 0);
	assert(ndb_socialgraph_get_follow_distance(&txn, ndb, bob_pk) == 1);
	ndb_end_query(&txn);

	ndb_destroy(ndb);
	printf("✓ test_socialgraph_root_persistence passed\n");
}

int main() {
	test_socialgraph_basic();
	test_socialgraph_follow_distance();
	test_socialgraph_mute_list();
	test_socialgraph_mute_update();
	test_socialgraph_set_root();
	test_socialgraph_root_persistence();

	printf("\nAll social graph tests passed!\n");
	return 0;
}
