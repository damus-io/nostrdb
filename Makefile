CFLAGS = -Wall -Wno-unused-function -Werror -O2 -g
DEPS = nostrdb.c nostrdb.h cursor.h hex.h jsmn.h

check: test
	./test

clean:
	rm -f test bench

tags:
	ctags *.c *.h

bench: bench.c $(DEPS)
	$(CC) $(CFLAGS) bench.c nostrdb.c -o $@
	./bench

test: test.c $(DEPS)
	$(CC) $(CFLAGS) test.c nostrdb.c -o $@

%.o: %.c
	$(CC) $(CFLAGS)

.PHONY: tags clean
