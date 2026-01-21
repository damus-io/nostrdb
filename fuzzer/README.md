# how to fuzz nostrdb with afl++

1. Install Clang 14+
2. Install LLVM 14+
3. Install AFL++
4. Then...
    AFL_USE_ASAN=1 afl-clang-lto -flto=full -fsanitize=address -Wall -Wno-unused-function -Werror -O2 -g -Ideps/secp256k1/include -Ideps/lmdb -Ideps/flatcc/include fuzzer.c nostrdb.c sha256.c deps/flatcc/src/runtime/json_parser.c deps/flatcc/src/runtime/builder.c deps/flatcc/src/runtime/emitter.c deps/flatcc/src/runtime/refmap.c deps/lmdb/liblmdb.a deps/secp256k1/.libs/libsecp256k1.a -o fuzzer
    create an input directory drop some sort of starting corpus. i chose blns.txt because it rocks.
5. create an input directory for your starting corpus. i chose blns.txt because it's awesome.    
6. afl-fuzz -i $input -o $output -- ./fuzzer
