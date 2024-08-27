#include "cursor.h"
#include "rcur.h"
#include "nostr_bech32.h"
#include "block.h"
#include "nostrdb.h"
#include "invoice.h"
#include "ccan/mem/mem.h"
#include "bolt11/bolt11.h"
#include "bolt11/bech32.h"
#include <stdlib.h>
#include <string.h>

#include "cursor.h"

struct ndb_content_parser {
	int bech32_strs;
	struct cursor buffer;
	struct rcur content;
	struct ndb_blocks *blocks;
};

/* Only updates rcur if it returns true */
static bool parse_digit(struct rcur *rcur, int *digit)
{
	const char *c;

	c = rcur_peek(rcur, 1);
	if (!c)
		return false;
	
	if (*c >= '0' && *c <= '9') {
		*digit = *c - '0';
		rcur_skip(rcur, 1);
		return true;
	}
	return false;
}

/* Leaves rcur untouched if it returns false. */
static bool parse_if_mention_index(struct rcur *rcur, struct ndb_block *block)
{
	int d1, d2, d3, ind;
	struct rcur index = *rcur;

	if (!rcur_skip_if_str_anycase(&index, "#["))
		return false;
	
	if (!parse_digit(&index, &d1))
		return false;
	
	ind = d1;
	
	if (parse_digit(&index, &d2)) {
		ind = (d1 * 10) + d2;
		if (parse_digit(&index, &d3))
			ind = (d1 * 100) + (d2 * 10) + d3;
	}

	if (!rcur_skip_if_str_anycase(rcur, "]"))
		return false;
	
	block->type = BLOCK_MENTION_INDEX;
	block->block.mention_index = ind;

	*rcur = index;
	return true;
}

/* Leaves rcur untouched if it returns false. */
static bool parse_if_hashtag(struct rcur *rcur, struct ndb_block *block)
{
	const char *c;
	struct rcur hashtag = *rcur;
	size_t len;

	if (!rcur_skip_if_str_anycase(&hashtag, "#"))
		return false;

	c = rcur_pull_word(&hashtag, &len);
	if (!c || is_whitespace(*c) || *c == '#')
		return false;
	
	block->type = BLOCK_HASHTAG;
	block->block.str.str = (const char *)rcur->p + 1;
	block->block.str.len = hashtag.p - (rcur->p + 1);

	*rcur = hashtag;
	return true;
}

//
// decode and push a bech32 mention into our blocks output buffer.
//
// bech32 blocks are stored as:
//
//     bech32_buffer_size : u16
//     nostr_bech32_type  : varint
//     bech32_data        : [u8]
//
// The TLV form is compact already, so we just use it directly
//
// This allows us to not duplicate all of the TLV encoding and decoding code
// for our on-disk nostrdb format.
//
static int push_bech32_mention(struct ndb_content_parser *p, struct ndb_str_block *bech32)
{
	// we decode the raw bech32 directly into the output buffer
	struct cursor u8, u5;
	unsigned char *start;
	uint16_t *u8_size;
	enum nostr_bech32_type type;
	size_t u5_out_len, u8_out_len;
	static const int MAX_PREFIX = 8;
	char prefix[9] = {0};

	start = p->buffer.p;

	if (!parse_nostr_bech32_type(bech32->str, &type))
		goto fail;

	// make sure to push the str block!
	if (!push_str_block(&p->buffer, (const char*)p->content.start, bech32))
		goto fail;
	//
	// save a spot for the raw bech32 buffer size
	u8_size = (uint16_t*)p->buffer.p;
	if (!cursor_skip(&p->buffer, 2))
		goto fail;

	if (!cursor_push_varint(&p->buffer, type))
		goto fail;

	if (!cursor_malloc_slice(&p->buffer, &u8, bech32->len))
		goto fail;

	if (!cursor_malloc_slice(&p->buffer, &u5, bech32->len))
		goto fail;
	
	if (bech32_decode_len(prefix, u5.p, &u5_out_len, bech32->str,
			      bech32->len, MAX_PREFIX) == BECH32_ENCODING_NONE) {
		goto fail;
	}

	u5.p += u5_out_len;

	if (!bech32_convert_bits(u8.p, &u8_out_len, 8, u5.start, u5.p - u5.start, 5, 0))
		goto fail;

	u8.p += u8_out_len;

	// move the out cursor to the end of the 8-bit buffer
	p->buffer.p = u8.p;

	if (u8_out_len > UINT16_MAX)
		goto fail;

	// mark the size of the bech32 buffer
	*u8_size = (uint16_t)u8_out_len;

	return 1;

fail:
	p->buffer.p = start;
	return 0;
}

static int push_invoice_str(struct ndb_content_parser *p, struct ndb_str_block *str)
{
	unsigned char *start;
	struct bolt11 *bolt11;
	char *fail;

	if (!(bolt11 = bolt11_decode_minimal(NULL, str->str, &fail)))
		return 0;

	start = p->buffer.p;

	// push the text block just incase we don't care for the invoice
	if (!push_str_block(&p->buffer, (const char*)p->content.start, str))
		return 0;

	// push decoded invoice data for quick access
	if (!ndb_encode_invoice(&p->buffer, bolt11)) {
		p->buffer.p = start;
		tal_free(bolt11);
		return 0;
	}

	tal_free(bolt11);
	return 1;
}

