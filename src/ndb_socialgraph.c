#include "ndb_socialgraph.h"
#include "nostrdb.h"
#include "hex.h"
#include "lmdb.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Helper to pack/unpack UID arrays for storage
// Format: 4-byte count + array of UIDs
struct uid_list {
	uint32_t count;
	ndb_uid_t uids[];
};

static int uid_list_contains(struct uid_list *list, ndb_uid_t uid)
{
	for (uint32_t i = 0; i < list->count; i++) {
		if (list->uids[i] == uid)
			return 1;
	}
	return 0;
}

static struct uid_list *uid_list_create(uint32_t capacity)
{
	size_t size = sizeof(struct uid_list) + capacity * sizeof(ndb_uid_t);
	struct uid_list *list = malloc(size);
	if (list)
		list->count = 0;
	return list;
}

static int uid_list_add(struct uid_list **list_ptr, uint32_t *capacity, ndb_uid_t uid)
{
	struct uid_list *list = *list_ptr;

	// Check if already exists
	if (uid_list_contains(list, uid))
		return 1;

	// Resize if needed
	if (list->count >= *capacity) {
		uint32_t new_capacity = *capacity * 2;
		size_t new_size = sizeof(struct uid_list) + new_capacity * sizeof(ndb_uid_t);
		struct uid_list *new_list = realloc(list, new_size);
		if (!new_list)
			return 0;
		*list_ptr = new_list;
		*capacity = new_capacity;
		list = new_list;
	}

	list->uids[list->count++] = uid;
	return 1;
}

static size_t uid_list_size(struct uid_list *list)
{
	return sizeof(struct uid_list) + list->count * sizeof(ndb_uid_t);
}

int ndb_socialgraph_init(struct ndb_socialgraph *graph, void *env,
                         const unsigned char *root_pubkey)
{
	MDB_txn *txn;
	int rc;
	MDB_env *mdb_env = (MDB_env*)env;

	graph->env = env;

	// Initialize UID mapper
	if (!ndb_uid_map_init(&graph->uid_map, env)) {
		fprintf(stderr, "ndb_socialgraph_init: failed to init uid_map\n");
		return 0;
	}

	// Create write transaction
	if ((rc = mdb_txn_begin(mdb_env, NULL, 0, &txn))) {
		fprintf(stderr, "ndb_socialgraph_init: mdb_txn_begin failed: %s\n",
			mdb_strerror(rc));
		ndb_uid_map_destroy(&graph->uid_map);
		return 0;
	}

	// Create databases
	MDB_dbi follow_distance_dbi, followed_by_user_dbi;
	MDB_dbi followers_by_user_dbi, follow_list_created_at_dbi;
	MDB_dbi muted_by_user_dbi, user_muted_by_dbi, mute_list_created_at_dbi;

	if ((rc = mdb_dbi_open(txn, "sg_follow_distance", MDB_CREATE, &follow_distance_dbi)) ||
	    (rc = mdb_dbi_open(txn, "sg_followed_by_user", MDB_CREATE, &followed_by_user_dbi)) ||
	    (rc = mdb_dbi_open(txn, "sg_followers_by_user", MDB_CREATE, &followers_by_user_dbi)) ||
	    (rc = mdb_dbi_open(txn, "sg_follow_list_created_at", MDB_CREATE, &follow_list_created_at_dbi)) ||
	    (rc = mdb_dbi_open(txn, "sg_muted_by_user", MDB_CREATE, &muted_by_user_dbi)) ||
	    (rc = mdb_dbi_open(txn, "sg_user_muted_by", MDB_CREATE, &user_muted_by_dbi)) ||
	    (rc = mdb_dbi_open(txn, "sg_mute_list_created_at", MDB_CREATE, &mute_list_created_at_dbi))) {
		fprintf(stderr, "ndb_socialgraph_init: failed to open databases: %s\n",
			mdb_strerror(rc));
		mdb_txn_abort(txn);
		ndb_uid_map_destroy(&graph->uid_map);
		return 0;
	}

	graph->follow_distance_db = (void*)(intptr_t)follow_distance_dbi;
	graph->followed_by_user_db = (void*)(intptr_t)followed_by_user_dbi;
	graph->followers_by_user_db = (void*)(intptr_t)followers_by_user_dbi;
	graph->follow_list_created_at_db = (void*)(intptr_t)follow_list_created_at_dbi;
	graph->muted_by_user_db = (void*)(intptr_t)muted_by_user_dbi;
	graph->user_muted_by_db = (void*)(intptr_t)user_muted_by_dbi;
	graph->mute_list_created_at_db = (void*)(intptr_t)mute_list_created_at_dbi;

	// Get or create UID for root
	if (!ndb_uid_get_or_create(txn, &graph->uid_map, root_pubkey, &graph->root_uid)) {
		fprintf(stderr, "ndb_socialgraph_init: failed to get root uid\n");
		mdb_txn_abort(txn);
		ndb_uid_map_destroy(&graph->uid_map);
		return 0;
	}

	// Set root distance to 0
	MDB_val key, val;
	key.mv_data = &graph->root_uid;
	key.mv_size = sizeof(ndb_uid_t);
	uint32_t zero = 0;
	val.mv_data = &zero;
	val.mv_size = sizeof(uint32_t);

	if ((rc = mdb_put(txn, follow_distance_dbi, &key, &val, 0))) {
		fprintf(stderr, "ndb_socialgraph_init: failed to set root distance: %s\n",
			mdb_strerror(rc));
		mdb_txn_abort(txn);
		ndb_uid_map_destroy(&graph->uid_map);
		return 0;
	}

	if ((rc = mdb_txn_commit(txn))) {
		fprintf(stderr, "ndb_socialgraph_init: mdb_txn_commit failed: %s\n",
			mdb_strerror(rc));
		ndb_uid_map_destroy(&graph->uid_map);
		return 0;
	}

	return 1;
}

