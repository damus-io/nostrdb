CFLAGS = -Wall -Wno-misleading-indentation -Wno-unused-function -Werror -O2 -g -Isrc -Ideps/secp256k1/include -Ideps/lmdb -Ideps/flatcc/include -Isrc/bolt11/ -Iccan/ -Ideps/libsodium/src/libsodium/include/ -Ideps/cmark/src -Ideps/cmark/build/src -DCCAN_TAL_NEVER_RETURN_NULL=1
BOLT11_HDRS := src/bolt11/amount.h src/bolt11/bech32.h src/bolt11/bech32_util.h src/bolt11/bolt11.h src/bolt11/debug.h src/bolt11/error.h src/bolt11/hash_u5.h src/bolt11/node_id.h src/bolt11/overflows.h
CCAN_SRCS := ccan/ccan/utf8/utf8.c ccan/ccan/tal/tal.c ccan/ccan/tal/str/str.c ccan/ccan/list/list.c ccan/ccan/mem/mem.c ccan/ccan/crypto/sha256/sha256.c ccan/ccan/take/take.c
CCAN_HDRS := ccan/ccan/utf8/utf8.h ccan/ccan/container_of/container_of.h ccan/ccan/check_type/check_type.h ccan/ccan/str/str.h ccan/ccan/tal/str/str.h ccan/ccan/tal/tal.h ccan/ccan/list/list.h ccan/ccan/structeq/structeq.h ccan/ccan/typesafe_cb/typesafe_cb.h ccan/ccan/short_types/short_types.h ccan/ccan/mem/mem.h ccan/ccan/likely/likely.h ccan/ccan/alignof/alignof.h ccan/ccan/crypto/sha256/sha256.h ccan/ccan/array_size/array_size.h ccan/ccan/endian/endian.h ccan/ccan/take/take.h ccan/ccan/build_assert/build_assert.h ccan/ccan/cppmagic/cppmagic.h
HEADERS = deps/lmdb/lmdb.h deps/secp256k1/include/secp256k1.h src/nostrdb.h src/cursor.h src/hex.h src/jsmn.h src/config.h src/random.h src/memchr.h src/cpu.h src/nostr_bech32.h src/block.h src/str_block.h src/print_util.h $(C_BINDINGS) $(CCAN_HDRS) $(BOLT11_HDRS) $(CMARK_AR)
FLATCC_SRCS=deps/flatcc/src/runtime/json_parser.c deps/flatcc/src/runtime/verifier.c deps/flatcc/src/runtime/builder.c deps/flatcc/src/runtime/emitter.c deps/flatcc/src/runtime/refmap.c
BOLT11_SRCS = src/bolt11/bolt11.c src/bolt11/bech32.c src/bolt11/amount.c src/bolt11/hash_u5.c
SRCS = src/base64.c src/hmac_sha256.c src/hkdf_sha256.c src/nip44.c src/nostrdb.c src/invoice.c src/nostr_bech32.c src/content_parser.c src/block.c src/binmoji.c src/metadata.c src/markdown_parser.c src/nip23.c $(BOLT11_SRCS) $(FLATCC_SRCS) $(CCAN_SRCS)
LIBSODIUM_AR=deps/libsodium/src/libsodium/.libs/libsodium.a
LDS = $(OBJS) $(ARS) 
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS) $(HEADERS) $(ARS)
CMARK_AR=deps/cmark/build/src/libcmark.a
ARS = deps/lmdb/liblmdb.a deps/secp256k1/.libs/libsecp256k1.a $(LIBSODIUM_AR) $(CMARK_AR)
LMDB_VER=0.9.31
FLATCC_VER=05dc16dc2b0316e61063bb1fc75426647badce48
PREFIX ?= /usr/local
SUBMODULES = deps/secp256k1 deps/cmark
BINDINGS=src/bindings
C_BINDINGS_PROFILE=$(BINDINGS)/c/profile_builder.h $(BINDINGS)/c/profile_reader.h $(BINDINGS)/c/profile_verifier.h $(BINDINGS)/c/profile_json_parser.h
C_BINDINGS_META=$(BINDINGS)/c/meta_builder.h $(BINDINGS)/c/meta_reader.h $(BINDINGS)/c/meta_verifier.h $(BINDINGS)/c/meta_json_parser.h
C_BINDINGS_COMMON=$(BINDINGS)/c/flatbuffers_common_builder.h $(BINDINGS)/c/flatbuffers_common_reader.h
C_BINDINGS=$(C_BINDINGS_COMMON) $(C_BINDINGS_PROFILE) $(C_BINDINGS_META)
BIN=ndb

