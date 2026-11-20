# Getting Started

This guide walks through preparing a development environment, building the
library/CLI, loading the provided fixtures, and embedding nostrdb from C.

## 1. Prerequisites

nostrdb is developed primarily on Linux and macOS. You will need:

- A modern C11 compiler (GCC ≥12 or Clang ≥15 recommended)
- `make`, `pkg-config`, `git`, `curl`, and `zstd`
- Autotools (`autoconf`, `automake`, `libtool`) for building `libsecp256k1`
- Optional: [`nix`](https://nixos.org/) to reuse `shell.nix`, `flatc`/`flatcc` when regenerating schema bindings, and `perf` for profiling

The repository vendors most third-party sources (LMDB, flatcc, ccan) and ships a helper
script for refreshable submodules.

```bash
git clone https://github.com/damus-io/nostrdb.git
cd nostrdb
./devtools/refresh-submodules.sh deps/secp256k1
```

If you use Nix, run `nix-shell` to drop into an environment with the required toolchain.

## 2. Building

The default `make` target builds everything: the `ndb` CLI, `libnostrdb.a`, and
benchmarks. Common targets:

| Command | Purpose |
| --- | --- |
| `make ndb` | Build only the CLI |
| `make lib` | Build `libnostrdb.a` for embedding |
| `make bench` | Build the ingestion benchmark |
| `make run-bench` | Build fixtures and run the benchmark |
| `make clean` | Remove binaries/objects |
| `make distclean` | Also wipe downloaded deps |

The build automatically downloads LMDB/flatcc archives and compiles
`deps/secp256k1/.libs/libsecp256k1.a`. If that step fails, ensure autotools are
installed and rerun `./devtools/refresh-submodules.sh deps/secp256k1`.

### Sanitized tests

`make test` compiles the `test` binary with ASan/UBSan (see `Makefile:194-197`) and
places LMDB scratch data in `testdata/db/`. Run it regularly while making changes:

```bash
make testdata/db/.dir   # first time only
make test
```

### Sample data + CLI smoke test

```bash
# Download and inflate fixture sets (stored in testdata/)
make testdata/many-events.json

# Populate an empty LMDB environment (default dir: ./data)
./ndb --skip-verification import testdata/many-events.json

# Run a few commands
./ndb stat
./ndb query --search "nostrdb" --limit 5
./ndb profile <hex-pubkey>
```

Fixtures can also be streamed via `./ndb import -` if you pipe events from another
process.

## 3. Repository layout refresher

- `src/` – core library plus bindings (`src/bindings/{c,rust,swift}`)
- `docs/` – conceptual and reference material (what you're reading)
- `testdata/` – sample LMDB environments and JSON fixtures
- `schemas/` – flatbuffers definitions for metadata/profile data
- `devtools/` – helper scripts (e.g., `refresh-submodules.sh`)
- `shell.nix` – reproducible dev environment

## 4. Minimal C ingestion example

The snippet below opens a database, ingests a single event with verification disabled,
and executes a simple kind filter. Replace the abbreviated hex strings with valid
64-character nostr IDs/signatures/pubkeys before running it. Use it as a template for
experiments or integration tests.

```c
#include "nostrdb.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    struct ndb *ndb;
    struct ndb_config config;
    ndb_default_config(&config);
    ndb_config_set_flags(&config, NDB_FLAG_SKIP_NOTE_VERIFY); // sample data only
    ndb_config_set_mapsize(&config, 4ULL * 1024 * 1024 * 1024); // 4 GiB

    if (!ndb_init(&ndb, "./data", &config)) {
        fprintf(stderr, "failed to init nostrdb\n");
        return 1;
    }

    const char *event =
        "{\"id\":\"5d...\","
        "\"pubkey\":\"ab...\","
        "\"created_at\":1700000000,"
        "\"kind\":1,"
        "\"tags\":[],"
        "\"content\":\"hello nostrdb\","
        "\"sig\":\"cd...\"}";

    if (!ndb_process_event(ndb, event, (int)strlen(event))) {
        fprintf(stderr, "failed to ingest event\n");
        ndb_destroy(ndb);
        return 1;
    }

    struct ndb_filter filter;
    ndb_filter_init(&filter);
    ndb_filter_start_field(&filter, NDB_FILTER_KINDS);
    ndb_filter_add_int_element(&filter, 1);
    ndb_filter_end_field(&filter);
    ndb_filter_end(&filter);

    struct ndb_txn txn;
    if (!ndb_begin_query(ndb, &txn)) {
        fprintf(stderr, "failed to open read txn\n");
        ndb_destroy(ndb);
        return 1;
    }

    struct ndb_query_result results[8];
    int count = 0;
    if (!ndb_query(&txn, &filter, 1, results, 8, &count)) {
        fprintf(stderr, "query failed\n");
    } else {
        printf("found %d notes of kind 1\n", count);
    }

    ndb_end_query(&txn);
    ndb_filter_destroy(&filter);
    ndb_destroy(ndb);
    return 0;
}
```

Compile by linking against `libnostrdb.a` and LMDB/secp256k1 (see `Makefile` for the
necessary include/link flags).

## 5. Troubleshooting

- **`libsecp256k1` missing** – rerun `./devtools/refresh-submodules.sh deps/secp256k1`
  and ensure autotools are installed.
- **`ndb_init` returns false** – check filesystem permissions and whether the configured
  map size fits on disk; LMDB needs a file large enough to satisfy `ndb_config_set_mapsize`.
- **`ndb_process_event` fails** – unless you have valid signatures, set
  `NDB_FLAG_SKIP_NOTE_VERIFY` in development builds.
- **`query` returns zero rows** – confirm your filters are finalized (`ndb_filter_end`)
  and that you opened read transactions with `ndb_begin_query`.

Once you are comfortable with the basics, dive into `docs/architecture.md`,
`docs/api.md`, and `docs/cli.md` for deeper coverage.
