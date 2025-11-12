
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
as a C library into any application with full nostr query support.

[1]: https://github.com/hoytech/strfry

## Why nostrdb

- Zero-copy note model backed by [LMDB](https://symas.com/lmdb/) so queries simply point at memory-mapped pages.
- Custom metadata and block formats give O(1) access to note fields, reactions, and parsed blocks.
- Built-in full text indexing plus CLI tooling (`ndb`) for ad-hoc inspection.
- Ships as a portable static library (`libnostrdb.a`) with bindings for C, Rust, and Swift.
- Focused on embeddability—drop it into a relay, client, or analytics pipeline.

## Project status

The API is **very unstable**. nostrdb is in heavy development mode so expect
breaking changes between commits. Track higher level docs in `docs/` for the latest APIs.

## Supported platforms & dependencies

nostrdb targets modern Unix-like systems (Linux, macOS). Windows builds require additional
work and are not covered by the default build. The repository vendors most dependencies but
you still need a toolchain with:

- A C11 compiler (tested with Clang 15+ and GCC 12+)
- `make`, `pkg-config`, and standard build tools (autoconf/automake for secp256k1)
- `curl` and `zstd` (used to download sample data)
- Optional: `flatc`/`flatcc` if regenerating schema bindings, `nix` if using `shell.nix`

Third-party libraries are fetched/built automatically:

- [LMDB](https://github.com/LMDB/lmdb) (memory-mapped storage)
- [libsecp256k1](https://github.com/bitcoin-core/secp256k1) (signature verification)
- [flatcc](https://github.com/dvidelabs/flatcc) / [flatbuffers](https://github.com/google/flatbuffers) (schema tooling)
- [ccan](https://github.com/rustyrussell/ccan) utilities

## Quick start

```bash
git clone https://github.com/damus-io/nostrdb.git
cd nostrdb
# refresh vendored submodules (libsecp256k1)
./devtools/refresh-submodules.sh deps/secp256k1

# build the CLI + static library
make ndb   # or simply `make` to build ndb, libnostrdb.a, and benchmarks
```

### Run the sanitizer-backed tests

```bash
make testdata/db/.dir        # ensures the temporary LMDB dir exists
make test                    # builds with ASan/UBSan flags and runs ./test
```

### Import sample data and query

```bash
# Download fixtures (zstd archives are pulled from jb55.com CDN)
make testdata/many-events.json

# Create an empty LMDB environment in ./data (default) and ingest
./ndb --skip-verification import testdata/many-events.json

# Run a text search (newest first by default)
./ndb query --search "nostrdb" --limit 5

# Print global stats about the database
./ndb stat
```

Extra fixtures are listed under `testdata/` (see Makefile targets). You can also point `ndb`
at an existing relay database via `-d path/to/db`.

### Where to go next

- `docs/getting-started.md` – detailed environment setup, workflows, and a minimal C ingestion example.
- `docs/architecture.md` – how LMDB, note blocks, metadata, and indexes fit together.
- `docs/api.md` – tour of the public C API surfaces.
- `docs/cli.md` – comprehensive CLI command reference.
- `docs/bindings/index.md` – pointers for C, Rust, and Swift consumers.

### Render the docs as a book

The `docs/book` directory contains an [mdBook](https://rust-lang.github.io/mdBook/) configuration
that stitches all documentation together into a browsable site:

```bash
cargo install mdbook        # once, if you don't already have it
mdbook serve docs/book --open
```

The `serve` task watches for file changes and rebuilds automatically. Use `mdbook build docs/book`
to emit static HTML under `docs/book/build/`.

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
	query [-k 42] [-k 1337] [-l 42]
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
