
# nostrdb

![ci](https://github.com/damus-io/nostrdb/actions/workflows/c-cpp.yml/badge.svg)

The unfairly fast nostr database backed by lmdb.

nostrdb stores nostr events as a custom in-memory representation that enables
zero-copy and O(1) access to all note fields. This is similar to flatbuffers
but it is custom built for nostr events.

These events are then memory-mapped inside lmdb, enabling insanely fast,
zero-copy access and querying.

This entire design of nostrdb is copied almost entirely from strfry[1], the
fastest nostr relay. The difference is that nostrdb is meant to be embeddable
as a C library into any application, and does not support full relay
functionality (yet?)

[1]: https://github.com/hoytech/strfry


## API

The API is *very* unstable. nostrdb is in heavy development mode so don't
expect any of the interfaces to be stable at this time.