int push_block(struct ndb_content_parser *p, struct ndb_block *block);
static int add_text_block(struct ndb_content_parser *p, const char *start, const char *end)
{
	struct ndb_block b;
	
	if (start == end)
		return 1;
	
	b.type = BLOCK_TEXT;
	b.block.str.str = start;
	b.block.str.len = end - start;
	
	return push_block(p, &b);
}


int push_block(struct ndb_content_parser *p, struct ndb_block *block)
{
	unsigned char *start = p->buffer.p;

	// push the tag
	if (!cursor_push_varint(&p->buffer, block->type))
		return 0;

	switch (block->type) {
	case BLOCK_HASHTAG:
	case BLOCK_TEXT:
	case BLOCK_URL:
		if (!push_str_block(&p->buffer, (const char*)p->content.start,
			       &block->block.str))
			goto fail;
		break;

	case BLOCK_MENTION_INDEX:
		if (!cursor_push_varint(&p->buffer, block->block.mention_index))
			goto fail;
		break;
	case BLOCK_MENTION_BECH32:
		// we only push bech32 strs here
		if (!push_bech32_mention(p, &block->block.str)) {
			// if we fail for some reason, try pushing just a text block
			p->buffer.p = start;
			if (!add_text_block(p, block->block.str.str,
					       block->block.str.str +
					       block->block.str.len)) {
				goto fail;
			}
		}
		break;

	case BLOCK_INVOICE:
		// we only push invoice strs here
		if (!push_invoice_str(p, &block->block.str)) {
			// if we fail for some reason, try pushing just a text block
			p->buffer.p = start;
			if (!add_text_block(p, block->block.str.str,
					    block->block.str.str + block->block.str.len)) {
				goto fail;
			}
		}
		break;
	}

	p->blocks->num_blocks++;

	return 1;

fail:
	p->buffer.p = start;
	return 0;
}



static bool char_disallowed_at_end_url(char c)
{
	return c == '.' || c == ',';
 
}

static bool is_final_url_char(const struct rcur *rcur) 
{
	const char *p = rcur_peek(rcur, 1), *p2;
	if (!p)
		return true;

	if (is_whitespace(*p))
		return true;

	p2 = rcur_peek(rcur, 2);
	if (!p2 || is_whitespace(p2[1])) {
		// next char is whitespace so this char could be the final char in the url
		return char_disallowed_at_end_url(*p);
	}

	// next char isn't whitespace so it can't be a final char
	return false;
}

static bool consume_until_end_url(struct rcur *rcur)
{
	bool consumed = false;

	while (rcur_bytes_remaining(*rcur)) {
		if (is_final_url_char(rcur))
			return consumed;

		rcur_skip(rcur, 1);
		consumed = true;
	}

	return true;
}

static bool consume_url_fragment(struct rcur *rcur)
{
	const char *c;

	c = rcur_peek(rcur, 1);
	if (!c)
		return true;

	if (*c != '#' && *c != '?') {
		return true;
	}

	rcur_skip(rcur, 1);

	return consume_until_end_url(rcur);
}

static void consume_url_path(struct rcur *rcur)
{
	const char *c;

	c = rcur_peek(rcur, 1);
	if (!c || *c != '/')
		return;

	while ((c = rcur_peek(rcur, 1)) != NULL) {
		if (*c == '?' || *c == '#' || is_final_url_char(rcur)) {
			return;
		}

		rcur_skip(rcur, 1);
	}
}

static bool consume_url_host(struct rcur *rcur)
{
	const char *c;
	int count = 0;

	while ((c = rcur_peek(rcur, 1)) != NULL) {
		// TODO: handle IDNs
		if ((is_alphanumeric(*c) || *c == '.' || *c == '-') && !is_final_url_char(rcur))
		{
			count++;
			rcur_skip(rcur, 1);
			continue;
		}

		return count != 0;
	}


	// this means the end of the URL hostname is the end of the buffer and we finished
	return count != 0;
}

/* Leaves rcur untouched if it returns false. */
static bool parse_if_url(struct rcur *rcur,
			 bool prev_was_open_bracket,
			 struct ndb_block *block)
{
	struct rcur url = *rcur, path_rcur;
	const unsigned char *host;
	int host_len;
	enum nostr_bech32_type type;
	
	if (!rcur_skip_if_str_anycase(&url, "http"))
		return false;

	rcur_skip_if_str_anycase(&url, "s");
	if (!rcur_skip_if_str_anycase(&url, "://"))
		return false;

	// make sure to save the hostname. We will use this to detect damus.io links
	host = url.p;

	if (!consume_url_host(&url))
		return false;

	// get the length of the host string
	host_len = (int)(url.p - host);

	// save the current parse state so that we can continue from here when
	// parsing the bech32 in the damus.io link if we have it
	path_rcur = url;
	// skip leading /
	rcur_skip(&path_rcur, 1);

