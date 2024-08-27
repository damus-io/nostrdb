//
//	nostr_bech32.c
//	damus
//
//	Created by William Casarin on 2023-04-09.
//

#include "nostr_bech32.h"
#include <stdlib.h>
#include "str_block.h"
#include "nostrdb.h"
#include "bolt11/bech32.h"
#include "ccan/array_size/array_size.h"
#include "rcur.h"

#define MAX_TLVS 32

#define TLV_SPECIAL 0
#define TLV_RELAY 1
#define TLV_AUTHOR 2
#define TLV_KIND 3
#define TLV_KNOWN_TLVS 4

struct nostr_tlv {
	unsigned char type;
	unsigned char len;
	const unsigned char *value;
};

/* Returns false on error, *or* empty */
static bool parse_nostr_tlv(struct rcur *rcur, struct nostr_tlv *tlv)
{
	if (!rcur_bytes_remaining(*rcur))
		return false;
	tlv->type = rcur_pull_byte(rcur);
	tlv->len = rcur_pull_byte(rcur);
	tlv->value = rcur_pull(rcur, tlv->len);

	return tlv->value != NULL;
}

enum nostr_bech32_type parse_nostr_bech32_type(struct rcur *typestr)
{
	struct {
		const char *name;
		enum nostr_bech32_type type;
	} table[] = {
		{"note", NOSTR_BECH32_NOTE},
		{"npub", NOSTR_BECH32_NPUB},
		{"nsec", NOSTR_BECH32_NSEC},
		{"nprofile", NOSTR_BECH32_NPROFILE},
		{"nevent", NOSTR_BECH32_NEVENT},
		{"nrelay", NOSTR_BECH32_NRELAY},
		{"naddr", NOSTR_BECH32_NADDR},
	};

	for (size_t i = 0; i < ARRAY_SIZE(table); i++) {
		if (rcur_skip_if_match(typestr,
				       table[i].name, strlen(table[i].name)))
			return table[i].type;
	}

	rcur_fail(typestr);
	return 0;
}

static void parse_nostr_bech32_note(struct rcur *rcur, struct bech32_note *note) {
	note->event_id = rcur_pull(rcur, 32);
}

static void parse_nostr_bech32_npub(struct rcur *rcur, struct bech32_npub *npub) {
	npub->pubkey = rcur_pull(rcur, 32);
}

static void parse_nostr_bech32_nsec(struct rcur *rcur, struct bech32_nsec *nsec) {
	nsec->nsec = rcur_pull(rcur, 32);
}

/* FIXME: Nobody checks this return? */
static bool add_relay(struct ndb_relays *relays, struct nostr_tlv *tlv)
{
	struct ndb_str_block *str;

	if (relays->num_relays + 1 > NDB_MAX_RELAYS)
		return false;
	
	str = &relays->relays[relays->num_relays++];
	str->str = (const char*)tlv->value;
	str->len = tlv->len;
	
	return true;
}

static bool parse_nostr_bech32_nevent(struct rcur *rcur, struct bech32_nevent *nevent) {
	struct nostr_tlv tlv;
	int i;

	nevent->event_id = NULL;
	nevent->pubkey = NULL;
	nevent->relays.num_relays = 0;

	for (i = 0; i < MAX_TLVS; i++) {
		if (!parse_nostr_tlv(rcur, &tlv))
			break;

		switch (tlv.type) {
		case TLV_SPECIAL:
			if (tlv.len != 32)
				return rcur_fail(rcur);
			nevent->event_id = tlv.value;
			break;
		case TLV_AUTHOR:
			if (tlv.len != 32)
				return rcur_fail(rcur);
			nevent->pubkey = tlv.value;
			break;
		case TLV_RELAY:
			add_relay(&nevent->relays, &tlv);
			break;
		}
	}

	if (nevent->event_id == NULL)
		return rcur_fail(rcur);
	return true;
}

static bool parse_nostr_bech32_naddr(struct rcur *rcur, struct bech32_naddr *naddr) {
	struct nostr_tlv tlv;
	int i;

	naddr->identifier.str = NULL;
	naddr->identifier.len = 0;
	naddr->pubkey = NULL;
	naddr->relays.num_relays = 0;

	for (i = 0; i < MAX_TLVS; i++) {
		if (!parse_nostr_tlv(rcur, &tlv))
			break;

		switch (tlv.type) {
		case TLV_SPECIAL:
			naddr->identifier.str = (const char*)tlv.value;
			naddr->identifier.len = tlv.len;
			break;
		case TLV_AUTHOR:
			if (tlv.len != 32) return false;
			naddr->pubkey = tlv.value;
			break;
		case TLV_RELAY:
			add_relay(&naddr->relays, &tlv);
			break;
		}
	}

	if (naddr->identifier.str == NULL)
		return rcur_fail(rcur);
	return true;
}

