
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

## CLI

nostrdb comes with a handy `ndb` command line tool for interacting with nostrdb
databases. The tool is relatively new, and only supports a few commands.

### Usage

```
usage: ndb [--skip-verification] [-d db_dir] <command>

commands

	stat
	search [--oldest-first] [--limit 42] <fulltext query>
	import <line-delimited json file>

settings

	--skip-verification  skip signature validation
	-d <db_dir>          set database directory
```

### Building

```bash
$ make ndb
```

### Fulltext Queries

nostrdb supports fulltext queries. You can import some test events like so:

```
$ make testdata/many-events.json
$ ndb --skip-verification import testdata/many-events.json
$ ndb search --limit 2 --oldest-first 'nosy ostrich'

[01] K<'ostrich' 7 1671217526 note_id:253309>
Q: What do you call a nosy ostrich?
A: A nosTrich!
```
