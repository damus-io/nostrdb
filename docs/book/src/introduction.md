# Introduction

nostrdb is an embeddable, LMDB-backed datastore for nostr events. This book stitches
together the project’s reference material so you can explore the storage design,
apis, and tooling without bouncing between Markdown files.

Use the chapters in the navigation sidebar to jump into the topic you need:

- **Getting Started**: prepare a development environment, build binaries, run tests, and
  ingest fixtures.
- **Architecture**: learn how packed notes, metadata tables, and LMDB buckets fit
  together.
- **API Tour**: browse the major public entry points in `src/nostrdb.h`.
- **CLI Guide**: discover everything the `ndb` tool can do.
- **Metadata**: deep dive into the note metadata format.
- **Language Bindings**: regenerate and consume the generated C/Rust/Swift bindings.

The book sources reuse the Markdown files stored under `docs/`, so contributions to
those files automatically appear here as well.
