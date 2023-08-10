CFLAGS = -Wall -Wno-unused-function -Werror -O2 -g -Ideps/secp256k1/include -Ideps/lmdb -Ideps/flatcc/include
HEADERS = sha256.h nostrdb.h cursor.h hex.h jsmn.h config.h sha256.h random.h memchr.h
SRCS = nostrdb.c sha256.c 
LDS = $(SRCS) $(ARS)
DEPS = $(SRCS) $(HEADERS) $(ARS)
ARS = deps/lmdb/liblmdb.a deps/secp256k1/.libs/libsecp256k1.a
LMDB_VER=0.9.31
FLATCC_VER=0.6.1
PREFIX ?= /usr/local
SUBMODULES = deps/secp256k1
C_BINDINGS_PROFILE=bindings/c/profile_builder.h bindings/c/profile_reader.h bindings/c/profile_verifier.h bindings/c/profile_json_parser.h
C_BINDINGS_COMMON=bindings/c/flatbuffers_common_builder.h bindings/c/flatbuffers_common_reader.h 
C_BINDINGS=$(C_BINDINGS_COMMON) $(C_BINDINGS_PROFILE)
BINDINGS=bindings

lib: benches test

all: lib bindings

bindings: bindings-swift bindings-c

check: test
	rm -rf testdata/db/*.mdb
	./test

clean:
	rm -rf test bench

benches: bench bench-ingest

distclean: clean
	rm -rf deps

tags:
	ctags *.c *.h

configurator: configurator.c
	$(CC) $< -o $@

config.h: configurator
	./configurator > $@

bindings-c: $(C_BINDINGS)

bindings/%:
	@mkdir -p $@

bindings/c/profile_builder.h: schemas/profile.fbs bindings/c
	flatcc --builder $<
	@mv profile_builder.h $@

bindings/c/profile_verifier.h bindings/c/profile_reader.h: schemas/profile.fbs bindings/c
	flatcc --verifier $<
	@mv profile_verifier.h profile_reader.h bindings/c

bindings/c/flatbuffers_common_reader.h: bindings/c
	flatcc --common_reader
	@mv flatbuffers_common_reader.h $@

bindings/c/flatbuffers_common_builder.h: bindings/c
	flatcc --common_builder
	@mv flatbuffers_common_builder.h $@

bindings/c/profile_json_parser.h: schemas/profile.fbs bindings/c
	flatcc --json-parser $<
	@mv profile_json_parser.h bindings/c

bindings-swift: bindings/swift/NdbProfile.swift

bindings/swift/NdbProfile.swift: schemas/profile.fbs bindings/swift
	flatc --swift $<
	@mv profile_generated.swift $@

deps/.dir:
	@mkdir -p deps
	touch deps/.dir

deps/LMDB_$(LMDB_VER).tar.gz: deps/.dir
	curl -L https://github.com/LMDB/lmdb/archive/refs/tags/LMDB_$(LMDB_VER).tar.gz -o $@

deps/flatcc_$(FLATCC_VER).tar.gz: deps/.dir
	curl -L https://github.com/dvidelabs/flatcc/archive/refs/tags/v0.6.1.tar.gz -o $@

deps/flatcc/include/flatcc/flatcc.h: deps/flatcc_$(FLATCC_VER).tar.gz deps/.dir
	tar xf $<
	rm -rf deps/flatcc
	mv flatcc-$(FLATCC_VER) deps/flatcc
	touch $@

deps/lmdb/lmdb.h: deps/LMDB_$(LMDB_VER).tar.gz deps/.dir
	tar xf $<
	rm -rf deps/lmdb
	mv lmdb-LMDB_$(LMDB_VER)/libraries/liblmdb deps/lmdb
	rm -rf lmdb-LMDB_$(LMDB_VER)
	touch $@

deps/secp256k1/.git: deps/.dir
	@devtools/refresh-submodules.sh $(SUBMODULES)

deps/secp256k1/include/secp256k1.h: deps/secp256k1/.git

deps/secp256k1/configure: deps/secp256k1/.git
	cd deps/secp256k1; \
	./autogen.sh

deps/secp256k1/.libs/libsecp256k1.a: deps/secp256k1/config.log
	cd deps/secp256k1; \
	make -j libsecp256k1.la

deps/secp256k1/config.log: deps/secp256k1/configure
	cd deps/secp256k1; \
	./configure --disable-shared --enable-module-ecdh --enable-module-schnorrsig --enable-module-extrakeys

deps/lmdb/liblmdb.a: deps/lmdb/lmdb.h
	$(MAKE) -C deps/lmdb liblmdb.a

bench: bench.c $(DEPS)
	$(CC) $(CFLAGS) bench.c $(LDS) -o $@

bench-ingest: bench-ingest.c $(DEPS)
	$(CC) $(CFLAGS) bench-ingest.c $(LDS) -o $@

testdata/db/.dir:
	@mkdir -p testdata/db
	touch testdata/db/.dir

test: test.c $(DEPS) testdata/db/.dir
	$(CC) $(CFLAGS) test.c $(LDS) -o $@

%.o: %.c
	$(CC) $(CFLAGS)

.PHONY: tags clean
