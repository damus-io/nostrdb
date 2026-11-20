# Architecture

nostrdb is an embeddable storage engine tailored for nostr events. It adopts the
LMDB single-writer/multi-reader model and layers a custom in-memory note format,
metadata tables, and indexes on top of LMDB key/value pairs. This document
summarizes the moving pieces so you can reason about performance and extend the
system safely.

## High-level data flow

```
 JSON event --> parser/builder --> packed note + metadata --> LMDB buckets
                                                   |
                                    derived indexes (tags, relays, text, ...)
```

1. **Ingestion** – `ndb_process_event*` (see `src/nostrdb.c`) parses JSON into
   a compact `struct ndb_note` using cursor-backed builders (`struct ndb_builder`
   in `src/nostrdb.h`). Optional ingest metadata (relay, client) gets recorded.
2. **Validation** – Signatures are checked via `libsecp256k1` unless
   `NDB_FLAG_SKIP_NOTE_VERIFY` is set in the config.
3. **Persistence** – The packed note, associated metadata (`struct ndb_note_meta`),
   and derived indexes are written into an LMDB environment using the single writer thread.
4. **Query** – Readers open LMDB read transactions (`struct ndb_txn`), memory-map note
   blobs, and evaluate filters or full-text queries without copying data out of LMDB.

## LMDB layout

The database uses multiple LMDB databases (`enum ndb_dbs` in `src/nostrdb.h`), the most
important being:

- `NDB_DB_NOTE` – primary storage for packed notes
- `NDB_DB_META` – note metadata (reactions, counts)
- `NDB_DB_PROFILE` / `NDB_DB_PROFILE_PK` – cached profiles and pubkey index
- `NDB_DB_NOTE_TEXT` – full-text search index
- `NDB_DB_NOTE_KIND`, `NDB_DB_NOTE_TAGS`, `NDB_DB_NOTE_PUBKEY[_KIND]` – secondary indexes
- `NDB_DB_NOTE_RELAYS` / `NDB_DB_NOTE_RELAY_KIND` – relay tracking

All buckets live inside the same LMDB environment and benefit from LMDB’s MVCC
semantics: readers see a consistent view, the single writer thread avoids locks, and
data is accessed by memory-mapping the LMDB pages.

## Packed notes & metadata

`struct ndb_note` packs the event ID, pubkey, signature, created-at, kind, tags, and
content into cursor-managed buffers (see `struct ndb_builder`). Strings are either
zero-copy references or 32-byte IDs flagged with `NDB_PACKED_STR`/`NDB_PACKED_ID`.

Metadata lives alongside the note as a table of 16-byte entries
(`struct ndb_note_meta_entry`, documented in `docs/metadata.md`). Each entry stores:

- a type (counts, reactions, thread state, custom odd-numbered tags)
- an 8-byte payload (counts or offsets into a data table)
- optional aux data (e.g., total reaction counts)

The format keeps frequently updated counters (direct replies, quotes, reactions)
in-place, enabling atomic updates without decoding/recoding blobs.

## Ingestion pipeline

1. **Parsing** – Notes are parsed via `ndb_note_from_json` or the CLI’s streaming
   helpers. The parser feeds an `ndb_builder` that writes into scratch buffers sized
   by `ndb_config_set_writer_scratch_buffer_size`.
2. **Filtering** – `ndb_config_set_ingest_filter` allows applications to inspect
   notes before they hit disk (accept/reject/skip validation).
3. **Metadata augmentation** – Reactions, relay sightings, and thread stats are
   updated via metadata builder helpers (`ndb_note_meta_builder_*`).
4. **Index maintenance** – As the single writer thread commits, it updates the
   secondary indexes (tag, relay, kind, pubkey, full-text) and persists metadata.

Because LMDB enforces a single writer, nostrdb runs the ingestion path on a
dedicated thread. Reader-heavy workloads scale because reads are lock-free and
operate on memory-mapped pages.

## Query path

Readers call `ndb_begin_query` to open an LMDB read transaction and construct one
or more filters (`struct ndb_filter`). Filters can match IDs, authors, kinds,
tags (`#e`, `#p`, custom), time ranges, search tokens, relays, or custom callbacks.

`ndb_query` takes a filter array, executes it against indexes, and returns a list
of `struct ndb_query_result` entries with direct `struct ndb_note *` pointers; no
copying occurs because LMDB pages remain mapped for the lifetime of the transaction.

Full-text searches (`ndb_text_search*`) use the same reader transactions but consult
`NDB_DB_NOTE_TEXT` for ranked matches. Results can be fed through filters to enforce
kind/author/relay constraints.

## Blocks, content parsing, and relays

`src/content_parser.c` and related block structures (`struct ndb_blocks`,
`struct ndb_block_iterator`) parse note content into blocks (text, mentions,
invoices, bech32 references) for rich rendering. Parsed blocks are cached in
`NDB_DB_NOTE_BLOCKS` to avoid reparsing long-form notes.

Relays are tracked with iterators (`struct ndb_note_relay_iterator`) that read from
`NDB_DB_NOTE_RELAYS` and can be used both by the CLI (`ndb note-relays`) and
embedders implementing gossip logic.

## CLI + bindings

The `ndb` CLI (`ndb.c`) exercises each subsystem:

- `stat` – reads global LMDB stats (`ndb_stat`)
- `query` – builds filters from CLI flags and prints JSON notes
- `import` – streams line-delimited JSON into `ndb_process_events`
- `profile`, `note-relays`, `print-*` – showcase profile cache, relay iterators,
  search indexes, and metadata printing helpers

Bindings in `src/bindings/{c,rust,swift}` expose the same primitives to higher-level
languages. They are generated from `schemas/*.fbs` via `flatcc`/`flatc`.

## Extending nostrdb safely

- Prefer new LMDB buckets over overloading existing ones; update `enum ndb_dbs` and
  provide readable names via `ndb_db_name`.
- Keep ingestion work inside the writer thread. Use metadata builders for counters to
  avoid race conditions.
- Ensure filters are always finalized (`ndb_filter_end`) before executing queries.
- When adjusting metadata formats, bump version fields in `struct ndb_note_meta` so
  older readers can detect incompatibilities.

For concrete API references, see `docs/api.md`. For CLI usage patterns, refer to
`docs/cli.md`.
