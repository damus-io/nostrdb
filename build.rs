// build.rs
use cc::Build;
use std::env;
use std::path::PathBuf;

fn main() {
    // Compile the C file
    Build::new()
        .files([
            "nostrdb.c",
            "sha256.c",
            "bech32.c",
            "deps/flatcc/src/runtime/json_parser.c",
            "deps/flatcc/src/runtime/verifier.c",
            "deps/flatcc/src/runtime/builder.c",
            "deps/flatcc/src/runtime/emitter.c",
            "deps/flatcc/src/runtime/refmap.c",
            // Add all your C source files here
            // For flatcc sources, you might want to iterate over an array of paths
        ])
        .include("deps/secp256k1/include")
        .include("deps/lmdb")
        .include("deps/flatcc/include")
        // Add other include paths
        //.flag("-Wall")
        .flag("-Wno-misleading-indentation")
        .flag("-Wno-unused-function")
        .flag("-Werror")
        .flag("-O2")
        .flag("-g")
        .compile("libnostrdb.a");

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

    println!("cargo:rustc-link-search=native=deps/lmdb");
    println!("cargo:rustc-link-lib=static=lmdb");
    println!("cargo:rustc-link-search=native=deps/secp256k1/.libs");
    println!("cargo:rustc-link-lib=static=secp256k1");

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
