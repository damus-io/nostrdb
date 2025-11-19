# Social Graph

nostrdb includes an integrated social graph index that builds follow relationships from kind 3 (contact list) events and mute relationships from kind 10000 (mute list) events.

## Architecture

### UID Mapping

Pubkeys (32 bytes) are mapped to locally unique 32-bit integer IDs (UIDs) to reduce index size.

- **Type**: `uint32_t` (4.29 billion user capacity)
- **Storage savings**: 28 bytes per UID reference (32-byte pubkey → 4-byte UID)
- **Memory impact**: For 10M edges with bidirectional indices, saves ~560MB vs using full pubkeys (56 bytes per edge: 28 bytes in forward index + 28 bytes in reverse index)

If you need >4B users, change `typedef uint32_t ndb_uid_t` to `uint64_t` in `src/ndb_uid.h`.

**UID Allocation**: Counter-based (`next_id++`), reconstructed from max existing UID on init. Thread-safe within nostrdb's single-writer architecture. **Not safe for multi-process access**—running multiple nostrdb processes against the same database will cause UID collisions.

### Storage Strategy

**Forward indexes** (bounded by user behavior, typically 100-500 follows):
- Use **bucketed UID lists** - space-optimized arrays partitioned by UID size (u8/u16/u32)
- Key: follower UID → Value: bucketed array of followed UIDs
- Storage: ~1.7 bytes per edge (78% savings vs naive array)
- `is_following(A,B)`: O(log N) btree + O(log M) binary search in buckets
- `get_followed(A)`: O(log N) btree, returns assembled array
- Insert: O(M) rebuild entire array (acceptable for bounded M)

**Reverse indexes** (unbounded by popularity, can reach millions):
- Use **composite keys** to avoid O(n) rewrites for popular users
- Key: (followed_uid, follower_uid) → Value: empty
- Storage: ~8 bytes per edge (higher overhead but scalable)
- `get_followers(A)`: O(log N + M) cursor range scan
- `follower_count(A)`: O(1) cached counter (updated on follow/unfollow)
- Insert: O(log N) single key write (critical for viral accounts)

Rationale: Users control who they follow (~500 max), but can't control follower count. Popular npubs with 1M+ followers need O(log n) insertion, not O(n) array rebuilds.

### Databases

**UID mapping (2 databases):**
- `uid_str_to_id`: 32-byte pubkey → UID (btree)
- `uid_id_to_str`: UID → 32-byte pubkey (btree)

**Follow graph (6 databases):**
- `sg_followed_by_user`: UID → bucketed_list<UID> (forward index - who you follow)
- `sg_followers_by_user`: (followed_uid, follower_uid) → empty (reverse index composite key)
- `sg_follower_count`: UID → u32 (cached follower count for O(1) queries)
- `sg_follow_distance`: UID → u32 distance from root user
- `sg_users_by_follow_distance`: (distance, UID) → empty (composite key index for distance queries)
- `sg_follow_list_created_at`: UID → u64 timestamp (prevents stale contact list processing)

**Mute graph (4 databases):**
- `sg_muted_by_user`: UID → bucketed_list<UID> (forward index - who you mute)
- `sg_user_muted_by`: (muted_uid, muter_uid) → empty (reverse index composite key)
- `sg_muter_count`: UID → u32 (cached muter count for O(1) queries)
- `sg_mute_list_created_at`: UID → u64 timestamp (prevents stale mute list processing)

All composite key indexes use LMDB's natural key ordering for efficient prefix scans. Counter databases provide O(1) follower/muter counts without cursor iteration.

## Usage

Contact lists (kind 3) and mute lists (kind 10000) are automatically processed during event ingestion. Query the graph via:

```c
struct ndb_txn txn;
ndb_begin_query(ndb, &txn);

// Follow graph queries
uint32_t distance = ndb_socialgraph_get_follow_distance(&txn, ndb, pubkey);
int follows = ndb_socialgraph_is_following(&txn, ndb, follower_pk, followed_pk);
int follower_count = ndb_socialgraph_follower_count(&txn, ndb, pubkey);
int followed_count = ndb_socialgraph_followed_count(&txn, ndb, pubkey);
int n_followed = ndb_socialgraph_get_followed(&txn, ndb, pubkey, followed_out, max_out);
int n_followers = ndb_socialgraph_get_followers(&txn, ndb, pubkey, followers_out, max_out);

// Mute list queries
int mutes = ndb_socialgraph_is_muting(&txn, ndb, muter_pk, muted_pk);
int n_muted = ndb_socialgraph_get_muted(&txn, ndb, pubkey, muted_out, max_out);
int n_muters = ndb_socialgraph_get_muters(&txn, ndb, pubkey, muters_out, max_out);
int muter_count = ndb_socialgraph_muter_count(&txn, ndb, pubkey);

ndb_end_query(&txn);
```

## Implementation

Based on [nostr-social-graph](https://github.com/mmalmi/nostr-social-graph).