int ndb_socialgraph_handle_contact_list(void *txn, struct ndb_socialgraph *graph,
                                        struct ndb_note *note)
{
	MDB_val key, val;
	int rc;
	ndb_uid_t author_uid;
	uint64_t created_at;
	struct uid_list *new_followed_list;
	uint32_t new_followed_capacity = 64;

	// Get author UID
	if (!ndb_uid_get_or_create(txn, &graph->uid_map, ndb_note_pubkey(note), &author_uid))
		return 0;

	created_at = ndb_note_created_at(note);

	// Check if we have a newer contact list already
	MDB_dbi created_at_dbi = (MDB_dbi)(intptr_t)graph->follow_list_created_at_db;
	key.mv_data = &author_uid;
	key.mv_size = sizeof(ndb_uid_t);

	if ((rc = mdb_get(txn, created_at_dbi, &key, &val)) == 0) {
		if (val.mv_size == sizeof(uint64_t)) {
			uint64_t existing_created_at;
			memcpy(&existing_created_at, val.mv_data, sizeof(uint64_t));
			if (created_at <= existing_created_at) {
				// Older or same timestamp, ignore
				return 1;
			}
		}
	}

	// Parse p-tags to build new follow list
	new_followed_list = uid_list_create(new_followed_capacity);
	if (!new_followed_list)
		return 0;

	// Iterate over tags
	struct ndb_iterator iter, *it = &iter;
	ndb_tags_iterate_start(note, it);

	while (ndb_tags_iterate_next(it)) {
		if (ndb_tag_count(it->tag) < 2)
			continue;

		// Check if it's a 'p' tag
		struct ndb_str str = ndb_tag_str(note, it->tag, 0);
		if (str.flag != NDB_PACKED_STR || str.str[0] != 'p' || str.str[1] != '\0')
			continue;

		// Get the pubkey from tag[1]
		str = ndb_tag_str(note, it->tag, 1);

		unsigned char pubkey[32];
		if (str.flag == NDB_PACKED_ID) {
			// Already binary
			memcpy(pubkey, str.id, 32);
		} else if (str.flag == NDB_PACKED_STR) {
			// Hex string, need to decode
			if (strlen(str.str) != 64)
				continue;
			if (!hex_decode(str.str, 64, pubkey, 32))
				continue;
		} else {
			continue;
		}

		// Get UID for followed user
		ndb_uid_t followed_uid;
		if (!ndb_uid_get_or_create(txn, &graph->uid_map, pubkey, &followed_uid)) {
			free(new_followed_list);
			return 0;
		}

		// Don't add self-follows
		if (followed_uid == author_uid)
			continue;

		if (!uid_list_add(&new_followed_list, &new_followed_capacity, followed_uid)) {
			free(new_followed_list);
			return 0;
		}
	}

