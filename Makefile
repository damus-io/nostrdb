CFLAGS = -Wall -Wno-unused-function -Werror -O1 -g -DJSMN_PARENT_LINKS -DJSMN_STRICT

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
