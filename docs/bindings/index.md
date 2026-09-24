# Language Bindings

nostrdb ships generated bindings for C (flatcc), Rust, and Swift to help
integrate metadata/profile schemas into higher-level applications. All bindings
live under `src/bindings/` and are regenerated from `schemas/*.fbs`.

| Language | Location | Status | Notes |
| --- | --- | --- | --- |
| C | `src/bindings/c/*.h` | Stable | FlatCC-generated builders/readers for profile + metadata schemas |
| Rust | `src/bindings/rust/*.rs` | Experimental | Plain module files suitable for inclusion in a workspace |
| Swift | `src/bindings/swift/*.swift` | Experimental | Swift structs/enums emitted by `flatc` |

## Regenerating bindings

```bash
# Generate everything
make bindings

# or target a single language
make bindings-c
make bindings-rust
make bindings-swift
```

The `Makefile` invokes `flatcc` for C headers and `flatc` for Rust/Swift. Ensure
both tools are installed (they are included in `shell.nix`).

## C bindings

Files:

- `flatbuffers_common_{builder,reader}.h`
- `profile_{builder,reader,verifier,json_parser}.h`
- `meta_{builder,reader,verifier,json_parser}.h`

Usage tips:

- Include the headers alongside `nostrdb.h` and link against the libflatcc runtime.
- Builders let you emit profile/metadata objects that match `ndb_profile` or
  `ndb_meta` flatbuffers; readers help decode cached blobs from LMDB.
- The CLI’s `profile` command shows the raw flatbuffer output you can feed into
  these readers.

Example:

```c
#include "src/bindings/c/profile_reader.h"

void dump_profile(const void *buf) {
    if (!ndb_profile_verify_as_root(buf, 1024)) return;
    const struct ndb_Profile_table *profile = ndb_Profile_as_root(buf);
    printf("name=%s\n", ndb_Profile_name(profile));
}
```

## Rust bindings

Files: `src/bindings/rust/ndb_profile.rs`, `src/bindings/rust/ndb_meta.rs`.

- Generated with `flatc --rust`.
- The modules use the `flatbuffers` crate’s runtime traits; add `flatbuffers = "23"`
  (or compatible) to your `Cargo.toml`.
- Include them in your crate via `mod ndb_profile;` / `mod ndb_meta;` and re-export
  types as needed.
- Pair them with FFI bindings to the core C API (e.g., via `bindgen`) for a
  full Rust client.

## Swift bindings

Files: `src/bindings/swift/NdbProfile.swift`, `src/bindings/swift/NdbMeta.swift`.

- Generated with `flatc --swift`.
- Designed to drop into an Xcode project or Swift Package.
- You will still interact with the C library via a bridging header or module map
  (e.g., `module nostrdb`); the Swift bindings only cover the FlatBuffers data.

## Versioning considerations

- Regenerate bindings whenever `schemas/*.fbs` change to keep readers/builders in
  sync with the on-disk data.
- Track the nostrdb commit hash alongside the generated files in downstream
  projects to avoid schema mismatches.
- If you add new schemas, update the `Makefile` rules and this document so the
  tooling remains discoverable.
