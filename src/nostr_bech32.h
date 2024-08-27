//
//	nostr_bech32.h
//	damus
//
//	Created by William Casarin on 2023-04-09.
//

#ifndef nostr_bech32_h
#define nostr_bech32_h

#include <stdio.h>
#include "str_block.h"
#include "nostrdb.h"

struct rcur;

bool parse_nostr_bech32_str(struct rcur *bech32, enum nostr_bech32_type *type);

/* Check rcur_valid(typestr) to determine if it failed */
enum nostr_bech32_type parse_nostr_bech32_type(struct rcur *typestr);

bool parse_nostr_bech32_buffer(struct rcur *rcur, enum nostr_bech32_type type,
			       struct nostr_bech32 *obj);

bool parse_nostr_bech32(unsigned char *buf, int buflen,
			const char *bech32_str, size_t bech32_len,
			struct nostr_bech32 *obj);

/*
int parse_nostr_bech32(const char *bech32, size_t input_len,
		       unsigned char *outbuf, size_t outlen,
		       enum nostr_bech32_type *type);
		       */

#endif /* nostr_bech32_h */
