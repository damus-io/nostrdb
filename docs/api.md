# API Tour

This document summarizes the primary entry points exposed by `src/nostrdb.h`.
It is not an exhaustive reference but should help you find the right family of
functions for common tasks. See the header for full signatures and structure
definitions.

## Configuration & lifecycle

```c
struct ndb *db;
struct ndb_config config;
ndb_default_config(&config);
ndb_config_set_mapsize(&config, 1ULL << 34); // 16 GiB
ndb_config_set_flags(&config, NDB_FLAG_SKIP_NOTE_VERIFY);
ndb_config_set_ingest_threads(&config, 4);
ndb_config_set_ingest_filter(&config, my_filter, ctx);
ndb_config_set_subscription_callback(&config, my_sub_cb, ctx);
ndb_config_set_writer_scratch_buffer_size(&config, 4 * 1024 * 1024);

if (!ndb_init(&db, "./data", &config)) { /* handle error */ }
...
ndb_destroy(db);
```

- `ndb_default_config` – zeroes + sensible defaults.
- `ndb_config_set_*` – tune ingest threads, flags, LMDB map size, filters, subscription callbacks.
- `ndb_init` – opens/creates the LMDB environment at `dbdir`. Always pair with `ndb_destroy`.

## Ingestion helpers

- `ndb_process_event`, `ndb_process_events` – parse nostr JSON (single event or LDJSON batch).
- `ndb_process_event_with`, `ndb_process_events_with` – include relay/client metadata (`struct ndb_ingest_meta`).
- `ndb_process_events_stream` – stream reader-friendly variant used by the CLI.
- `ndb_ingest_meta_init` – convenience initializer for metadata.
- `ndb_process_client_event(s)` – legacy wrappers (prefer the `ndb_process_event*` family).

### Metadata builders

Metadata is created/updated via `ndb_note_meta_builder`:

```c
unsigned char buf[1024];
struct ndb_note_meta_builder builder;
ndb_note_meta_builder_init(&builder, buf, sizeof buf);

struct ndb_note_meta_entry *entry = ndb_note_meta_add_entry(&builder);
ndb_note_meta_reaction_set(entry, /*count=*/5, reaction);

struct ndb_note_meta *meta;
ndb_note_meta_build(&builder, &meta);
ndb_set_note_meta(db, note_id, meta);
```

Key functions:

- `ndb_note_meta_builder_init`, `ndb_note_meta_builder_resized`, `ndb_note_meta_build`
- `ndb_note_meta_add_entry`, `ndb_note_meta_builder_find_entry`
- Accessors such as `ndb_note_meta_counts_*`, `ndb_note_meta_reaction_set`, `ndb_note_meta_flags`
- `ndb_get_note_meta`, `ndb_note_meta_entries`, `ndb_note_meta_find_entry`

## Transactions & queries

Readers borrow LMDB transactions via `ndb_begin_query` / `ndb_end_query`.

```c
struct ndb_txn txn;
if (!ndb_begin_query(db, &txn)) { /* handle error */ }

struct ndb_filter filter;
ndb_filter_init(&filter);
ndb_filter_start_field(&filter, NDB_FILTER_KINDS);
ndb_filter_add_int_element(&filter, 1);
ndb_filter_end_field(&filter);
ndb_filter_end(&filter);

struct ndb_query_result results[32];
int count = 0;
ndb_query(&txn, &filter, 1, results, 32, &count);

for (int i = 0; i < count; i++) {
    struct ndb_note *note = results[i].note;
    printf("kind=%u content=%.*s\n",
           ndb_note_kind(note),
           ndb_note_content_length(note),
           ndb_note_content(note));
}

ndb_filter_destroy(&filter);
ndb_end_query(&txn);
```

Notable APIs:

- `ndb_filter_init`, `ndb_filter_init_with` – allocate filter buffers.
- `ndb_filter_start_field`, `ndb_filter_start_tag_field`, `ndb_filter_add_*` – build queries.
- `ndb_filter_matches*`, `ndb_filter_is_subset_of`, `ndb_filter_clone`, `ndb_filter_json`.
- `ndb_filter_from_json` – decode NIP-01 filters.
- `ndb_query` – execute filter arrays.
- `ndb_subscribe`, `ndb_wait_for_notes`, `ndb_poll_for_notes`, `ndb_unsubscribe` – pub/sub APIs driven by ingest callbacks.

### Text search

- `ndb_text_search` / `ndb_text_search_with` – ranked searches over `NDB_DB_NOTE_TEXT`.
- `ndb_default_text_search_config`, `ndb_text_search_config_set_order`, `ndb_text_search_config_set_limit` – configure sort order and result caps.

## Profiles, relays, and helpers

- `ndb_search_profile`, `ndb_search_profile_next`, `ndb_search_profile_end` – profile text search.
- `ndb_get_profile_by_pubkey`, `ndb_get_profile_by_key`, `ndb_get_profilekey_by_pubkey` – profile cache access.
- `ndb_get_notekey_by_id`, `ndb_get_note_by_id`, `ndb_get_note_by_key` – note lookups.
- `ndb_note_seen_on_relay`, `ndb_note_relay_iterate_start`, `ndb_note_relay_iterate_next` – relay tracking.
- `ndb_write_last_profile_fetch`, `ndb_read_last_profile_fetch` – per-pubkey sync bookkeeping.

Helper utilities:

- `ndb_create_keypair`, `ndb_decode_key`, `ndb_sign_id`, `ndb_calculate_id`, `ndb_note_verify`
- `ndb_parse_content`, `ndb_blocks_iterate_*`, `ndb_blocks_free` – block parser helpers for rendering.

## Note & tag inspection

Once you have `struct ndb_note *` pointers:

- `ndb_note_id`, `ndb_note_pubkey`, `ndb_note_sig`, `ndb_note_kind`, `ndb_note_created_at`
- `ndb_note_content`, `ndb_note_content_length`, `ndb_note_json`
- `ndb_note_tags`, `ndb_tags_iterate_start`, `ndb_tags_iterate_next`, `ndb_iter_tag_str`
- `ndb_note_meta_iterator`, `ndb_note_meta_entry_at`, `ndb_note_meta_reaction_str`

These functions operate on the packed, zero-copy note representation without heap
allocations.

## Error handling tips

- Most APIs return `int` (non-zero success). Check return codes and call
  `ndb_destroy` on failure to release LMDB handles.
- Filters must be finalized with `ndb_filter_end` before calling `ndb_query`.
- Reader transactions must be closed (`ndb_end_query`) before destroying the DB.

For deeper explanations of the storage layers see `docs/architecture.md`. For CLI
examples and manual test workflows refer to `docs/cli.md` and `docs/getting-started.md`.