	consume_url_path(&url);

	if (!consume_url_fragment(&url))
		return false;

	// smart parens: is entire URL surrounded by ()?
	if (prev_was_open_bracket
	    && url.p > rcur->start
	    && url.p[-1] == ')') {
		    url.p--;
	}

	// save the bech32 string pos in case we hit a damus.io link
	block->block.str.str = (const char *)path_rcur.p;

	// if we have a damus link, make it a mention
	if (memeq(host, host_len, "damus.io", strlen("damus.io"))) {
		struct cursor path_cur = cursor_from_rcur(&path_rcur);
		if (parse_nostr_bech32_str(&path_cur, &type)) {
			block->block.str.len = path_cur.p - path_cur.start;
			block->type = BLOCK_MENTION_BECH32;
			return true;
		}
	}

	block->type = BLOCK_URL;
	block->block.str.str = (const char *)rcur->p;
	block->block.str.len = url.p - rcur->p;

	*rcur = url;
	return true;
}

/* Leaves rcur untouched if it returns false. */
static bool parse_if_invoice(struct rcur *rcur, struct ndb_block *block)
{
	struct rcur invoice = *rcur;
	size_t len;

	// optional
	rcur_skip_if_str_anycase(&invoice, "lightning:");
	
	if (!rcur_skip_if_str_anycase(&invoice, "lnbc"))
		return false;

	if (!rcur_pull_word(&invoice, &len) || len == 0)
		return false;

	block->type = BLOCK_INVOICE;

	block->block.str.str = (const char*)rcur->p;
	block->block.str.len = invoice.p - rcur->p;

	*rcur = invoice;
	return true;
}

/* Leaves rcur untouched if it returns false. */
static bool parse_if_mention_bech32(struct rcur *rcur, struct ndb_block *block)
{
	struct rcur bech32 = *rcur;
	enum nostr_bech32_type type;
	struct cursor cursor;

	/* Ignore these */
	rcur_skip_if_str_anycase(&bech32, "@");
	rcur_skip_if_str_anycase(&bech32, "nostr:");

	block->block.str.str = (const char *)bech32.p;

	cursor = cursor_from_rcur(&bech32);
	if (!parse_nostr_bech32_str(&cursor, &type))
		return false;
	
	block->block.str.len = cursor.p - (unsigned char*)block->block.str.str;
	block->type = BLOCK_MENTION_BECH32;

	*rcur = rcur_from_cursor(&cursor);
	return true;
}

int ndb_parse_content(unsigned char *buf, int buf_size,
		      const char *content, int content_len,
		      struct ndb_blocks **blocks_p)
{
	struct ndb_content_parser parser;
	struct ndb_block block;
	bool prev_was_open_bracket = false;
	const unsigned char *start, *pre_mention, *blocks_start;
	
	make_cursor(buf, buf + buf_size, &parser.buffer);

	// allocate some space for the blocks header
	*blocks_p = parser.blocks = (struct ndb_blocks *)buf;
	parser.buffer.p += sizeof(struct ndb_blocks);

	parser.content = rcur_forbuf(content, content_len);

	parser.blocks->words = 0;
	parser.blocks->num_blocks = 0;
	parser.blocks->blocks_size = 0;
	parser.blocks->flags = 0;
	parser.blocks->version = 1;

	blocks_start = start = parser.content.p;

	while (rcur_bytes_remaining(parser.content)) {
		const char *c;

		// Skip whitespace.
		rcur_pull_whitespace(&parser.content);

		c = rcur_peek(&parser.content, 1);
		if (!c)
			break;
		
		// new word
		parser.blocks->words++;

		pre_mention = parser.content.p;

		switch (*c) {
		case '#':
			if (parse_if_mention_index(&parser.content, &block) || parse_if_hashtag(&parser.content, &block))
				goto add_it;
			break;

		case 'h':			
		case 'H':
			if (parse_if_url(&parser.content, prev_was_open_bracket, &block))
				goto add_it;
			break;

		case 'l':
		case 'L':
			if (parse_if_invoice(&parser.content, &block))
				goto add_it;
			break;

		case 'n':
		case '@':
			if (parse_if_mention_bech32(&parser.content, &block))
				goto add_it;
			break;
		}
		prev_was_open_bracket = (*c == '(');
		rcur_skip(&parser.content, 1);
		continue;

	add_it:
		// Add any text (e.g. whitespace) before this (noop if empty)
		if (!add_text_block(&parser, (char *)start, (char *)pre_mention))
			return 0;
		if (!push_block(&parser, &block))
			return 0;

		start = parser.content.p;
	}

	// Add any trailing text (noop if empty)
	if (!add_text_block(&parser, (const char*)start, (const char *)parser.content.p))
		return 0;

	parser.blocks->blocks_size = parser.buffer.p - blocks_start;

	//
	// pad to 8-byte alignment
	//
	if (!cursor_align(&parser.buffer, 8))
		return 0;
	assert((parser.buffer.p - parser.buffer.start) % 8 == 0);
	parser.blocks->total_size = parser.buffer.p - parser.buffer.start;

	return 1;
}

