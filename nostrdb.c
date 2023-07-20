
#include "nostrdb.h"
#include "jsmn.h"
#include "hex.h"
#include "cursor.h"
#include <stdlib.h>

struct ndb_json_parser {
	struct ndb_builder builder;
	struct cursor buf;
};

static inline int
cursor_push_tag(struct cursor *cur, struct ndb_tag *tag) {
	return cursor_push_u16(cur, tag->count);
}

int
ndb_builder_new(struct ndb_builder *builder, int *bufsize) {
	struct ndb_note *note;
	struct cursor mem;
	// 1MB + 0.5MB
	builder->size = 1024 * 1024;
	if (bufsize)
		builder->size = *bufsize;

	int str_indices_size = builder->size / 32;
	unsigned char *bytes = malloc(builder->size + str_indices_size);
	if (!bytes) return 0;

	make_cursor(bytes, bytes + builder->size + str_indices_size, &mem);

	note = builder->note = (struct ndb_note *)bytes;
	size_t half = builder->size >> 1;

	if (!cursor_slice(&mem, &builder->note_cur, half)) return 0;
	if (!cursor_slice(&mem, &builder->strings, half)) return 0;
	if (!cursor_slice(&mem, &builder->str_indices, str_indices_size)) return 0;

	memset(note, 0, sizeof(*note));
	builder->note_cur.p += sizeof(*note);

	note->version = 1;

	return 1;
}

int
ndb_builder_finalize(struct ndb_builder *builder, struct ndb_note **note) {
	int strings_len = builder->strings.p - builder->strings.start;
	unsigned char *end = builder->note_cur.p + strings_len;
	int total_size = end - builder->note_cur.start;

	// move the strings buffer next to the end of our ndb_note
	memmove(builder->note_cur.p, builder->strings.start, strings_len);

	// set the strings location
	builder->note->strings = builder->note_cur.p - builder->note_cur.start;

	// remove any extra data
	*note = realloc(builder->note_cur.start, total_size);

	return total_size;
}

struct ndb_note *
ndb_builder_note(struct ndb_builder *builder) {
	return builder->note;
}

int
ndb_builder_make_string(struct ndb_builder *builder, const char *str, int len, union packed_str *pstr) {
	uint32_t loc;

	if (len == 0) {
		*pstr = ndb_char_to_packed_str(0);
		return 1;
	} else if (len == 1) {
		*pstr = ndb_char_to_packed_str(str[0]);
		return 1;
	} else if (len == 2) {
		*pstr = ndb_chars_to_packed_str(str[0], str[1]);
		return 1;
	}

	// find existing matching string to avoid duplicate strings
	int indices = cursor_count(&builder->str_indices, sizeof(uint32_t));
	for (int i = 0; i < indices; i++) {
		uint32_t index = ((uint32_t*)builder->str_indices.start)[i];
		const char *some_str = (const char*)builder->strings.start + index;

		if (!strcmp(some_str, str)) {
			// found an existing matching str, use that index
			*pstr = ndb_offset_str(index);
			return 1;
		}
	}

	// no string found, push a new one
	loc = builder->strings.p - builder->strings.start;
	if (!(cursor_push(&builder->strings, (unsigned char*)str, len) &&
	      cursor_push_byte(&builder->strings, '\0'))) {
		return 0;
	}
	*pstr = ndb_offset_str(loc);

	// record in builder indices. ignore return value, if we can't cache it
	// then whatever
	cursor_push_u32(&builder->str_indices, loc);

	return 1;
}

int
ndb_builder_set_content(struct ndb_builder *builder, const char *content, int len) {
	return ndb_builder_make_string(builder, content, len, &builder->note->content);
}


static inline int
jsoneq(const char *json, jsmntok_t *tok, int tok_len, const char *s) {
	if (tok->type == JSMN_STRING && (int)strlen(s) == tok_len &&
	    memcmp(json + tok->start, s, tok_len) == 0) {
		return 1;
	}
	return 0;
}

static inline int toksize(jsmntok_t *tok) {
	return tok->end - tok->start;
}

static inline int
build_tag_from_json_tokens(const char *json, jsmntok_t *tag, struct ndb_builder *builder) {
	printf("tag %.*s %d\n", toksize(tag), json + tag->start, tag->type);

	return 1;
}