	// Get old follow list
	MDB_dbi followed_by_user_dbi = (MDB_dbi)(intptr_t)graph->followed_by_user_db;
	MDB_dbi followers_by_user_dbi = (MDB_dbi)(intptr_t)graph->followers_by_user_db;

	struct uid_list *old_followed_list = NULL;
	if ((rc = mdb_get(txn, followed_by_user_dbi, &key, &val)) == 0) {
		// Copy old list
		old_followed_list = malloc(val.mv_size);
		if (old_followed_list)
			memcpy(old_followed_list, val.mv_data, val.mv_size);
	}

	// Process unfollows: remove from followers_by_user
	if (old_followed_list) {
		for (uint32_t i = 0; i < old_followed_list->count; i++) {
			ndb_uid_t unfollowed_uid = old_followed_list->uids[i];

			// Skip if still followed
			if (uid_list_contains(new_followed_list, unfollowed_uid))
				continue;

			// Remove author from this user's followers
			MDB_val follower_key, follower_val;
			follower_key.mv_data = &unfollowed_uid;
			follower_key.mv_size = sizeof(ndb_uid_t);

			if ((rc = mdb_get(txn, followers_by_user_dbi, &follower_key, &follower_val)) == 0) {
				struct uid_list *followers = malloc(follower_val.mv_size);
				if (followers) {
					memcpy(followers, follower_val.mv_data, follower_val.mv_size);

					// Remove author_uid from followers list
					for (uint32_t j = 0; j < followers->count; j++) {
						if (followers->uids[j] == author_uid) {
							// Shift remaining elements
							memmove(&followers->uids[j], &followers->uids[j+1],
							        (followers->count - j - 1) * sizeof(ndb_uid_t));
							followers->count--;
							break;
						}
					}

					// Update or delete
					if (followers->count == 0) {
						mdb_del(txn, followers_by_user_dbi, &follower_key, NULL);
					} else {
						follower_val.mv_data = followers;
						follower_val.mv_size = uid_list_size(followers);
						mdb_put(txn, followers_by_user_dbi, &follower_key, &follower_val, 0);
					}
					free(followers);
				}
			}
		}
		free(old_followed_list);
	}

	// Process new follows: add to followers_by_user
	for (uint32_t i = 0; i < new_followed_list->count; i++) {
		ndb_uid_t followed_uid = new_followed_list->uids[i];

		MDB_val follower_key, follower_val;
		follower_key.mv_data = &followed_uid;
		follower_key.mv_size = sizeof(ndb_uid_t);

		struct uid_list *followers = NULL;
		uint32_t followers_capacity = 64;

		if ((rc = mdb_get(txn, followers_by_user_dbi, &follower_key, &follower_val)) == 0) {
			// Existing followers list
			followers = malloc(follower_val.mv_size);
			if (!followers) {
				free(new_followed_list);
				return 0;
			}
			memcpy(followers, follower_val.mv_data, follower_val.mv_size);
			followers_capacity = followers->count + 16; // Some headroom
		} else {
			// New followers list
			followers = uid_list_create(followers_capacity);
			if (!followers) {
				free(new_followed_list);
				return 0;
			}
		}

		// Add author to followers list
		if (!uid_list_add(&followers, &followers_capacity, author_uid)) {
			free(followers);
			free(new_followed_list);
			return 0;
		}

		// Store updated followers list
		follower_val.mv_data = followers;
		follower_val.mv_size = uid_list_size(followers);
		if ((rc = mdb_put(txn, followers_by_user_dbi, &follower_key, &follower_val, 0))) {
			free(followers);
			free(new_followed_list);
			return 0;
		}
		free(followers);
	}

	// Store new followed list
	val.mv_data = new_followed_list;
	val.mv_size = uid_list_size(new_followed_list);
	if ((rc = mdb_put(txn, followed_by_user_dbi, &key, &val, 0))) {
		free(new_followed_list);
		return 0;
	}

	// Update created_at timestamp
	val.mv_data = &created_at;
	val.mv_size = sizeof(uint64_t);
	if ((rc = mdb_put(txn, created_at_dbi, &key, &val, 0))) {
		free(new_followed_list);
		return 0;
	}

	free(new_followed_list);

	// Trigger distance recalculation
	return ndb_socialgraph_recalculate_distances(txn, graph);
}

