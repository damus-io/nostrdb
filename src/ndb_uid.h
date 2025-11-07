#ifndef NDB_UID_H
#define NDB_UID_H

#include <inttypes.h>
#include <stddef.h>

typedef uint32_t ndb_uid_t;

struct ndb_uid_map {
	void *env;           // MDB_env* - LMDB environment
	void *str_to_id_db;  // MDB_dbi - maps 32-byte pubkey -> UID
	void *id_to_str_db;  // MDB_dbi - maps UID -> 32-byte pubkey
	uint32_t next_id;
};

/**
 * Initialize UID mapping subsystem
 * @param map The UID map structure to initialize
 * @param env Pointer to MDB_env
 * @return 1 on success, 0 on failure
 */
int ndb_uid_map_init(struct ndb_uid_map *map, void *env);

/**
 * Get existing UID for a pubkey, or create one if it doesn't exist
 * Must be called within a transaction
 * @param txn Active transaction
 * @param map UID map
 * @param pubkey 32-byte binary pubkey
 * @param uid_out Output parameter for the UID
 * @return 1 on success, 0 on failure
 */
int ndb_uid_get_or_create(void *txn, struct ndb_uid_map *map,
                          const unsigned char *pubkey, ndb_uid_t *uid_out);

/**
 * Get existing UID for a pubkey (read-only)
 * @param txn Active read transaction
 * @param map UID map
 * @param pubkey 32-byte binary pubkey
 * @param uid_out Output parameter for the UID
 * @return 1 if found, 0 if not found
 */
int ndb_uid_get(void *txn, struct ndb_uid_map *map,
                const unsigned char *pubkey, ndb_uid_t *uid_out);

/**
 * Get pubkey for a UID
 * @param txn Active transaction
 * @param map UID map
 * @param uid The UID to look up
 * @param pubkey_out Output buffer (must be 32 bytes)
 * @return 1 if found, 0 if not found
 */
int ndb_uid_to_pubkey(void *txn, struct ndb_uid_map *map,
                      ndb_uid_t uid, unsigned char *pubkey_out);

/**
 * Destroy UID map (closes databases)
 */
void ndb_uid_map_destroy(struct ndb_uid_map *map);

#endif // NDB_UID_H
