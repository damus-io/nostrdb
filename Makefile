CFLAGS = -Wall -Wno-unused-function -Werror -O3 -DJSMN_PARENT_LINKS

check: test
	./test

clean:
	rm test

tags:
	ctags *.c *.h

test: test.c nostrdb.c nostrdb.h
	$(CC) $(CFLAGS) test.c nostrdb.c -o $@

%.o: %.c
	$(CC) $(CFLAGS)

.PHONY: tags clean