int ndb_socialgraph_recalculate_distances(void *txn, struct ndb_socialgraph *graph)
{
	MDB_dbi distance_dbi = (MDB_dbi)(intptr_t)graph->follow_distance_db;
	MDB_dbi followed_dbi = (MDB_dbi)(intptr_t)graph->followed_by_user_db;
	int rc;

	// Clear existing distances
	if ((rc = mdb_drop(txn, distance_dbi, 0))) {
		fprintf(stderr, "ndb_socialgraph_recalculate_distances: mdb_drop failed: %s\n",
			mdb_strerror(rc));
		return 0;
	}

	// BFS queue (simple growing array)
	ndb_uid_t *queue = malloc(1024 * sizeof(ndb_uid_t));
	uint32_t queue_capacity = 1024;
	uint32_t queue_head = 0;
	uint32_t queue_tail = 0;

	if (!queue)
		return 0;

	// Set root distance to 0
	MDB_val key, val;
	key.mv_data = &graph->root_uid;
	key.mv_size = sizeof(ndb_uid_t);
	uint32_t zero = 0;
	val.mv_data = &zero;
	val.mv_size = sizeof(uint32_t);

	if ((rc = mdb_put(txn, distance_dbi, &key, &val, 0))) {
		free(queue);
		return 0;
	}

	// Enqueue root
	queue[queue_tail++] = graph->root_uid;

	// BFS
	while (queue_head < queue_tail) {
		ndb_uid_t current_uid = queue[queue_head++];

		// Get current distance
		key.mv_data = &current_uid;
		key.mv_size = sizeof(ndb_uid_t);
		if ((rc = mdb_get(txn, distance_dbi, &key, &val)) != 0)
			continue;

		uint32_t current_distance;
		memcpy(&current_distance, val.mv_data, sizeof(uint32_t));
		uint32_t next_distance = current_distance + 1;

		// Get followed users
		if ((rc = mdb_get(txn, followed_dbi, &key, &val)) != 0)
			continue;

		struct uid_list *followed = (struct uid_list*)val.mv_data;

		for (uint32_t i = 0; i < followed->count; i++) {
			ndb_uid_t followed_uid = followed->uids[i];

			// Check if already has a distance
			MDB_val check_key, check_val;
			check_key.mv_data = &followed_uid;
			check_key.mv_size = sizeof(ndb_uid_t);

			if ((rc = mdb_get(txn, distance_dbi, &check_key, &check_val)) == 0) {
				// Already visited
				continue;
			}

			// Set distance
			check_val.mv_data = &next_distance;
			check_val.mv_size = sizeof(uint32_t);
			if ((rc = mdb_put(txn, distance_dbi, &check_key, &check_val, 0))) {
				free(queue);
				return 0;
			}

			// Enqueue
			if (queue_tail >= queue_capacity) {
				queue_capacity *= 2;
				ndb_uid_t *new_queue = realloc(queue, queue_capacity * sizeof(ndb_uid_t));
				if (!new_queue) {
					free(queue);
					return 0;
				}
				queue = new_queue;
			}
			queue[queue_tail++] = followed_uid;
		}
	}

	free(queue);
	return 1;
}

uint32_t ndb_sg_get_follow_distance(void *txn, struct ndb_socialgraph *graph,
                                    const unsigned char *pubkey)
{
	ndb_uid_t uid;
	MDB_val key, val;
	MDB_dbi dbi = (MDB_dbi)(intptr_t)graph->follow_distance_db;

	if (!ndb_uid_get(txn, &graph->uid_map, pubkey, &uid))
		return 1000;

	key.mv_data = &uid;
	key.mv_size = sizeof(ndb_uid_t);

	if (mdb_get(txn, dbi, &key, &val) != 0)
		return 1000;

	uint32_t distance;
	memcpy(&distance, val.mv_data, sizeof(uint32_t));
	return distance;
}

int ndb_sg_is_following(void *txn, struct ndb_socialgraph *graph,
                        const unsigned char *follower_pubkey,
                        const unsigned char *followed_pubkey)
{
	ndb_uid_t follower_uid, followed_uid;
	MDB_val key, val;
	MDB_dbi dbi = (MDB_dbi)(intptr_t)graph->followed_by_user_db;

	if (!ndb_uid_get(txn, &graph->uid_map, follower_pubkey, &follower_uid))
		return 0;
	if (!ndb_uid_get(txn, &graph->uid_map, followed_pubkey, &followed_uid))
		return 0;

	key.mv_data = &follower_uid;
	key.mv_size = sizeof(ndb_uid_t);

	if (mdb_get(txn, dbi, &key, &val) != 0)
		return 0;

	struct uid_list *followed = (struct uid_list*)val.mv_data;
	return uid_list_contains(followed, followed_uid);
}

