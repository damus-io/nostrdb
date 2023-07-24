CFLAGS = -Wall -Wno-unused-function -Werror -O2 -g
HEADERS = sha256.h nostrdb.h cursor.h hex.h jsmn.h config.h sha256.h
SRCS = nostrdb.c sha256.c 
DEPS = $(SRCS) $(HEADERS)

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

bench: bench.c $(DEPS)
	$(CC) $(CFLAGS) bench.c $(SRCS) -o $@

test: test.c $(DEPS)
	$(CC) $(CFLAGS) test.c $(SRCS) -o $@

%.o: %.c
	$(CC) $(CFLAGS)

.PHONY: tags clean
