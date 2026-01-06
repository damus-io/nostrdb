#include "nostrdb.h"
#include "block.h"
#include "cursor.h"
#include "nostr_bech32.h"

#include <cmark.h>
#include <string.h>
#include <stdlib.h>

struct ndb_markdown_parser {
	struct cursor buffer;
	struct cursor content;
	struct ndb_blocks *blocks;
	const char *content_start;

	// Image alt text collection state
	int in_image;
	char alt_buf[1024];
	int alt_len;
	const char *image_url;
	const char *image_title;
};

// Push a string block by storing length and the actual string data
// This is different from regular push_str_block which stores offsets
static int push_md_str_block(struct cursor *buf, struct ndb_str_block *block)
{
	if (!cursor_push_varint(buf, block->len))
		return 0;

	// Store the actual string content
	if (block->len > 0 && block->str) {
		if (!cursor_push(buf, (unsigned char *)block->str, block->len))
			return 0;
	}

	return 1;
}

// Forward declarations
static int push_markdown_block(struct ndb_markdown_parser *p, struct ndb_block *block);
static int parse_node(struct ndb_markdown_parser *p, cmark_node *node, cmark_event_type ev_type);

static int init_str_block(struct ndb_str_block *block, const char *str, int len)
{
	if (!str) {
		block->str = "";
		block->len = 0;
		return 1;
	}
	block->str = str;
	block->len = len >= 0 ? len : (int)strlen(str);
	return 1;
}

static int push_text_block(struct ndb_markdown_parser *p, const char *text, int len)
{
	struct ndb_block block;

	if (!text || len == 0)
		return 1;

	block.type = BLOCK_TEXT;
	init_str_block(&block.block.str, text, len);

	return push_markdown_block(p, &block);
}

// Check if a URL is a nostr: URI and parse it
static int is_nostr_uri(const char *url, struct ndb_str_block *bech32_out)
{
	if (!url)
		return 0;

	// Check for nostr: prefix
	if (strncmp(url, "nostr:", 6) != 0)
		return 0;

	const char *bech32 = url + 6;
	int len = (int)strlen(bech32);

	if (len == 0)
		return 0;

	bech32_out->str = bech32;
	bech32_out->len = len;
	return 1;
}

static int push_markdown_block(struct ndb_markdown_parser *p, struct ndb_block *block)
{
	unsigned char *start = p->buffer.p;

	// Push the block type
	if (!cursor_push_varint(&p->buffer, block->type))
		return 0;

	switch (block->type) {
	case BLOCK_TEXT:
	case BLOCK_HASHTAG:
	case BLOCK_URL:
		// Use inline storage for markdown content (cmark returns its own buffer pointers)
		if (!push_md_str_block(&p->buffer, &block->block.str))
			goto fail;
		break;

	case BLOCK_HEADING:
		if (!cursor_push_byte(&p->buffer, block->block.heading.level))
			goto fail;
		break;

	case BLOCK_CODE_BLOCK:
		if (!push_md_str_block(&p->buffer, &block->block.code_block.info))
			goto fail;
		if (!push_md_str_block(&p->buffer, &block->block.code_block.literal))
			goto fail;
		break;

	case BLOCK_LIST:
		if (!cursor_push_byte(&p->buffer, block->block.list.list_type))
			goto fail;
		if (!cursor_push_varint(&p->buffer, block->block.list.start))
			goto fail;
		if (!cursor_push_byte(&p->buffer, block->block.list.tight))
			goto fail;
		break;

	case BLOCK_LINK:
		if (!push_md_str_block(&p->buffer, &block->block.link.url))
			goto fail;
		if (!push_md_str_block(&p->buffer, &block->block.link.title))
			goto fail;
		break;

	case BLOCK_IMAGE:
		if (!push_md_str_block(&p->buffer, &block->block.image.url))
			goto fail;
		if (!push_md_str_block(&p->buffer, &block->block.image.title))
			goto fail;
		if (!push_md_str_block(&p->buffer, &block->block.image.alt))
			goto fail;
		break;

	case BLOCK_CODE_INLINE:
		if (!push_md_str_block(&p->buffer, &block->block.str))
			goto fail;
		break;

	case BLOCK_MENTION_BECH32:
		// For nostr: links, push the bech32 string inline
		if (!push_md_str_block(&p->buffer, &block->block.str))
			goto fail;
		break;

	case BLOCK_PARAGRAPH:
	case BLOCK_BLOCKQUOTE:
	case BLOCK_LIST_ITEM:
	case BLOCK_THEMATIC_BREAK:
	case BLOCK_LINEBREAK:
	case BLOCK_SOFTBREAK:
	case BLOCK_EMPH:
	case BLOCK_STRONG:
		// These are markers only, no additional data
		break;

	default:
		goto fail;
	}

	p->blocks->num_blocks++;
	return 1;

fail:
	p->buffer.p = start;
	return 0;
}