int ndb_sg_get_followed(void *txn, struct ndb_socialgraph *graph,
                        const unsigned char *pubkey,
                        unsigned char *followed_out, int max_out)
{
	ndb_uid_t uid;
	MDB_val key, val;
	MDB_dbi dbi = (MDB_dbi)(intptr_t)graph->followed_by_user_db;

	if (!ndb_uid_get(txn, &graph->uid_map, pubkey, &uid))
		return 0;

	key.mv_data = &uid;
	key.mv_size = sizeof(ndb_uid_t);

	if (mdb_get(txn, dbi, &key, &val) != 0)
		return 0;

	struct uid_list *followed = (struct uid_list*)val.mv_data;
	int count = followed->count < max_out ? followed->count : max_out;

	for (int i = 0; i < count; i++) {
		if (!ndb_uid_to_pubkey(txn, &graph->uid_map, followed->uids[i],
		                       &followed_out[i * 32])) {
			return i;
		}
	}

	return count;
}

int ndb_sg_get_followers(void *txn, struct ndb_socialgraph *graph,
                         const unsigned char *pubkey,
                         unsigned char *followers_out, int max_out)
{
	ndb_uid_t uid;
	MDB_val key, val;
	MDB_dbi dbi = (MDB_dbi)(intptr_t)graph->followers_by_user_db;

	if (!ndb_uid_get(txn, &graph->uid_map, pubkey, &uid))
		return 0;

	key.mv_data = &uid;
	key.mv_size = sizeof(ndb_uid_t);

	if (mdb_get(txn, dbi, &key, &val) != 0)
		return 0;

	struct uid_list *followers = (struct uid_list*)val.mv_data;
	int count = followers->count < max_out ? followers->count : max_out;

	for (int i = 0; i < count; i++) {
		if (!ndb_uid_to_pubkey(txn, &graph->uid_map, followers->uids[i],
		                       &followers_out[i * 32])) {
			return i;
		}
	}

	return count;
}

int ndb_sg_follower_count(void *txn, struct ndb_socialgraph *graph,
                          const unsigned char *pubkey)
{
	ndb_uid_t uid;
	MDB_val key, val;
	MDB_dbi dbi = (MDB_dbi)(intptr_t)graph->followers_by_user_db;

	if (!ndb_uid_get(txn, &graph->uid_map, pubkey, &uid))
		return 0;

	key.mv_data = &uid;
	key.mv_size = sizeof(ndb_uid_t);

	if (mdb_get(txn, dbi, &key, &val) != 0)
		return 0;

	struct uid_list *followers = (struct uid_list*)val.mv_data;
	return followers->count;
}

int ndb_socialgraph_handle_mute_list(void *txn, struct ndb_socialgraph *graph,
                                     struct ndb_note *note)
{
	MDB_val key, val;
	int rc;
	ndb_uid_t author_uid;
	uint64_t created_at;
	struct uid_list *new_muted_list;
	uint32_t new_muted_capacity = 64;

	// Get author UID
	if (!ndb_uid_get_or_create(txn, &graph->uid_map, ndb_note_pubkey(note), &author_uid))
		return 0;

	created_at = ndb_note_created_at(note);

	// Check if we have a newer mute list already
	MDB_dbi created_at_dbi = (MDB_dbi)(intptr_t)graph->mute_list_created_at_db;
	key.mv_data = &author_uid;
	key.mv_size = sizeof(ndb_uid_t);

	if ((rc = mdb_get(txn, created_at_dbi, &key, &val)) == 0) {
		if (val.mv_size == sizeof(uint64_t)) {
			uint64_t existing_created_at;
			memcpy(&existing_created_at, val.mv_data, sizeof(uint64_t));
			if (created_at <= existing_created_at) {
				// Older or same timestamp, ignore
				return 1;
			}
		}
	}

	// Parse p-tags to build new mute list
	new_muted_list = uid_list_create(new_muted_capacity);
	if (!new_muted_list)
		return 0;

	// Iterate over tags
	struct ndb_iterator iter, *it = &iter;
	ndb_tags_iterate_start(note, it);

