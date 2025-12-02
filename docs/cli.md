# `ndb` CLI Guide

`ndb` is a thin CLI wrapper around the nostrdb library. It is useful for smoke
tests, debugging indexes, and inspecting fixtures. Build it with `make ndb`
(or plain `make`) and run the binary from the repo root.

```
usage: ndb [--skip-verification] [-d db_dir] <command>
```

- `--skip-verification` – disables signature checks during ingestion (often used
  with fixtures or during development).
- `-d <db_dir>` – points the CLI at a specific LMDB environment (defaults to `.`).

## Commands

### `stat`

Prints per-database counts and byte usage (`ndb_stat`). Useful for monitoring
index growth and verifying compaction.

### `query`

Executes an NIP-01 style filter. Options can be combined; most flags accept
multiple occurrences.

| Option | Description |
| --- | --- |
| `-k`, `--kind <int>` | Add a `kinds` entry |
| `-a`, `--author <hex>` | Add an author pubkey |
| `-i`, `--id <hex>` | Exact note IDs |
| `-e <hex>` | `#e` tag references |
| `-q <hex>` | `#q` tag references |
| `-t <value>` | `#t` tag (string) |
| `--relay`, `-r <url>` | Relay filter |
| `--search`, `-S <term>` | Full-text query string |
| `--since`, `-s <unix>` | Lower bound timestamp |
| `--until`, `-u <unix>` | Upper bound timestamp |
| `--limit`, `-l <n>` | Max results |
| `--notekey <u64>` | Direct note primary key lookup (bypasses filters) |

Examples:

```bash
# Latest text notes containing "nostrdb"
./ndb query --kind 1 --search nostrdb --limit 5

# Notes from one author referencing a thread id
./ndb query --author deadbeef... --e cafebabe... --limit 20

# Time-bounded relay-specific query
./ndb query --relay wss://relay.damus.io --kind 1 --since 1700000000 --until 1700100000
```

> **Note:** Older help text mentions a `search` command. The current binary
> implements full-text search via `query --search ...`.

### `import <path|-`

Imports line-delimited JSON. Pass `-` to read from stdin. Combine with
`--skip-verification` during local testing:

```bash
./ndb --skip-verification import testdata/many-events.json
```

### `profile <hex-pubkey>`

Looks up a cached profile, prints the raw flatbuffer payload in hex, and emits
the profile database key to stderr. Handy for verifying profile syncs.

### `note-relays <hex-note-id>`

Translates a note ID into its LMDB key and prints every relay that has delivered
that note. The command relies on `ndb_note_relay_iterator`.

### `print-note-metadata`

Walks every note’s metadata table and prints entries — useful for debugging
reaction counts and thread stats. Combined with scripts you can analyze reaction
distributions.

### Index debug helpers

Each helper opens a read transaction and dumps raw index keys:

- `print-search-keys`
- `print-kind-keys`
- `print-tag-keys`
- `print-relay-kind-index-keys`
- `print-author-kind-index-keys`

## Typical workflows

1. **Bootstrap database**
   ```bash
   make testdata/many-events.json
   ./ndb --skip-verification import testdata/many-events.json
   ./ndb stat
   ```
2. **Inspect a failing query**
   ```bash
   ./ndb query --kind 1 --author deadbeef...
   ./ndb print-tag-keys | head
   ```
3. **Validate relay coverage**
   ```bash
   ./ndb note-relays <note-id-hex>
   ```

Use `strace`, `perf`, or `valgrind` around these commands if you need deeper
diagnostics. For more automation, script around `ndb` by reading/writing JSON on
stdin/stdout.
