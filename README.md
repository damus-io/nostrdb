
# nostrdb

The unfairly fast nostr database backed by lmdb.

nostrdb stores nostr events as a custom in-memory representation that enables
zero-copy and O(1) access to all note fields. This is similar to flatbuffers
but it is custom built for nostr events.

These events are then memory-mapped inside lmdb, enabling insanely fast,
zero-copy access and querying.
