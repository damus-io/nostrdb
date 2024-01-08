{ pkgs ? import <nixpkgs> {} }:
with pkgs;
mkShell {
  buildInputs = [ autoreconfHook flatbuffers flatcc pkg-config flamegraph ];

  LIBCLANG_PATH="${llvmPackages.libclang}/lib";
}
