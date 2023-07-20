
#include "nostrdb.h"

#include <stdio.h>
#include <assert.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

static void test_basic_event() {
	struct ndb_builder builder, *b = &builder;
	struct ndb_note *note;
	int len, ok;

	unsigned char id[32];
	memset(id, 1, 32);

	unsigned char pubkey[32];
	memset(pubkey, 2, 32);

	unsigned char sig[64];
	memset(sig, 3, 64);


	const char *hex_pk = "5d9b81b2d4d5609c5565286fc3b511dc6b9a1b3d7d1174310c624d61d1f82bb9";
	const char *tag[] = { "p", hex_pk };
	const char *word_tag[] = { "word", "words", "w" };

	ndb_builder_new(b, 0);
	note = builder.note;

	memset(note->padding, 3, sizeof(note->padding));

	ndb_builder_set_content(b, "hello, world!");
	ndb_builder_set_id(b, id);
	ndb_builder_set_pubkey(b, pubkey);
	ndb_builder_set_signature(b, sig);
	ndb_builder_add_tag(b, tag, ARRAY_SIZE(tag));
	ndb_builder_add_tag(b, word_tag, ARRAY_SIZE(word_tag));

	len = ndb_builder_finalize(b, &note);

	assert(note->tags.count == 2);

	// test iterator
	struct ndb_iterator iter, *it = &iter;
	
	ok = ndb_tags_iterate_start(note, it);
	assert(ok);
	assert(it->tag->count == 2);
	const char *p   = ndb_note_string(note, &it->tag->strs[0]);
	const char *hpk = ndb_note_string(note, &it->tag->strs[1]);
	assert(hpk);
	assert(!ndb_str_is_packed(it->tag->strs[1]));
	assert(!strcmp(hpk, hex_pk));
	assert(!strcmp(p, "p"));

	ok = ndb_tags_iterate_next(it);
	assert(ok);
	assert(it->tag->count == 3);
	assert(!strcmp(ndb_note_string(note, &it->tag->strs[0]), "word"));
	assert(!strcmp(ndb_note_string(note, &it->tag->strs[1]), "words"));
	assert(!strcmp(ndb_note_string(note, &it->tag->strs[2]), "w"));

	ok = ndb_tags_iterate_next(it);
	assert(!ok);
}

static void test_empty_tags() {
	struct ndb_builder builder, *b = &builder;
	struct ndb_iterator iter, *it = &iter;
	struct ndb_note *note;
	int ok, len;

	ndb_builder_new(b, 0);
	len = ndb_builder_finalize(b, &note);

	assert(note->tags.count == 0);

	ok = ndb_tags_iterate_start(note, it);
	assert(!ok);
}

int main(int argc, const char *argv[]) {
	test_basic_event();
	test_empty_tags();
}
