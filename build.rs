// build.rs
use cc::Build;
use std::env;
use std::path::PathBuf;

fn secp256k1_build() {
    // Actual build
    let mut base_config = cc::Build::new();
    base_config
        .include("deps/secp256k1/")
        .include("deps/secp256k1/include")
        .include("deps/secp256k1/src")
        .flag_if_supported("-Wno-unused-function") // some ecmult stuff is defined but not used upstream
        .flag_if_supported("-Wno-unused-parameter") // patching out printf causes this warning
        //.define("SECP256K1_API", Some(""))
        .define("ENABLE_MODULE_ECDH", Some("1"))
        .define("ENABLE_MODULE_SCHNORRSIG", Some("1"))
        .define("ENABLE_MODULE_EXTRAKEYS", Some("1"))
        //.define("ENABLE_MODULE_ELLSWIFT", Some("0"))
        // upstream sometimes introduces calls to printf, which we cannot compile
        // with WASM due to its lack of libc. printf is never necessary and we can
        // just #define it away.
        .define("printf(...)", Some(""));

    //if cfg!(feature = "lowmemory") {
    //    base_config.define("ECMULT_WINDOW_SIZE", Some("4")); // A low-enough value to consume negligible memory
    //    base_config.define("ECMULT_GEN_PREC_BITS", Some("2"));
    //} else {
    //    base_config.define("ECMULT_GEN_PREC_BITS", Some("4"));
    //    base_config.define("ECMULT_WINDOW_SIZE", Some("15")); // This is the default in the configure file (`auto`)
    //}

    //base_config.define("USE_EXTERNAL_DEFAULT_CALLBACKS", Some("1"));
    //#[cfg(feature = "recovery")]
    //base_config.define("ENABLE_MODULE_RECOVERY", Some("1"));

    // WASM headers and size/align defines.
    if env::var("CARGO_CFG_TARGET_ARCH").unwrap() == "wasm32" {
        base_config.include("wasm/wasm-sysroot").file("wasm/wasm.c");
    }

    // secp256k1
    base_config
        .file("deps/secp256k1/contrib/lax_der_parsing.c")
        .file("deps/secp256k1/src/precomputed_ecmult_gen.c")
        .file("deps/secp256k1/src/precomputed_ecmult.c")
        .file("deps/secp256k1/src/secp256k1.c");

    if base_config.try_compile("libsecp256k1.a").is_err() {
        // Some embedded platforms may not have, eg, string.h available, so if the build fails
        // simply try again with the wasm sysroot (but without the wasm type sizes) in the hopes
        // that it works.
        base_config.include("wasm/wasm-sysroot");
        base_config.compile("libsecp256k1.a");
    }
}

fn main() {
    // Compile the C file
    let mut build = Build::new();

    build
        .files([
            "nostrdb.c",
            "sha256.c",
            "bech32.c",
            "deps/flatcc/src/runtime/json_parser.c",
            "deps/flatcc/src/runtime/verifier.c",
            "deps/flatcc/src/runtime/builder.c",
            "deps/flatcc/src/runtime/emitter.c",
            "deps/flatcc/src/runtime/refmap.c",
            "deps/lmdb/mdb.c",
            "deps/lmdb/midl.c",
        ])
        .include("deps/lmdb")
        .include("deps/flatcc/include")
        .include("deps/secp256k1/include")
        // Add other include paths
        //.flag("-Wall")
        .flag("-Wno-misleading-indentation")
        .flag("-Wno-unused-function")
        //.flag("-Werror")
        //.flag("-g")
        .compile("libnostrdb.a");

    secp256k1_build();

    // Re-run the build script if any of the C files or headers change
    for file in &[
        "src/nostrdb.c",
        "src/sha256.c",
        "src/bech32.c",
        // Add all your C source files here
        "include/nostrdb.h",
        "include/sha256.h",
        // Add all your header files here
    ] {
        println!("cargo:rerun-if-changed={}", file);
    }

    //println!("cargo:rustc-link-search=native=deps/lmdb");
    //println!("cargo:rustc-link-lib=static=lmdb");
    //println!("cargo:rustc-link-search=native=deps/secp256k1/.libs");
    println!("cargo:rustc-link-lib=secp256k1");

    // Print out the path to the compiled library
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    println!("cargo:rustc-link-search=native={}", out_path.display());
    println!("cargo:rustc-link-lib=static=nostrdb");

    // The bindgen::Builder is the main entry point to bindgen, and lets you build up options for
    // the resulting bindings.
    let bindings = bindgen::Builder::default()
        .header("nostrdb.h")
        .generate()
        .expect("Unable to generate bindings");

    // Write the bindings to the $OUT_DIR/bindings.rs file.
    bindings
        .write_to_file("src/bindings.rs")
        .expect("Couldn't write bindings!");
}
