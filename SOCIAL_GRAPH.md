# Social Graph

nostrdb includes an integrated social graph index that builds follow relationships from kind 3 (contact list) events.

## Architecture

### UID Mapping

Pubkeys (32 bytes) are mapped to locally unique 32-bit integer IDs (UIDs) to reduce index size.

- **Type**: `uint32_t` (4.29 billion user capacity)
- **Storage savings**: ~28 bytes per pubkey reference in adjacency lists
- **Memory impact**: For 10M edges, saves ~100MB vs using full pubkeys

If you need >4B users, change `typedef uint32_t ndb_uid_t` to `uint64_t` in `src/ndb_uid.h`.

**UID Allocation**: Counter-based (`next_id++`), reconstructed from max existing UID on init. Thread-safe within nostrdb's single-writer architecture. **Not safe for multi-process access**—running multiple nostrdb processes against the same database will cause UID collisions.

### Adjacency List Storage

Follow relationships use the **set pattern**: `UID → array<UID>` rather than composite keys.

**Set pattern** (current):
- Key: follower UID
- Value: packed array of followed UIDs
- Storage: ~4 bytes per edge
- `is_following(A,B)`: O(log N) btree + O(M) array scan (M = follow count, typically 100-500)
- `get_followed(A)`: O(log N) btree, returns assembled array

**Composite key alternative**:
- Key: (follower_uid, followed_uid)
- Value: empty
- Storage: ~8 bytes per edge
- `is_following(A,B)`: O(log N) btree lookup
- `get_followed(A)`: O(log N + M) range scan

The set pattern is optimal for Nostr's access patterns where retrieving full follow lists is more common than single edge checks. With typical follow counts of 100-500 users, linear array scans are negligible.

### Databases

- `uid_str_to_id`: 32-byte pubkey → UID
- `uid_id_to_str`: UID → 32-byte pubkey
- `sg_follow_distance`: UID → distance from root
- `sg_followed_by_user`: UID → array of UIDs they follow
- `sg_followers_by_user`: UID → array of UIDs following them
- `sg_follow_list_created_at`: UID → contact list timestamp (prevents processing older contact lists)
- `sg_users_by_follow_distance`: distance → array of UIDs at that distance (reverse index, semi-essential)

The `users_by_follow_distance` index enables efficient distance-based queries and iteration. It's primarily used for stats and serialization. Could be made optional/togglable if you only need direct follow lookups.

## Usage

Contact lists (kind 3) are automatically processed during event ingestion. Query the graph via:

```c
struct ndb_txn txn;
ndb_begin_query(ndb, &txn);

uint32_t distance = ndb_socialgraph_get_follow_distance(&txn, ndb, pubkey);
int follows = ndb_socialgraph_is_following(&txn, ndb, follower_pk, followed_pk);
int count = ndb_socialgraph_follower_count(&txn, ndb, pubkey);

ndb_end_query(&txn);
```

## Implementation

Based on [nostr-social-graph](https://github.com/mmalmi/nostr-social-graph).
