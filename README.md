
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

## Getting Started (Embedding)

```
git clone https://github.com/damus-io/nostrdb.git
cd nostrdb
git submodule update --init --recursive
make lib ndb
```

The build generates `libnostrdb.a` alongside the `ndb` CLI, reusing headers under
`src/` and vendored dependencies in `deps/`. Link your application against the
static archive and the libraries nostrdb relies on:

```
cc app.c libnostrdb.a \
  -Isrc -Ideps/lmdb -Ideps/secp256k1/include \
  -llmdb -lsecp256k1 -lpthread -lzstd
```

`/var/lib/nostrdb` is a convenient default path for the LMDB environment, but any
empty directory will do:

```
mkdir -p /var/lib/nostrdb
```

## API

The API is *very* unstable. nostrdb is in heavy development mode so don't
expect any of the interfaces to be stable at this time.

### Minimal C Example

```c
#include "nostrdb.h"

int main(void) {
    struct ndb *db = NULL;
    struct ndb_config cfg;

    ndb_default_config(&cfg);
    ndb_config_set_flags(&cfg, NDB_FLAG_SKIP_NOTE_VERIFY);

    if (ndb_init(&db, "/var/lib/nostrdb", &cfg) != 0)
        return 1;

    // Replace "..." with valid hex: id/pubkey (32 bytes), sig (64 bytes)
    const char *ldjson =
        "{\"id\":\"...\",\"pubkey\":\"...\",\"kind\":1,\"content\":\"hello\","
        "\"created_at\":1700000000,\"tags\":[],\"sig\":\"...\"}\n";

    if (ndb_process_events(db, ldjson, strlen(ldjson)) != 0)
        goto cleanup;

    struct ndb_txn txn;
    if (ndb_begin_query(db, &txn) != 0)
        goto cleanup;

    struct ndb_filter filter;
    ndb_filter_init(&filter);
    ndb_filter_start_field(&filter, NDB_FILTER_KINDS);
    ndb_filter_add_int_element(&filter, 1);
    ndb_filter_end_field(&filter);
    ndb_filter_end(&filter);

    struct ndb_query_result results[8];
    int count = 0;

    if (ndb_query(&txn, &filter, 1, results, 8, &count) == 0) {
        for (int i = 0; i < count; i++) {
            struct ndb_note *note = results[i].note;
            printf("created_at=%u content=%.*s\n",
                   ndb_note_created_at(note),
                   ndb_note_content_length(note),
                   ndb_note_content(note));
        }
    }

    ndb_filter_destroy(&filter);
    ndb_end_query(&txn);

cleanup:
    ndb_destroy(db);
    return 0;
}
```

### Configuration

`struct ndb_config` lets you tune ingest throughput, disk usage, and extensions.

| Setter | Purpose |
| --- | --- |
| `ndb_default_config` | Populate defaults (signature verification, fulltext, note blocks, 2MB writer scratch buffer, 16 GiB mapsize). |
| `ndb_config_set_flags` | Toggle runtime features: skip verification (`NDB_FLAG_SKIP_NOTE_VERIFY`), disable fulltext (`NDB_FLAG_NO_FULLTEXT`), disable parsed blocks (`NDB_FLAG_NO_NOTE_BLOCKS`), keep stats off (`NDB_FLAG_NO_STATS`), and more. |
| `ndb_config_set_ingest_threads` | Control the number of concurrent JSON ingest workers. |
| `ndb_config_set_writer_scratch_buffer_size` | Grow beyond 2MB when storing large events; shrink to save RAM when ingesting tiny notes. |
| `ndb_config_set_mapsize` | Increase the LMDB map when expecting many events (must be a multiple of the OS page size). |
| `ndb_config_set_ingest_filter` | Register a callback that accepts, rejects, or skips validation for each parsed note. |
| `ndb_config_set_subscription_callback` | Receive `ndb_subscribe` notifications when queried events arrive. |

### Event Ingestion

Use `ndb_process_event` for single JSON events or `ndb_process_events` for
line-delimited JSON (LDJSON) blobs. Both parse, verify (unless disabled), and
store notes into LMDB. `ndb_process_event_with` and
`ndb_process_events_with` accept an `ndb_ingest_meta` struct so you can track the
relay or client that supplied the data. On Unix-like systems,
`ndb_process_events_stream` streams from `FILE *` descriptors.

When ingesting trusted data, `NDB_FLAG_SKIP_NOTE_VERIFY` avoids signature
checks. Provide `ndb_ingest_filter` hooks to veto malformed or undesired events
before they commit to disk.

### Structured Queries

Queries execute inside a read transaction (`struct ndb_txn`) that you open with
`ndb_begin_query` and close with `ndb_end_query`. Filters describe the NIP-01
constraints to apply.

