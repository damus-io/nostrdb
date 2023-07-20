
#ifndef HEX_H
#define HEX_H

#include <stdlib.h>

static int char_to_hex(unsigned char *val, char c)
{
	if (c >= '0' && c <= '9') {
		*val = c - '0';
		return 1;
	}
 	if (c >= 'a' && c <= 'f') {
		*val = c - 'a' + 10;
		return 1;
	}
 	if (c >= 'A' && c <= 'F') {
		*val = c - 'A' + 10;
		return 1;
	}
	return 0;
}

static int hex_decode(const char *str, size_t slen, void *buf, size_t bufsize)
{
	unsigned char v1, v2;
	unsigned char *p = buf;

	while (slen > 1) {
		if (!char_to_hex(&v1, str[0]) || !char_to_hex(&v2, str[1]))
			return 0;
		if (!bufsize)
			return 0;
		*(p++) = (v1 << 4) | v2;
		str += 2;
		slen -= 2;
		bufsize--;
	}
	return slen == 0 && bufsize == 0;
}


static char hexchar(unsigned int val)
{
	if (val < 10)
		return '0' + val;
	if (val < 16)
		return 'a' + val - 10;
	abort();
}

static int hex_encode(const void *buf, size_t bufsize, char *dest, size_t destsize)
{
	size_t i;

	for (i = 0; i < bufsize; i++) {
		unsigned int c = ((const unsigned char *)buf)[i];
		*(dest++) = hexchar(c >> 4);
		*(dest++) = hexchar(c & 0xF);
	}
	*dest = '\0';

	return 1;
}


#endif 