static inline int
ndb_builder_process_json_tags(const char *json, jsmntok_t *array, struct ndb_builder *builder) {
	jsmntok_t *tag = array;
	printf("json_tags %.*s %d\n", toksize(tag), json + tag->start, tag->type);

	for (int i = 0; i < array->size; i++) {
		if (!build_tag_from_json_tokens(json, &tag[i+1], builder))
			return 0;
		tag += array->size;
	}

	return 1;
}


int
ndb_note_from_json(const char *json, int len, struct ndb_note **note) {
	jsmntok_t toks[4096], *tok = NULL;
	unsigned char buf[64];
	struct ndb_builder builder;
	jsmn_parser p;

	int i, r, tok_len;
	const char *start;

	jsmn_init(&p);
	ndb_builder_new(&builder, &len);

	r = jsmn_parse(&p, json, len, toks, sizeof(toks)/sizeof(toks[0]));

	if (r < 0) return 0;
	if (r < 1 || toks[0].type != JSMN_OBJECT) return 0;

	for (i = 1; i < r; i++) {
		tok = &toks[i];
		start = json + tok->start;
		tok_len = toksize(tok);

		//printf("toplevel %.*s %d\n", tok_len, json + tok->start, tok->type);
		if (tok_len == 0 || i + 1 >= r)
			continue;

		if (start[0] == 'p' && jsoneq(json, tok, tok_len, "pubkey")) {
			// pubkey
			tok = &toks[i+1];
			hex_decode(json + tok->start, toksize(tok), buf, sizeof(buf));
			ndb_builder_set_pubkey(&builder, buf);
		} else if (tok_len == 2 && start[0] == 'i' && start[1] == 'd') {
			// id
			tok = &toks[i+1];
			hex_decode(json + tok->start, toksize(tok), buf, sizeof(buf));
			// TODO: validate id
			ndb_builder_set_id(&builder, buf);
		} else if (tok_len == 3 && start[0] == 's' && start[1] == 'i' && start[2] == 'g') {
			// sig
			tok = &toks[i+1];
			hex_decode(json + tok->start, toksize(tok), buf, sizeof(buf));
			ndb_builder_set_signature(&builder, buf);
		} else if (start[0] == 'k' && jsoneq(json, tok, tok_len, "kind")) {
			// kind
			tok = &toks[i+1];
			printf("json_kind %.*s\n", toksize(tok), json + tok->start);
		} else if (start[0] == 'c') {
			if (jsoneq(json, tok, tok_len, "created_at")) {
				// created_at
				tok = &toks[i+1];
				printf("json_created_at %.*s\n", toksize(tok), json + tok->start);
			} else if (jsoneq(json, tok, tok_len, "content")) {
				// content
				tok = &toks[i+1];
				if (!ndb_builder_set_content(&builder, json + tok->start, toksize(tok)))
					return 0;
			}
		} else if (start[0] == 't' && jsoneq(json, tok, tok_len, "tags")) {
			tok = &toks[i+1];
			ndb_builder_process_json_tags(json, tok, &builder);
			i += tok->size;
		}
	}

	return ndb_builder_finalize(&builder, note);
}

void
ndb_builder_set_pubkey(struct ndb_builder *builder, unsigned char *pubkey) {
	memcpy(builder->note->pubkey, pubkey, 32);
}

void
ndb_builder_set_id(struct ndb_builder *builder, unsigned char *id) {
	memcpy(builder->note->id, id, 32);
}

void
ndb_builder_set_signature(struct ndb_builder *builder, unsigned char *signature) {
	memcpy(builder->note->signature, signature, 64);
}

void
ndb_builder_set_kind(struct ndb_builder *builder, uint32_t kind) {
	builder->note->kind = kind;
}

int
ndb_builder_add_tag(struct ndb_builder *builder, const char **strs, uint16_t num_strs) {
	int i;
	union packed_str pstr;
	const char *str;
	struct ndb_tag tag;

	builder->note->tags.count++;
	tag.count = num_strs;

	if (!cursor_push_tag(&builder->note_cur, &tag))
		return 0;

	for (i = 0; i < num_strs; i++) {
		str = strs[i];
		if (!ndb_builder_make_string(builder, str, strlen(str), &pstr))
			return 0;
		if (!cursor_push_u32(&builder->note_cur, pstr.offset))
			return 0;
	}

	return 1;
}