static int parse_node(struct ndb_markdown_parser *p, cmark_node *node, cmark_event_type ev_type)
{
	struct ndb_block block;
	cmark_node_type type = cmark_node_get_type(node);
	const char *literal;
	const char *url;
	const char *title;
	struct ndb_str_block bech32_str;

	// If inside an image, suppress all nodes except TEXT (for alt text)
	// and IMAGE EXIT (to emit the image block)
	if (p->in_image && type != CMARK_NODE_TEXT) {
		if (type != CMARK_NODE_IMAGE)
			return 1;
	}

	memset(&block, 0, sizeof(block));

	switch (type) {
	case CMARK_NODE_DOCUMENT:
		// Skip document node
		return 1;

	case CMARK_NODE_PARAGRAPH:
		if (ev_type == CMARK_EVENT_ENTER) {
			block.type = BLOCK_PARAGRAPH;
			return push_markdown_block(p, &block);
		}
		return 1;

	case CMARK_NODE_HEADING:
		if (ev_type == CMARK_EVENT_ENTER) {
			block.type = BLOCK_HEADING;
			block.block.heading.level = (uint8_t)cmark_node_get_heading_level(node);
			return push_markdown_block(p, &block);
		}
		return 1;

	case CMARK_NODE_BLOCK_QUOTE:
		if (ev_type == CMARK_EVENT_ENTER) {
			block.type = BLOCK_BLOCKQUOTE;
			return push_markdown_block(p, &block);
		}
		return 1;

	case CMARK_NODE_LIST:
		if (ev_type == CMARK_EVENT_ENTER) {
			block.type = BLOCK_LIST;
			block.block.list.list_type =
				(cmark_node_get_list_type(node) == CMARK_ORDERED_LIST)
				? NDB_LIST_ORDERED : NDB_LIST_BULLET;
			block.block.list.start = cmark_node_get_list_start(node);
			block.block.list.tight = cmark_node_get_list_tight(node) ? 1 : 0;
			return push_markdown_block(p, &block);
		}
		return 1;

	case CMARK_NODE_ITEM:
		if (ev_type == CMARK_EVENT_ENTER) {
			block.type = BLOCK_LIST_ITEM;
			return push_markdown_block(p, &block);
		}
		return 1;

	case CMARK_NODE_CODE_BLOCK:
		if (ev_type == CMARK_EVENT_ENTER) {
			block.type = BLOCK_CODE_BLOCK;

			const char *info = cmark_node_get_fence_info(node);
			literal = cmark_node_get_literal(node);

			init_str_block(&block.block.code_block.info, info, -1);
			init_str_block(&block.block.code_block.literal, literal, -1);

			return push_markdown_block(p, &block);
		}
		return 1;

	case CMARK_NODE_THEMATIC_BREAK:
		// Leaf node: only emit on ENTER to avoid double-push
		if (ev_type != CMARK_EVENT_ENTER)
			return 1;
		block.type = BLOCK_THEMATIC_BREAK;
		return push_markdown_block(p, &block);

	case CMARK_NODE_TEXT:
		// Leaf node: only emit on ENTER
		if (ev_type != CMARK_EVENT_ENTER)
			return 1;
		literal = cmark_node_get_literal(node);
		if (literal) {
			int len = (int)strlen(literal);
			// If we're inside an image, collect text as alt text
			if (p->in_image) {
				int space = (int)sizeof(p->alt_buf) - p->alt_len - 1;
				int copy_len = len < space ? len : space;
				if (copy_len > 0) {
					memcpy(p->alt_buf + p->alt_len, literal, copy_len);
					p->alt_len += copy_len;
				}
				return 1;  // Don't push as separate TEXT block
			}
			return push_text_block(p, literal, len);
		}
		return 1;

	case CMARK_NODE_SOFTBREAK:
		// Leaf node: only emit on ENTER
		if (ev_type != CMARK_EVENT_ENTER)
			return 1;
		block.type = BLOCK_SOFTBREAK;
		return push_markdown_block(p, &block);

	case CMARK_NODE_LINEBREAK:
		// Leaf node: only emit on ENTER
		if (ev_type != CMARK_EVENT_ENTER)
			return 1;
		block.type = BLOCK_LINEBREAK;
		return push_markdown_block(p, &block);

	case CMARK_NODE_CODE:
		// Leaf node: only emit on ENTER
		if (ev_type != CMARK_EVENT_ENTER)
			return 1;
		literal = cmark_node_get_literal(node);
		block.type = BLOCK_CODE_INLINE;
		init_str_block(&block.block.str, literal, -1);
		return push_markdown_block(p, &block);

	case CMARK_NODE_EMPH:
		if (ev_type == CMARK_EVENT_ENTER) {
			block.type = BLOCK_EMPH;
			return push_markdown_block(p, &block);
		}
		return 1;

	case CMARK_NODE_STRONG:
		if (ev_type == CMARK_EVENT_ENTER) {
			block.type = BLOCK_STRONG;
			return push_markdown_block(p, &block);
		}
		return 1;

	case CMARK_NODE_LINK:
		if (ev_type == CMARK_EVENT_ENTER) {
			url = cmark_node_get_url(node);
			title = cmark_node_get_title(node);

			// Check if this is a nostr: URI
			if (is_nostr_uri(url, &bech32_str)) {
				block.type = BLOCK_MENTION_BECH32;
				block.block.str = bech32_str;
				return push_markdown_block(p, &block);
			}

			block.type = BLOCK_LINK;
			init_str_block(&block.block.link.url, url, -1);
			init_str_block(&block.block.link.title, title, -1);
			return push_markdown_block(p, &block);
		}
		return 1;

	case CMARK_NODE_IMAGE:
		if (ev_type == CMARK_EVENT_ENTER) {
			// Save image info and start collecting alt text
			p->in_image = 1;
			p->alt_len = 0;
			p->image_url = cmark_node_get_url(node);
			p->image_title = cmark_node_get_title(node);
			return 1;
		} else {
			// EXIT: push image block with collected alt text
			p->in_image = 0;
			p->alt_buf[p->alt_len] = '\0';

			block.type = BLOCK_IMAGE;
			init_str_block(&block.block.image.url, p->image_url, -1);
			init_str_block(&block.block.image.title, p->image_title, -1);
			init_str_block(&block.block.image.alt, p->alt_buf, p->alt_len);
			return push_markdown_block(p, &block);
		}

	case CMARK_NODE_HTML_BLOCK:
	case CMARK_NODE_HTML_INLINE:
		// NIP-23 says: MUST NOT support adding HTML to Markdown
		// Skip HTML nodes entirely
		return 1;

	default:
		return 1;
	}
}