static bool parse_nostr_bech32_nprofile(struct rcur *rcur, struct bech32_nprofile *nprofile) {
	struct nostr_tlv tlv;
	int i;

	nprofile->pubkey = NULL;
	nprofile->relays.num_relays = 0;

	for (i = 0; i < MAX_TLVS; i++) {
		if (!parse_nostr_tlv(rcur, &tlv))
			break;

		switch (tlv.type) {
		case TLV_SPECIAL:
			if (tlv.len != 32) return rcur_fail(rcur);
			nprofile->pubkey = tlv.value;
			break;
		case TLV_RELAY:
			add_relay(&nprofile->relays, &tlv);
			break;
		}
	}

	if (nprofile->pubkey == NULL)
		return rcur_fail(rcur);
	return true;
}

static bool parse_nostr_bech32_nrelay(struct rcur *rcur, struct bech32_nrelay *nrelay) {
	struct nostr_tlv tlv;
	int i;

	nrelay->relay.str = NULL;
	nrelay->relay.len = 0;

	for (i = 0; i < MAX_TLVS; i++) {
		if (!parse_nostr_tlv(rcur, &tlv))
			break;

		switch (tlv.type) {
		case TLV_SPECIAL:
			nrelay->relay.str = (const char*)tlv.value;
			nrelay->relay.len = tlv.len;
			break;
		}
	}
	
	if (nrelay->relay.str == NULL)
		return rcur_fail(rcur);
	return true;
}

bool parse_nostr_bech32_buffer(struct rcur *rcur,
			       enum nostr_bech32_type type,
			       struct nostr_bech32 *obj)
{
	obj->type = type;
	
	switch (obj->type) {
		case NOSTR_BECH32_NOTE:
			parse_nostr_bech32_note(rcur, &obj->note);
			break;
		case NOSTR_BECH32_NPUB:
			parse_nostr_bech32_npub(rcur, &obj->npub);
			break;
		case NOSTR_BECH32_NSEC:
			parse_nostr_bech32_nsec(rcur, &obj->nsec);
			break;
		case NOSTR_BECH32_NEVENT:
			parse_nostr_bech32_nevent(rcur, &obj->nevent);
			break;
		case NOSTR_BECH32_NADDR:
			parse_nostr_bech32_naddr(rcur, &obj->naddr);
			break;
		case NOSTR_BECH32_NPROFILE:
			parse_nostr_bech32_nprofile(rcur, &obj->nprofile);
			break;
		case NOSTR_BECH32_NRELAY:
			parse_nostr_bech32_nrelay(rcur, &obj->nrelay);
			break;
	}
	return rcur_valid(rcur);
}

bool parse_nostr_bech32_str(struct rcur *bech32, enum nostr_bech32_type *type) {
	*type = parse_nostr_bech32_type(bech32);
	// must be at least 59 chars for the data part
	return rcur_pull_non_alphanumeric(bech32) >= 59;
}


bool parse_nostr_bech32(unsigned char *buf, int buflen,
			const char *bech32_str, size_t bech32_len,
			struct nostr_bech32 *obj) {
	const unsigned char *start;
	size_t parsed_len, u5_out_len, u8_out_len;
	enum nostr_bech32_type type;
	static const int MAX_PREFIX = 8;
	struct cursor cur;
	struct rcur bech32, u8;

	make_cursor(buf, buf + buflen, &cur);
	bech32 = rcur_forbuf(bech32_str, bech32_len);
	
	start = bech32.p;
	if (!parse_nostr_bech32_str(&bech32, &type))
		return 0;

	parsed_len = bech32.p - start;

	// some random sanity checking
	if (parsed_len < 10 || parsed_len > 10000)
		return 0;

	unsigned char u5[parsed_len];
	char prefix[MAX_PREFIX];
	
	if (bech32_decode_len(prefix, u5, &u5_out_len, (const char*)start,
			      parsed_len, MAX_PREFIX) == BECH32_ENCODING_NONE)
	{
		return 0;
	}

	if (!bech32_convert_bits(cur.p, &u8_out_len, 8, u5, u5_out_len, 5, 0))
		return 0;

	u8 = rcur_forbuf(cur.p, u8_out_len);

	return parse_nostr_bech32_buffer(&u8, type, obj);
}