SANFLAGS = -fsanitize=leak

# Detect operating system
UNAME_S := $(shell uname -s)

# macOS-specific flags
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -framework Security
endif

CHECKDATA=testdata/db/v0/data.mdb

all: $(BIN) lib bench

$(OBJS): $(HEADERS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

libnostrdb.a: $(OBJS)
	ar rcs $@ $(OBJS)

lib: libnostrdb.a

ndb: ndb.c $(DEPS)
	$(CC) $(CFLAGS) ndb.c $(LDS) $(LDFLAGS) -o $@

bindings: bindings-swift bindings-rust bindings-c

check: test
	./test

clean:
	rm -rf test bench bench-ingest bench-ingest-many $(OBJS)

distclean: clean
	rm -rf deps

tags:
	find src -name '*.c' -or -name '*.h' | xargs ctags

configurator: src/configurator.c
	$(CC) $< -o $@

src/config.h: configurator
	./configurator > $@

bindings-c: $(C_BINDINGS)

deps/libsodium/config.log: deps/libsodium/configure
	cd deps/libsodium; \
	./configure --disable-shared --enable-minimal

$(LIBSODIUM_AR): deps/libsodium/config.log
	cd deps/libsodium/src/libsodium; \
	make -j libsodium.la

deps/cmark/.git: deps/.dir
	@devtools/refresh-submodules.sh $(SUBMODULES)

deps/cmark/src/cmark.h: deps/cmark/.git

deps/cmark/build/src/libcmark.a: deps/cmark/src/cmark.h
	mkdir -p deps/cmark/build && cd deps/cmark/build && cmake .. -DCMARK_TESTS=OFF -DCMARK_SHARED=OFF && $(MAKE)

src/bindings/%/.dir:
	mkdir -p $(shell dirname $@)
	touch $@

src/bindings/c/%_builder.h: schemas/%.fbs $(BINDINGS)/c/.dir
	flatcc --builder $< -o $(BINDINGS)/c

src/bindings/c/%_verifier.h bindings/c/%_reader.h: schemas/%.fbs $(BINDINGS)/c/.dir
	flatcc --verifier -o $(BINDINGS)/c $<

src/bindings/c/flatbuffers_common_reader.h: $(BINDINGS)/c/.dir
	flatcc --common_reader -o $(BINDINGS)/c

src/bindings/c/flatbuffers_common_builder.h: $(BINDINGS)/c/.dir
	flatcc --common_builder -o $(BINDINGS)/c

src/bindings/c/%_json_parser.h: schemas/%.fbs $(BINDINGS)/c/.dir
	flatcc --json-parser $< -o $(BINDINGS)/c

bindings-rust: $(BINDINGS)/rust/ndb_profile.rs $(BINDINGS)/rust/ndb_meta.rs

$(BINDINGS)/rust/ndb_profile.rs: schemas/profile.fbs $(BINDINGS)/rust
	flatc --gen-json-emit --rust $<
	@mv profile_generated.rs $@

$(BINDINGS)/rust/ndb_meta.rs: schemas/meta.fbs $(BINDINGS)/swift
	flatc --rust $< 
	@mv meta_generated.rs $@

bindings-swift: $(BINDINGS)/swift/NdbProfile.swift $(BINDINGS)/swift/NdbMeta.swift

$(BINDINGS)/swift/NdbProfile.swift: schemas/profile.fbs $(BINDINGS)/swift
	flatc --gen-json-emit --swift $<
	@mv profile_generated.swift $@

$(BINDINGS)/swift/NdbMeta.swift: schemas/meta.fbs $(BINDINGS)/swift
	flatc --swift $<
	@mv meta_generated.swift $@

deps/.dir:
	@mkdir -p deps
	touch deps/.dir

deps/LMDB_$(LMDB_VER).tar.gz: deps/.dir
	curl -L https://github.com/LMDB/lmdb/archive/refs/tags/LMDB_$(LMDB_VER).tar.gz -o $@

deps/flatcc_$(FLATCC_VER).tar.gz: deps/.dir
	curl -L https://github.com/jb55/flatcc/archive/$(FLATCC_VER).tar.gz -o $@

#deps/flatcc/src/runtime/json_parser.c: deps/flatcc_$(FLATCC_VER).tar.gz deps/.dir
#	tar xf $<
#	rm -rf deps/flatcc
#	mv flatcc-$(FLATCC_VER) deps/flatcc
#	touch $@

#deps/lmdb/lmdb.h: deps/LMDB_$(LMDB_VER).tar.gz deps/.dir
#	tar xf $<
#	rm -rf deps/lmdb
#	mv lmdb-LMDB_$(LMDB_VER)/libraries/liblmdb deps/lmdb
#	rm -rf lmdb-LMDB_$(LMDB_VER)
#	touch $@

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

testdata/db/ndb-v0.tar.zst:
	curl https://cdn.jb55.com/s/ndb-v0.tar.zst -o $@

testdata/db/ndb-v0.tar: testdata/db/ndb-v0.tar.zst
	zstd -d < $< > $@

testdata/db/v0/data.mdb: testdata/db/ndb-v0.tar
	tar xf $<
	rm -rf testdata/db/v0
	mv v0 testdata/db

testdata/many-events.json.zst:
	curl https://cdn.jb55.com/s/many-events.json.zst -o $@

testdata/many-events.json: testdata/many-events.json.zst
	zstd -d $<

bench: bench-ingest-many.c $(DEPS) 
	$(CC) $(CFLAGS) $< $(LDS) $(LDFLAGS) -o $@

perf.out: fake
	perf script > $@

perf.folded: perf.out
	stackcollapse-perf.pl $< > $@

ndb.svg: perf.folded
	flamegraph.pl $< > $@

flamegraph: ndb.svg
	browser $<

run-bench: testdata/many-events.json bench
	./bench

testdata/db/.dir:
	@mkdir -p testdata/db
	touch testdata/db/.dir

test: CFLAGS  += $(SANFLAGS)   # compile test objects with ASan/UBSan
test: LDFLAGS += $(SANFLAGS)   # link test binary with the sanitizer runtime
test: test.c $(DEPS) testdata/db/.dir
	$(CC) $(CFLAGS) test.c $(LDS) $(LDFLAGS) -o $@

# Call this with CCAN_NEW="mod1 mod2..." to add new ccan modules.
update-ccan:
	mv ccan ccan.old
	DIR=$$(pwd)/ccan; cd ../ccan && ./tools/create-ccan-tree -a $$DIR `cd $$DIR.old/ccan && find * -name _info | sed s,/_info,, | LC_ALL=C sort` $(CCAN_NEW)
	mkdir -p ccan/tools/configurator
	cp ../ccan/tools/configurator/configurator.c ../ccan/doc/configurator.1 ccan/tools/configurator/
	$(MAKE) src/config.h
	grep -v '^CCAN version:' ccan.old/README > ccan/README
	echo CCAN version: `git -C ../ccan describe` >> ccan/README
	$(RM) -r ccan/ccan/hash/ ccan/ccan/tal/talloc/	# Unnecessary deps
	$(RM) -r ccan.old

.PHONY: tags clean fake update-ccan
