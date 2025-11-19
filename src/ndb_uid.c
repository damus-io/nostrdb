#include "ndb_uid.h"
#include "nostrdb.h"
#include "lmdb.h"
#include <string.h>
#include <stdio.h>

int ndb_uid_map_init(struct ndb_uid_map *map, void *env)
{
	MDB_txn *txn;
	int rc;
	MDB_env *mdb_env = (MDB_env*)env;

	map->env = env;
	map->next_id = 0;

	// Create write transaction to create databases
	if ((rc = mdb_txn_begin(mdb_env, NULL, 0, &txn))) {
		fprintf(stderr, "ndb_uid_map_init: mdb_txn_begin failed: %s\n",
			mdb_strerror(rc));
		return 0;
	}

	// Create str_to_id database
	MDB_dbi str_to_id_dbi;
	if ((rc = mdb_dbi_open(txn, "uid_str_to_id", MDB_CREATE, &str_to_id_dbi))) {
		fprintf(stderr, "ndb_uid_map_init: failed to open str_to_id db: %s\n",
			mdb_strerror(rc));
		mdb_txn_abort(txn);
		return 0;
	}

	// Create id_to_str database
	MDB_dbi id_to_str_dbi;
	if ((rc = mdb_dbi_open(txn, "uid_id_to_str", MDB_CREATE, &id_to_str_dbi))) {
		fprintf(stderr, "ndb_uid_map_init: failed to open id_to_str db: %s\n",
			mdb_strerror(rc));
		mdb_txn_abort(txn);
		return 0;
	}

	// Store DBIs
	map->str_to_id_db = (void*)(intptr_t)str_to_id_dbi;
	map->id_to_str_db = (void*)(intptr_t)id_to_str_dbi;

	// Find the highest UID to initialize next_id
	MDB_cursor *cursor;
	if ((rc = mdb_cursor_open(txn, id_to_str_dbi, &cursor)) == 0) {
		MDB_val key, val;
		// Position at last entry
		if (mdb_cursor_get(cursor, &key, &val, MDB_LAST) == 0) {
			if (key.mv_size == sizeof(uint32_t)) {
				uint32_t max_id;
				memcpy(&max_id, key.mv_data, sizeof(uint32_t));
				map->next_id = max_id + 1;
			}
		}
		mdb_cursor_close(cursor);
	}

	if ((rc = mdb_txn_commit(txn))) {
		fprintf(stderr, "ndb_uid_map_init: mdb_txn_commit failed: %s\n",
			mdb_strerror(rc));
		return 0;
	}

	return 1;
}

int ndb_uid_get(void *txn, struct ndb_uid_map *map,
                const unsigned char *pubkey, ndb_uid_t *uid_out)
{
	MDB_val key, val;
	MDB_dbi dbi = (MDB_dbi)(intptr_t)map->str_to_id_db;
	MDB_txn *mdb_txn = (MDB_txn*)txn;

	key.mv_data = (void*)pubkey;
	key.mv_size = 32;

	if (mdb_get(mdb_txn, dbi, &key, &val) != 0)
		return 0;

	if (val.mv_size != sizeof(uint32_t))
		return 0;

	memcpy(uid_out, val.mv_data, sizeof(uint32_t));
	return 1;
}

int ndb_uid_exists_map(void *txn, struct ndb_uid_map *map,
                       const unsigned char *pubkey)
{
	ndb_uid_t uid;
	return ndb_uid_get(txn, map, pubkey, &uid);
}

int ndb_uid_get_or_create(void *txn, struct ndb_uid_map *map,
                          const unsigned char *pubkey, ndb_uid_t *uid_out)
{
	MDB_val key, val;
	MDB_dbi str_to_id_dbi = (MDB_dbi)(intptr_t)map->str_to_id_db;
	MDB_dbi id_to_str_dbi = (MDB_dbi)(intptr_t)map->id_to_str_db;
	MDB_txn *mdb_txn = (MDB_txn*)txn;
	int rc;

	// Check if already exists
	key.mv_data = (void*)pubkey;
	key.mv_size = 32;

	if ((rc = mdb_get(mdb_txn, str_to_id_dbi, &key, &val)) == 0) {
		if (val.mv_size == sizeof(uint32_t)) {
			memcpy(uid_out, val.mv_data, sizeof(uint32_t));
			return 1;
		}
	}

	// Doesn't exist, create new UID
	uint32_t new_uid = map->next_id++;

	// Insert into str_to_id
	val.mv_data = &new_uid;
	val.mv_size = sizeof(uint32_t);
	if ((rc = mdb_put(mdb_txn, str_to_id_dbi, &key, &val, 0))) {
		fprintf(stderr, "ndb_uid_get_or_create: failed to insert str_to_id: %s\n",
			mdb_strerror(rc));
		return 0;
	}

	// Insert into id_to_str
	key.mv_data = &new_uid;
	key.mv_size = sizeof(uint32_t);
	val.mv_data = (void*)pubkey;
	val.mv_size = 32;
	if ((rc = mdb_put(mdb_txn, id_to_str_dbi, &key, &val, 0))) {
		fprintf(stderr, "ndb_uid_get_or_create: failed to insert id_to_str: %s\n",
			mdb_strerror(rc));
		return 0;
	}

	*uid_out = new_uid;
	return 1;
}

int ndb_uid_to_pubkey(void *txn, struct ndb_uid_map *map,
                      ndb_uid_t uid, unsigned char *pubkey_out)
{
	MDB_val key, val;
	MDB_dbi dbi = (MDB_dbi)(intptr_t)map->id_to_str_db;
	MDB_txn *mdb_txn = (MDB_txn*)txn;

	key.mv_data = &uid;
	key.mv_size = sizeof(uint32_t);

	if (mdb_get(mdb_txn, dbi, &key, &val) != 0)
		return 0;

	if (val.mv_size != 32)
		return 0;

	memcpy(pubkey_out, val.mv_data, 32);
	return 1;
}

void ndb_uid_map_destroy(struct ndb_uid_map *map)
{
	// DBIs don't need explicit closing in LMDB, just clear the map
	map->env = NULL;
	map->str_to_id_db = NULL;
	map->id_to_str_db = NULL;
	map->next_id = 0;
}
