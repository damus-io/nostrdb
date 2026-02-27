// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "nostrdb",
    products: [
        .library(name: "Nostrdb", targets: ["Nostrdb"]),
    ],
    targets: [
        .target(
            name: "Nostrdb",
            path: ".",
            exclude: [
                "CMakeLists.txt",
                "Makefile",
                "LICENSE",
                "README.md",
                "TODO",
                "ndb.c",
                "test.c",
                "bench-ingest-many.c",
                "schemas",
                "testdata",
                "devtools",
                "docs",
                "shell.nix",
                "src/bindings",
                "src/configurator.c",
                "src/giftwrap.c",
            ],
            sources: [
                // nostrdb core
                "src/nostrdb.c",
                "src/block.c",
                "src/content_parser.c",
                "src/invoice.c",
                "src/nostr_bech32.c",
                "src/base64.c",
                "src/hmac_sha256.c",
                "src/hkdf_sha256.c",
                "src/nip44.c",
                "src/binmoji.c",
                "src/metadata.c",
                "src/sodium_shim.c",

                // bolt11
                "src/bolt11/bolt11.c",
                "src/bolt11/bech32.c",
                "src/bolt11/amount.c",
                "src/bolt11/hash_u5.c",

                // lmdb
                "deps/lmdb/mdb.c",
                "deps/lmdb/midl.c",

                // secp256k1
                "deps/secp256k1/src/secp256k1.c",
                "deps/secp256k1/src/precomputed_ecmult.c",
                "deps/secp256k1/src/precomputed_ecmult_gen.c",

                // flatcc runtime
                "deps/flatcc/src/runtime/builder.c",
                "deps/flatcc/src/runtime/emitter.c",
                "deps/flatcc/src/runtime/json_parser.c",
                "deps/flatcc/src/runtime/json_printer.c",
                "deps/flatcc/src/runtime/refmap.c",
                "deps/flatcc/src/runtime/verifier.c",

                // ccan
                "ccan/ccan/utf8/utf8.c",
                "ccan/ccan/tal/tal.c",
                "ccan/ccan/tal/str/str.c",
                "ccan/ccan/list/list.c",
                "ccan/ccan/mem/mem.c",
                "ccan/ccan/crypto/sha256/sha256.c",
                "ccan/ccan/take/take.c",

                // libsodium chacha20 (minimal)
                "deps/libsodium/src/libsodium/crypto_stream/chacha20/stream_chacha20.c",
                "deps/libsodium/src/libsodium/crypto_stream/chacha20/ref/chacha20_ref.c",
            ],
            publicHeadersPath: "src",
            cSettings: [
                .headerSearchPath("src"),
                .headerSearchPath("src/bolt11"),
                .headerSearchPath("deps/flatcc/include"),
                .headerSearchPath("deps/lmdb"),
                .headerSearchPath("deps/secp256k1/include"),
                .headerSearchPath("deps/secp256k1/src"),
                .headerSearchPath("deps/libsodium/src/libsodium/include"),
                .headerSearchPath("deps/libsodium/src/libsodium/include/sodium"),
                .headerSearchPath("deps/libsodium/src/libsodium/crypto_stream/chacha20"),
                .headerSearchPath("ccan"),
                .define("CCAN_TAL_NEVER_RETURN_NULL", to: "1"),
                .define("MDB_SHORT_SEMNAMES", to: "1"),
                .define("MDB_SEM_NAME_PREFIX", to: "\"group.com.damus\""),
                .define("ENABLE_MODULE_ECDH", to: "1"),
                .define("ENABLE_MODULE_EXTRAKEYS", to: "1"),
                .define("ENABLE_MODULE_SCHNORRSIG", to: "1"),
                .unsafeFlags(["-w"]),
            ]
        ),
    ]
)