int ndb_parse_markdown_content(unsigned char *buf, int buf_size,
                               const char *content, int content_len,
                               struct ndb_blocks **blocks_p)
{
	struct ndb_markdown_parser parser;
	cmark_node *document;
	cmark_iter *iter;
	cmark_event_type ev_type;
	cmark_node *cur;
	unsigned char *blocks_start;

	// Initialize parser
	make_cursor(buf, buf + buf_size, &parser.buffer);

	// Allocate space for blocks header
	*blocks_p = parser.blocks = (struct ndb_blocks *)buf;
	parser.buffer.p += sizeof(struct ndb_blocks);

	make_cursor((unsigned char *)content,
	            (unsigned char *)content + content_len, &parser.content);

	parser.content_start = content;
	parser.in_image = 0;
	parser.alt_len = 0;
	parser.image_url = NULL;
	parser.image_title = NULL;
	parser.blocks->words = 0;
	parser.blocks->num_blocks = 0;
	parser.blocks->blocks_size = 0;
	parser.blocks->flags = 0;
	parser.blocks->version = 2;  // Version 2 for markdown blocks

	blocks_start = parser.buffer.p;

	// Parse markdown with cmark
	document = cmark_parse_document(content, content_len, CMARK_OPT_DEFAULT);
	if (!document)
		return 0;

	// Iterate through the AST
	iter = cmark_iter_new(document);

	while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
		cur = cmark_iter_get_node(iter);
		if (!parse_node(&parser, cur, ev_type)) {
			cmark_iter_free(iter);
			cmark_node_free(document);
			return 0;
		}
	}

	cmark_iter_free(iter);
	cmark_node_free(document);

	// Calculate word count (simple approximation from text blocks)
	parser.blocks->blocks_size = parser.buffer.p - blocks_start;

	// Pad to 8-byte alignment
	if (!cursor_align(&parser.buffer, 8))
		return 0;

	parser.blocks->total_size = parser.buffer.p - parser.buffer.start;

	return 1;
}

// Accessor functions for markdown blocks
struct ndb_heading_block *ndb_block_heading(struct ndb_block *block)
{
	if (block->type != BLOCK_HEADING)
		return NULL;
	return &block->block.heading;
}

struct ndb_code_block *ndb_block_code(struct ndb_block *block)
{
	if (block->type != BLOCK_CODE_BLOCK)
		return NULL;
	return &block->block.code_block;
}

struct ndb_list_block *ndb_block_list(struct ndb_block *block)
{
	if (block->type != BLOCK_LIST)
		return NULL;
	return &block->block.list;
}

struct ndb_link_block *ndb_block_link(struct ndb_block *block)
{
	if (block->type != BLOCK_LINK)
		return NULL;
	return &block->block.link;
}

struct ndb_image_block *ndb_block_image(struct ndb_block *block)
{
	if (block->type != BLOCK_IMAGE)
		return NULL;
	return &block->block.image;
}
