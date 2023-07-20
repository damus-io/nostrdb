
CFLAGS = -Wall -Werror -O3

check: test
	./test

clean:
	rm test

tags:
	ctags *.c *.h

test: test.c nostrdb.c nostrdb.h
	$(CC) test.c nostrdb.c -o $@

%.o: %.c
	$(CC) $(CFLAGS)

.PHONY: tags clean