```c
struct ndb_filter filter;
ndb_filter_init(&filter);
ndb_filter_start_field(&filter, NDB_FILTER_IDS);
ndb_filter_add_id_element(&filter, some_id32);
ndb_filter_end_field(&filter);

ndb_filter_start_tag_field(&filter, 't');
ndb_filter_add_str_element(&filter, "nosql");
ndb_filter_end_field(&filter);

ndb_filter_end(&filter);
```

Populate filters manually, reuse them across transactions, or call
`ndb_filter_from_json` to load raw JSON filter objects using a caller-supplied
buffer. Execute the query via `ndb_query(txn, filters, num_filters, results,
capacity, &count)`; results reference in-memory notes valid until the
transaction ends.

### Fulltext Search

nostrdb maintains an LMDB-backed inverted index (if fulltext is enabled) that
`ndb_text_search` and `ndb_text_search_with` query. Configure ordering and result
limits with `ndb_default_text_search_config`, `ndb_text_search_config_set_order`,
and `ndb_text_search_config_set_limit`. When needed, pass a filter to
`ndb_text_search_with` to combine keyword search with structured constraints.

### Reading Data

The note accessor helpers expose raw content, metadata, and parsed structures:

* `ndb_note_content`, `ndb_note_content_length`
* `ndb_note_created_at`, `ndb_note_kind`
* `ndb_note_id`, `ndb_note_pubkey`, `ndb_note_sig`
* `ndb_note_tags`, `ndb_tags_iterate_start`, `ndb_tags_iterate_next`,
  `ndb_iter_tag_str`
* `ndb_note_relay_iterate_start`, `ndb_note_relay_iterate_next`,
  `ndb_note_relay_iterate_close`

Parsed block data provides structured hashtags, mentions, URLs, and invoices:

```c
struct ndb_blocks *blocks =
    ndb_get_blocks_by_key(ndb, &txn, results[i].note_id);

if (blocks) {
    struct ndb_block_iterator it;
    ndb_blocks_iterate_start(ndb_note_content(note), blocks, &it);

    struct ndb_block *block;
    while ((block = ndb_blocks_iterate_next(&it))) {
        if (ndb_get_block_type(block) == BLOCK_HASHTAG) {
            struct ndb_str_block *tag = ndb_block_str(block);
            printf("#%.*s\n", (int)ndb_str_block_len(tag), ndb_str_block_ptr(tag));
        }
    }

    ndb_blocks_free(blocks);
}
```

Disable block extraction entirely with `NDB_FLAG_NO_NOTE_BLOCKS` when the feature
is unused.

### Creating Events

`struct ndb_builder` helps construct, sign, and ingest new notes:

```c
unsigned char buf[2048];
struct ndb_builder builder;
struct ndb_note *note = NULL;

ndb_builder_init(&builder, buf, sizeof(buf));
ndb_builder_set_pubkey(&builder, pubkey32);
ndb_builder_set_kind(&builder, 1);
ndb_builder_set_created_at(&builder, time(NULL));
ndb_builder_set_content(&builder, "hello nostrdb", -1);

ndb_builder_new_tag(&builder);
ndb_builder_push_tag_str(&builder, "p", -1);
ndb_builder_push_tag_id(&builder, other_pubkey32);

struct ndb_keypair keypair;
ndb_decode_key("nsec1...", &keypair);
ndb_builder_finalize(&builder, &note, &keypair);

// Convert to JSON and ingest
char json_buf[4096];
int json_len = ndb_note_json(note, json_buf, sizeof(json_buf));
if (json_len > 0)
    ndb_process_event(db, json_buf, json_len);
```

Supporting functions include `ndb_calculate_id`, `ndb_sign_id`,
`ndb_create_keypair`, and `ndb_note_verify`, which wrap the bundled secp256k1
implementation.

### Subscriptions

Long-lived consumers can call `ndb_subscribe` to register filters and receive
updates. Deliveries arrive through either `ndb_wait_for_notes` (blocking) or
`ndb_poll_for_notes` (non-blocking), returning note IDs for follow-up queries.
Couple this with `ndb_config_set_subscription_callback` to inject per-note hooks.

### Language Bindings

`make bindings` regenerates higher level bindings using `flatc` and `flatcc`:

* `src/bindings/c/*`
* `src/bindings/rust/ndb_profile.rs`, `src/bindings/rust/ndb_meta.rs`
* `src/bindings/swift/NdbProfile.swift`, `src/bindings/swift/NdbMeta.swift`

Include these generated files in your host project. Regenerate them whenever the
schemas in `schemas/` change.

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

## Development

Run `make test` for sanitizer-enabled tests. The repository vendors LMDB,
secp256k1, flatcc, and CCAN modules under `deps/` to keep reproducible builds.
