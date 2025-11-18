#ifndef NDB_SOCIALGRAPH_H
#define NDB_SOCIALGRAPH_H

#include "ndb_uid.h"
#include <inttypes.h>

struct ndb_lmdb;
struct ndb_txn;
struct ndb_note;

struct ndb_socialgraph {
	void *env; // MDB_env*
	struct ndb_uid_map uid_map;

	// LMDB databases (MDB_dbi stored as void*)
	void *follow_distance_db;      // UID -> u32 distance from root
	void *users_by_follow_distance_db; // (u32 distance, UID) -> empty (composite key index)
	void *followed_by_user_db;     // UID -> bucketed_list of UIDs they follow
	void *followers_by_user_db;    // (followed_uid, follower_uid) -> empty (composite key index)
	void *follower_count_db;       // UID -> u32 follower count
	void *follow_list_created_at_db; // UID -> u64 timestamp
	void *muted_by_user_db;        // UID -> bucketed_list of UIDs they mute
	void *user_muted_by_db;        // (muted_uid, muter_uid) -> empty (composite key index)
	void *muter_count_db;          // UID -> u32 muter count
	void *mute_list_created_at_db; // UID -> u64 timestamp

	ndb_uid_t root_uid;
};

/**
 * Initialize social graph subsystem
 * @param graph The social graph structure to initialize
 * @param env Pointer to MDB_env
 * @param root_pubkey 32-byte root user pubkey (for distance calculation)
 * @return 1 on success, 0 on failure
 */
int ndb_socialgraph_init(struct ndb_socialgraph *graph, void *env,
                         const unsigned char *root_pubkey);

/**
 * Handle a contact list event (kind 3)
 * Updates follow graph based on p-tags
 * Must be called within a write transaction
 * @param txn Active write transaction
 * @param graph Social graph
 * @param note The contact list note
 * @return 1 on success, 0 on failure
 */
int ndb_socialgraph_handle_contact_list(void *txn, struct ndb_socialgraph *graph,
                                        struct ndb_note *note);

/**
 * Handle a mute list event (kind 10000)
 * Updates mute graph based on p-tags
 * Must be called within a write transaction
 * @param txn Active write transaction
 * @param graph Social graph
 * @param note The mute list note
 * @return 1 on success, 0 on failure
 */
int ndb_socialgraph_handle_mute_list(void *txn, struct ndb_socialgraph *graph,
                                     struct ndb_note *note);

/**
 * Get follow distance from root user (internal API)
 * @param txn Active transaction
 * @param graph Social graph
 * @param pubkey 32-byte pubkey
 * @return Distance (0 = root, 1 = followed by root, etc), 1000 if not in graph
 */
uint32_t ndb_sg_get_follow_distance(void *txn, struct ndb_socialgraph *graph,
                                     const unsigned char *pubkey);

/**
 * Check if one user follows another (internal API)
 * @param txn Active transaction
 * @param graph Social graph
 * @param follower_pubkey 32-byte follower pubkey
 * @param followed_pubkey 32-byte followed pubkey
 * @return 1 if following, 0 otherwise
 */
int ndb_sg_is_following(void *txn, struct ndb_socialgraph *graph,
                        const unsigned char *follower_pubkey,
                        const unsigned char *followed_pubkey);

/**
 * Get list of users followed by a user (internal API)
 * @param txn Active transaction
 * @param graph Social graph
 * @param pubkey 32-byte pubkey
 * @param followed_out Output array of 32-byte pubkeys (caller allocates)
 * @param max_out Maximum number of entries in output array
 * @return Number of followed users written to output (may be less than actual count)
 */
int ndb_sg_get_followed(void *txn, struct ndb_socialgraph *graph,
                        const unsigned char *pubkey,
                        unsigned char *followed_out, int max_out);

/**
 * Get list of followers of a user (internal API)
 * @param txn Active transaction
 * @param graph Social graph
 * @param pubkey 32-byte pubkey
 * @param followers_out Output array of 32-byte pubkeys (caller allocates)
 * @param max_out Maximum number of entries in output array
 * @return Number of followers written to output (may be less than actual count)
 */
int ndb_sg_get_followers(void *txn, struct ndb_socialgraph *graph,
                         const unsigned char *pubkey,
                         unsigned char *followers_out, int max_out);

/**
 * Get follower count for a user (internal API)
 * @param txn Active transaction
 * @param graph Social graph
 * @param pubkey 32-byte pubkey
 * @return Number of followers
 */
int ndb_sg_follower_count(void *txn, struct ndb_socialgraph *graph,
                          const unsigned char *pubkey);

/**
 * Get followed count for a user (internal API)
 * @param txn Active transaction
 * @param graph Social graph
 * @param pubkey 32-byte pubkey
 * @return Number of users they follow
 */
int ndb_sg_followed_count(void *txn, struct ndb_socialgraph *graph,
                          const unsigned char *pubkey);

/**
 * Check if one user mutes another (internal API)
 * @param txn Active transaction
 * @param graph Social graph
 * @param muter_pubkey 32-byte muter pubkey
 * @param muted_pubkey 32-byte muted pubkey
 * @return 1 if muting, 0 otherwise
 */
int ndb_sg_is_muting(void *txn, struct ndb_socialgraph *graph,
                     const unsigned char *muter_pubkey,
                     const unsigned char *muted_pubkey);

/**
 * Get list of users muted by a user (internal API)
 * @param txn Active transaction
 * @param graph Social graph
 * @param pubkey 32-byte pubkey
 * @param muted_out Output array of 32-byte pubkeys (caller allocates)
 * @param max_out Maximum number of entries in output array
 * @return Number of muted users written to output (may be less than actual count)
 */
int ndb_sg_get_muted(void *txn, struct ndb_socialgraph *graph,
                     const unsigned char *pubkey,
                     unsigned char *muted_out, int max_out);

/**
 * Get list of users who mute this user (internal API)
 * @param txn Active transaction
 * @param graph Social graph
 * @param pubkey 32-byte pubkey
 * @param muters_out Output array of 32-byte pubkeys (caller allocates)
 * @param max_out Maximum number of entries in output array
 * @return Number of muters written to output (may be less than actual count)
 */
int ndb_sg_get_muters(void *txn, struct ndb_socialgraph *graph,
                      const unsigned char *pubkey,
                      unsigned char *muters_out, int max_out);

/**
 * Get muter count for a user (internal API)
 * @param txn Active transaction
 * @param graph Social graph
 * @param pubkey 32-byte pubkey
 * @return Number of users muting this user
 */
int ndb_sg_muter_count(void *txn, struct ndb_socialgraph *graph,
                       const unsigned char *pubkey);

/**
 * Set the root user for distance calculations (internal API)
 * Recalculates all distances from the new root if changed
 * Must be called within a write transaction
 * @param txn Active write transaction
 * @param graph Social graph
 * @param root_pubkey 32-byte root user pubkey
 * @return 1 on success, 0 on failure
 */
int ndb_sg_set_root(void *txn, struct ndb_socialgraph *graph,
                    const unsigned char *root_pubkey);

/**
 * Recalculate follow distances from root user via BFS
 * Must be called within a write transaction
 * @param txn Active write transaction
 * @param graph Social graph
 * @return 1 on success, 0 on failure
 */
int ndb_socialgraph_recalculate_distances(void *txn, struct ndb_socialgraph *graph);

/**
 * Destroy social graph (closes databases)
 */
void ndb_socialgraph_destroy(struct ndb_socialgraph *graph);

#endif // NDB_SOCIALGRAPH_H
