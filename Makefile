INCLUDES = deps/secp256k1/include
CFLAGS = -Wall -Wno-unused-function -Werror -O2 -g -I$(INCLUDES)
HEADERS = sha256.h nostrdb.h cursor.h hex.h jsmn.h config.h sha256.h random.h
SRCS = nostrdb.c sha256.c 
LDS = $(SRCS) $(ARS)
ARS = libsecp256k1.a
DEPS = $(SRCS) $(HEADERS) $(ARS)
PREFIX ?= /usr/local
SUBMODULES = deps/secp256k1
C_BINDINGS_PROFILE=bindings/c/profile_builder.h bindings/c/profile_reader.h bindings/c/profile_verifier.h
C_BINDINGS_COMMON=bindings/c/flatbuffers_common_builder.h bindings/c/flatbuffers_common_reader.h 
C_BINDINGS=$(C_BINDINGS_COMMON) $(C_BINDINGS_PROFILE)
BINDINGS=bindings

all: bench test

bindings: bindings-swift bindings-c

check: test
	./test

clean:
	rm -rf test bench bindings

tags:
	ctags *.c *.h

benchmark: bench
	./bench

configurator: configurator.c
	$(CC) $< -o $@

config.h: configurator
	./configurator > $@

bindings-c: $(C_BINDINGS)

bindings/%:
	mkdir -p $@

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

bindings-swift: bindings/swift/NdbProfile.swift

bindings/swift/NdbProfile.swift: schemas/profile.fbs bindings/swift
	flatc --swift $<
	@mv profile_generated.swift $@

deps/secp256k1/.git:
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

libsecp256k1.a: deps/secp256k1/.libs/libsecp256k1.a
	cp $< $@

bench: bench.c $(DEPS)
	$(CC) $(CFLAGS) bench.c $(LDS) -o $@

test: test.c $(DEPS)
	$(CC) $(CFLAGS) test.c $(LDS) -o $@

%.o: %.c
	$(CC) $(CFLAGS)

.PHONY: tags clean
