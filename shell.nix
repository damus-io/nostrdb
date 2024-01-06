{ pkgs ? import <nixpkgs> {} }:
with pkgs;
mkShell {
  buildInputs = [ autoreconfHook flatbuffers flatcc pkg-config ];

  LIBCLANG_PATH="${llvmPackages.libclang}/lib";
}
