
#include "nostrdb.h"
#include <stdlib.h>

int ndb_builder_new(struct ndb_builder *builder, int *bufsize) {
	struct ndb_note *note;
	struct cursor mem;
	// 1MB + 0.5MB
	builder->size = 1024 * 1024;
	if (bufsize)
		builder->size = *bufsize;

	int str_indices_size = sizeof(uint32_t) * (2<<14);
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

int ndb_builder_finalize(struct ndb_builder *builder, struct ndb_note **note) {
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

struct ndb_note *ndb_builder_note(struct ndb_builder *builder) {
	return builder->note;
}

int ndb_builder_make_string(struct ndb_builder *builder, const char *str, union packed_str *pstr) {
	int len = strlen(str);
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

int ndb_builder_set_content(struct ndb_builder *builder, const char *content) {
	return ndb_builder_make_string(builder, content, &builder->note->content);
}

void ndb_builder_set_pubkey(struct ndb_builder *builder, unsigned char *pubkey) {
	memcpy(builder->note->pubkey, pubkey, 32);
}

void ndb_builder_set_id(struct ndb_builder *builder, unsigned char *id) {
	memcpy(builder->note->id, id, 32);
}

void ndb_builder_set_signature(struct ndb_builder *builder, unsigned char *signature) {
	memcpy(builder->note->signature, signature, 64);
}

void ndb_builder_set_kind(struct ndb_builder *builder, uint32_t kind) {
	builder->note->kind = kind;
}

static inline int cursor_push_tag(struct cursor *cur, struct ndb_tag *tag) {
	return cursor_push_u16(cur, tag->count);
}

int ndb_builder_add_tag(struct ndb_builder *builder, const char **strs, uint16_t num_strs) {
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
		if (!ndb_builder_make_string(builder, str, &pstr))
			return 0;
		if (!cursor_push_u32(&builder->note_cur, pstr.offset))
			return 0;
	}

	return 1;
}

