

#include "nostrdb.h"
#include "block.h"
#include "rcur.h"
#include <stdlib.h>

int push_str_block(struct cursor *buf, const char *content, struct ndb_str_block *block) {
	return cursor_push_varint(buf, block->str - content) &&
	       cursor_push_varint(buf, block->len);
}

bool pull_str_block(struct rcur *rcur, const char *content, struct ndb_str_block *block) {
	block->str = content + rcur_pull_varint_u32(rcur);
	block->len = rcur_pull_varint_u32(rcur);

	return rcur_valid(rcur);
}

static bool pull_nostr_bech32_type(struct rcur *rcur, enum nostr_bech32_type *type)
{
	uint64_t inttype;

	// Returns 0 on failure.
	inttype = rcur_pull_varint(rcur);
	if (inttype <= 0 || inttype > NOSTR_BECH32_KNOWN_TYPES) {
		rcur_fail(rcur);
		return false;
	}

	*type = inttype;
	return true;
}


static bool pull_bech32_mention(const char *content, struct rcur *rcur, struct ndb_mention_bech32_block *block) {
	uint16_t size;
	struct rcur bech32;

	pull_str_block(rcur, content, &block->str);
	size = rcur_pull_u16(rcur);
	pull_nostr_bech32_type(rcur, &block->bech32.type);

	bech32 = rcur_pull_slice(rcur, size);
	parse_nostr_bech32_buffer(&bech32, block->bech32.type, &block->bech32);
	if (!rcur_valid(&bech32))
		rcur_fail(rcur);

	return rcur_valid(rcur);
}

static bool pull_invoice(const char *content, struct rcur *rcur,
			 struct ndb_invoice_block *block)
{
	pull_str_block(rcur, content, &block->invstr);
	return ndb_decode_invoice(rcur, &block->invoice);
}

static bool pull_block_type(struct rcur *rcur, enum ndb_block_type *type)
{
	uint32_t itype;

	itype = rcur_pull_varint_u32(rcur);
	if (itype <= 0 || itype > NDB_NUM_BLOCK_TYPES) {
		rcur_fail(rcur);
		return false;
	}

	*type = itype;
	return true;
}

static bool pull_block(const char *content, struct rcur *rcur, struct ndb_block *block)
{
	if (!pull_block_type(rcur, &block->type))
		return false;

	switch (block->type) {
	case BLOCK_HASHTAG:
	case BLOCK_TEXT:
	case BLOCK_URL:
		return pull_str_block(rcur, content, &block->block.str);

	case BLOCK_MENTION_INDEX:
		block->block.mention_index = rcur_pull_varint_u32(rcur);
		return rcur_valid(rcur);

	case BLOCK_MENTION_BECH32:
		return pull_bech32_mention(content, rcur, &block->block.mention_bech32);

	case BLOCK_INVOICE:
		// we only push invoice strs here
		return pull_invoice(content, rcur, &block->block.invoice);
	}

	/* unreachable: pull_block_type can only return known types */
	assert(0);
}


enum ndb_block_type ndb_get_block_type(struct ndb_block *block) {
	return block->type;
}

// BLOCK ITERATORS
void ndb_blocks_iterate_start(const char *content, struct ndb_blocks *blocks, struct ndb_block_iterator *iter) {
	iter->blocks = blocks;
	iter->content = content;
	iter->rcur = rcur_forbuf(blocks->blocks, iter->blocks->blocks_size);
}

struct ndb_block *ndb_blocks_iterate_next(struct ndb_block_iterator *iter)
{
	if (!pull_block(iter->content, &iter->rcur, &iter->block))
		return NULL;

	return &iter->block;
}

// STR BLOCKS
struct ndb_str_block *ndb_block_str(struct ndb_block *block)
{
	switch (block->type) {
	case BLOCK_HASHTAG:
	case BLOCK_TEXT:
	case BLOCK_URL:
		return &block->block.str;
	case BLOCK_MENTION_INDEX:
		return NULL;
	case BLOCK_MENTION_BECH32:
		return &block->block.mention_bech32.str;
	case BLOCK_INVOICE:
		return &block->block.invoice.invstr;
	}

	return NULL;
}

const char *ndb_str_block_ptr(struct ndb_str_block *str_block) {
	return str_block->str;
}

uint32_t ndb_str_block_len(struct ndb_str_block *str_block) {
	return str_block->len;
}

struct nostr_bech32 *ndb_bech32_block(struct ndb_block *block) {
	return &block->block.mention_bech32.bech32;
}

// total size including padding
size_t ndb_blocks_total_size(struct ndb_blocks *blocks) {
	return blocks->total_size;
}

void ndb_blocks_free(struct ndb_blocks *blocks) {
	if ((blocks->flags & NDB_BLOCK_FLAG_OWNED) != NDB_BLOCK_FLAG_OWNED)
		return;

	free(blocks);
}

int ndb_blocks_flags(struct ndb_blocks *blocks) {
	return blocks->flags;
}

int ndb_blocks_word_count(struct ndb_blocks *blocks) {
	return blocks->words;
}
