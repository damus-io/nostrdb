
#include "nostrdb.h"
#include "hex.h"

#include <stdio.h>
#include <assert.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

static void test_basic_event() {
	struct ndb_builder builder, *b = &builder;
	struct ndb_note *note;
	int ok;

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

	const char *content = "hello, world!";
	ndb_builder_set_content(b, content, strlen(content));
	ndb_builder_set_id(b, id);
	ndb_builder_set_pubkey(b, pubkey);
	ndb_builder_set_signature(b, sig);
	ndb_builder_add_tag(b, tag, ARRAY_SIZE(tag));
	ndb_builder_add_tag(b, word_tag, ARRAY_SIZE(word_tag));

	ndb_builder_finalize(b, &note);

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
	int ok;

	ndb_builder_new(b, 0);
	ndb_builder_finalize(b, &note);

	assert(note->tags.count == 0);

	ok = ndb_tags_iterate_start(note, it);
	assert(!ok);
}


static void test_parse_json() {
	char hex_id[65] = {0};
	struct ndb_note *note;
#define HEX_ID "5004a081e397c6da9dc2f2d6b3134006a9d0e8c1b46689d9fe150bb2f21a204d"
	static const char *json = 
		"{\"id\": \"" HEX_ID "\",\"pubkey\": \"b169f596968917a1abeb4234d3cf3aa9baee2112e58998d17c6db416ad33fe40\",\"created_at\": 1689836342,\"kind\": 1,\"tags\": [],\"content\": \"共通語\",\"sig\": \"e4d528651311d567f461d7be916c37cbf2b4d530e672f29f15f353291ed6df60c665928e67d2f18861c5ca88\"}";

	ndb_note_from_json(json, strlen(json), &note);

	const char *content = ndb_note_content(note);
	unsigned char *id = ndb_note_id(note);
	hex_encode(id, 32, hex_id, sizeof(hex_id));

	assert(!strcmp(content, "共通語"));
	assert(!strcmp(HEX_ID, hex_id));
}

int main(int argc, const char *argv[]) {
	test_basic_event();
	test_empty_tags();
	test_parse_json();
}
