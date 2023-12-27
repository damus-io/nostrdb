{ pkgs ? import <nixpkgs> {} }:
with pkgs;
mkShell {
  buildInputs = [ autoreconfHook flatbuffers flatcc gdb pkg-config ];

  LIBCLANG_PATH="${llvmPackages.libclang}/lib";
}