	while (ndb_tags_iterate_next(it)) {
		if (ndb_tag_count(it->tag) < 2)
			continue;

		// Check if it's a 'p' tag
		struct ndb_str str = ndb_tag_str(note, it->tag, 0);
		if (str.flag != NDB_PACKED_STR || str.str[0] != 'p' || str.str[1] != '\0')
			continue;

		// Get the pubkey from tag[1]
		str = ndb_tag_str(note, it->tag, 1);

		unsigned char pubkey[32];
		if (str.flag == NDB_PACKED_ID) {
			// Already binary
			memcpy(pubkey, str.id, 32);
		} else if (str.flag == NDB_PACKED_STR) {
			// Hex string, need to decode
			if (strlen(str.str) != 64)
				continue;
			if (!hex_decode(str.str, 64, pubkey, 32))
				continue;
		} else {
			continue;
		}

		// Get UID for muted user
		ndb_uid_t muted_uid;
		if (!ndb_uid_get_or_create(txn, &graph->uid_map, pubkey, &muted_uid)) {
			free(new_muted_list);
			return 0;
		}

		// Don't add self-mutes
		if (muted_uid == author_uid)
			continue;

		if (!uid_list_add(&new_muted_list, &new_muted_capacity, muted_uid)) {
			free(new_muted_list);
			return 0;
		}
	}

	// Get old mute list
	MDB_dbi muted_by_user_dbi = (MDB_dbi)(intptr_t)graph->muted_by_user_db;
	MDB_dbi user_muted_by_dbi = (MDB_dbi)(intptr_t)graph->user_muted_by_db;

	struct uid_list *old_muted_list = NULL;
	if ((rc = mdb_get(txn, muted_by_user_dbi, &key, &val)) == 0) {
		// Copy old list
		old_muted_list = malloc(val.mv_size);
		if (old_muted_list)
			memcpy(old_muted_list, val.mv_data, val.mv_size);
	}

	// Process unmutes: remove from user_muted_by
	if (old_muted_list) {
		for (uint32_t i = 0; i < old_muted_list->count; i++) {
			ndb_uid_t unmuted_uid = old_muted_list->uids[i];

			// Skip if still muted
			if (uid_list_contains(new_muted_list, unmuted_uid))
				continue;

			// Remove author from this user's muters
			MDB_val muter_key, muter_val;
			muter_key.mv_data = &unmuted_uid;
			muter_key.mv_size = sizeof(ndb_uid_t);

			if ((rc = mdb_get(txn, user_muted_by_dbi, &muter_key, &muter_val)) == 0) {
				struct uid_list *muters = malloc(muter_val.mv_size);
				if (muters) {
					memcpy(muters, muter_val.mv_data, muter_val.mv_size);

					// Remove author_uid from muters list
					for (uint32_t j = 0; j < muters->count; j++) {
						if (muters->uids[j] == author_uid) {
							// Shift remaining elements
							memmove(&muters->uids[j], &muters->uids[j+1],
							        (muters->count - j - 1) * sizeof(ndb_uid_t));
							muters->count--;
							break;
						}
					}

					// Update or delete
					if (muters->count == 0) {
						mdb_del(txn, user_muted_by_dbi, &muter_key, NULL);
					} else {
						muter_val.mv_data = muters;
						muter_val.mv_size = uid_list_size(muters);
						mdb_put(txn, user_muted_by_dbi, &muter_key, &muter_val, 0);
					}
					free(muters);
				}
			}
		}
		free(old_muted_list);
	}

	// Process new mutes: add to user_muted_by
	for (uint32_t i = 0; i < new_muted_list->count; i++) {
		ndb_uid_t muted_uid = new_muted_list->uids[i];

		MDB_val muter_key, muter_val;
		muter_key.mv_data = &muted_uid;
		muter_key.mv_size = sizeof(ndb_uid_t);

		struct uid_list *muters = NULL;
		uint32_t muters_capacity = 64;

		if ((rc = mdb_get(txn, user_muted_by_dbi, &muter_key, &muter_val)) == 0) {
			// Existing muters list
			muters = malloc(muter_val.mv_size);
			if (!muters) {
				free(new_muted_list);
				return 0;
			}
			memcpy(muters, muter_val.mv_data, muter_val.mv_size);
			muters_capacity = muters->count + 16; // Some headroom
		} else {
			// New muters list
			muters = uid_list_create(muters_capacity);
			if (!muters) {
				free(new_muted_list);
				return 0;
			}
		}

		// Add author to muters list
		if (!uid_list_add(&muters, &muters_capacity, author_uid)) {
			free(muters);
			free(new_muted_list);
			return 0;
		}

		// Store updated muters list
		muter_val.mv_data = muters;
		muter_val.mv_size = uid_list_size(muters);
		if ((rc = mdb_put(txn, user_muted_by_dbi, &muter_key, &muter_val, 0))) {
			free(muters);
			free(new_muted_list);
			return 0;
		}
		free(muters);
	}

