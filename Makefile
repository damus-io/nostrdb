CFLAGS = -Wall -Wno-unused-function -Werror -O2 -g
HEADERS = sha256.h nostrdb.h cursor.h hex.h jsmn.h config.h sha256.h random.h
SRCS = nostrdb.c sha256.c 
LDS = $(SRCS) $(ARS)
ARS = libsecp256k1.a
DEPS = $(SRCS) $(HEADERS) $(ARS)
PREFIX ?= /usr/local
SUBMODULES = deps/secp256k1

check: test
	./test

clean:
	rm -f test bench

tags:
	ctags *.c *.h

benchmark: bench
	./bench

configurator: configurator.c
	$(CC) $< -o $@

config.h: configurator
	./configurator > $@

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