	// Store new muted list
	val.mv_data = new_muted_list;
	val.mv_size = uid_list_size(new_muted_list);
	if ((rc = mdb_put(txn, muted_by_user_dbi, &key, &val, 0))) {
		free(new_muted_list);
		return 0;
	}

	// Update created_at timestamp
	val.mv_data = &created_at;
	val.mv_size = sizeof(uint64_t);
	if ((rc = mdb_put(txn, created_at_dbi, &key, &val, 0))) {
		free(new_muted_list);
		return 0;
	}

	free(new_muted_list);
	return 1;
}

int ndb_sg_is_muting(void *txn, struct ndb_socialgraph *graph,
                     const unsigned char *muter_pubkey,
                     const unsigned char *muted_pubkey)
{
	ndb_uid_t muter_uid, muted_uid;
	MDB_val key, val;
	MDB_dbi dbi = (MDB_dbi)(intptr_t)graph->muted_by_user_db;

	if (!ndb_uid_get(txn, &graph->uid_map, muter_pubkey, &muter_uid))
		return 0;
	if (!ndb_uid_get(txn, &graph->uid_map, muted_pubkey, &muted_uid))
		return 0;

	key.mv_data = &muter_uid;
	key.mv_size = sizeof(ndb_uid_t);

	if (mdb_get(txn, dbi, &key, &val) != 0)
		return 0;

	struct uid_list *muted = (struct uid_list*)val.mv_data;
	return uid_list_contains(muted, muted_uid);
}

int ndb_sg_get_muted(void *txn, struct ndb_socialgraph *graph,
                     const unsigned char *pubkey,
                     unsigned char *muted_out, int max_out)
{
	ndb_uid_t uid;
	MDB_val key, val;
	MDB_dbi dbi = (MDB_dbi)(intptr_t)graph->muted_by_user_db;

	if (!ndb_uid_get(txn, &graph->uid_map, pubkey, &uid))
		return 0;

	key.mv_data = &uid;
	key.mv_size = sizeof(ndb_uid_t);

	if (mdb_get(txn, dbi, &key, &val) != 0)
		return 0;

	struct uid_list *muted = (struct uid_list*)val.mv_data;
	int count = muted->count < max_out ? muted->count : max_out;

	for (int i = 0; i < count; i++) {
		if (!ndb_uid_to_pubkey(txn, &graph->uid_map, muted->uids[i],
		                       &muted_out[i * 32])) {
			return i;
		}
	}

	return count;
}

int ndb_sg_get_muters(void *txn, struct ndb_socialgraph *graph,
                      const unsigned char *pubkey,
                      unsigned char *muters_out, int max_out)
{
	ndb_uid_t uid;
	MDB_val key, val;
	MDB_dbi dbi = (MDB_dbi)(intptr_t)graph->user_muted_by_db;

	if (!ndb_uid_get(txn, &graph->uid_map, pubkey, &uid))
		return 0;

	key.mv_data = &uid;
	key.mv_size = sizeof(ndb_uid_t);

	if (mdb_get(txn, dbi, &key, &val) != 0)
		return 0;

	struct uid_list *muters = (struct uid_list*)val.mv_data;
	int count = muters->count < max_out ? muters->count : max_out;

	for (int i = 0; i < count; i++) {
		if (!ndb_uid_to_pubkey(txn, &graph->uid_map, muters->uids[i],
		                       &muters_out[i * 32])) {
			return i;
		}
	}

	return count;
}

void ndb_socialgraph_destroy(struct ndb_socialgraph *graph)
{
	ndb_uid_map_destroy(&graph->uid_map);
	graph->env = NULL;
	graph->follow_distance_db = NULL;
	graph->followed_by_user_db = NULL;
	graph->followers_by_user_db = NULL;
	graph->follow_list_created_at_db = NULL;
	graph->muted_by_user_db = NULL;
	graph->user_muted_by_db = NULL;
	graph->mute_list_created_at_db = NULL;
}
