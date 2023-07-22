
#include "nostrdb.h"
#include "hex.h"
#include "io.h"

#include <stdio.h>
#include <assert.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

static void test_basic_event() {
	unsigned char buf[512];
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

	ok = ndb_builder_new(b, buf, sizeof(buf));
	assert(ok);
	note = builder.note;

	memset(note->padding, 3, sizeof(note->padding));

	const char *content = "hello, world!";

	ok = ndb_builder_set_content(b, content, strlen(content)); assert(ok);
	ndb_builder_set_id(b, id); assert(ok);
	ndb_builder_set_pubkey(b, pubkey); assert(ok);
	ndb_builder_set_signature(b, sig); assert(ok);

	ok = ndb_builder_new_tag(b); assert(ok);
	ok = ndb_builder_push_tag_str(b, "p", 1); assert(ok);
	ok = ndb_builder_push_tag_str(b, hex_pk, 64); assert(ok);

	ok = ndb_builder_new_tag(b); assert(ok);
	ok = ndb_builder_push_tag_str(b, "word", 4); assert(ok);
	ok = ndb_builder_push_tag_str(b, "words", 5); assert(ok);
	ok = ndb_builder_push_tag_str(b, "w", 1); assert(ok);

	ok = ndb_builder_finalize(b, &note);
	assert(ok);

	assert(note->tags.count == 2);

	// test iterator
	struct ndb_iterator iter, *it = &iter;
	
	ok = ndb_tags_iterate_start(note, it);
	assert(ok);
	assert(it->tag->count == 2);
	const char *p   = ndb_iter_tag_str(it, 0);
	const char *hpk = ndb_iter_tag_str(it, 1);
	assert(hpk);
	assert(!strcmp(p, "p"));
	assert(!strcmp(hpk, hex_pk));

	ok = ndb_tags_iterate_next(it);
	assert(ok);
	assert(it->tag->count == 3);
	assert(!strcmp(ndb_iter_tag_str(it, 0), "word"));
	assert(!strcmp(ndb_iter_tag_str(it, 1), "words"));
	assert(!strcmp(ndb_iter_tag_str(it, 2), "w"));

	ok = ndb_tags_iterate_next(it);
	assert(!ok);
}

static void test_empty_tags() {
	struct ndb_builder builder, *b = &builder;
	struct ndb_iterator iter, *it = &iter;
	struct ndb_note *note;
	int ok;
	unsigned char buf[1024];

	ok = ndb_builder_new(b, buf, sizeof(buf));
	assert(ok);

	ok = ndb_builder_finalize(b, &note);
	assert(ok);

	assert(note->tags.count == 0);

	ok = ndb_tags_iterate_start(note, it);
	assert(!ok);
}

static void test_parse_contact_list()
{
	int size, written;
	static const int alloc_size = 2 << 18;
	unsigned char *json = malloc(alloc_size);
	unsigned char *buf = malloc(alloc_size);
	struct ndb_note *note;

	read_file("contacts.json", json, alloc_size, &written);

	size = ndb_note_from_json((const char*)json, written, &note, buf, alloc_size);
	printf("ndb_note_from_json size %d\n", size);
	assert(size > 0);

	free(json);
	free(buf);
}

static void test_parse_json() {
	char hex_id[65] = {0};
	unsigned char buffer[1024];
	struct ndb_note *note;
#define HEX_ID "5004a081e397c6da9dc2f2d6b3134006a9d0e8c1b46689d9fe150bb2f21a204d"
#define HEX_PK "b169f596968917a1abeb4234d3cf3aa9baee2112e58998d17c6db416ad33fe40"
	static const char *json = 
		"{\"id\": \"" HEX_ID "\",\"pubkey\": \"" HEX_PK "\",\"created_at\": 1689836342,\"kind\": 1,\"tags\": [[\"p\",\"" HEX_ID "\"], [\"word\", \"words\", \"w\"]],\"content\": \"共通語\",\"sig\": \"e4d528651311d567f461d7be916c37cbf2b4d530e672f29f15f353291ed6df60c665928e67d2f18861c5ca88\"}";
	int ok;

	ok = ndb_note_from_json(json, strlen(json), &note, buffer, sizeof(buffer));
	assert(ok);

	const char *content = ndb_note_content(note);
	unsigned char *id = ndb_note_id(note);

	hex_encode(id, 32, hex_id, sizeof(hex_id));

	assert(!strcmp(content, "共通語"));
	assert(!strcmp(HEX_ID, hex_id));

	assert(note->tags.count == 2);

	struct ndb_iterator iter, *it = &iter;
	ok = ndb_tags_iterate_start(note, it); assert(ok);
	assert(it->tag->count == 2);
	assert(!strcmp(ndb_iter_tag_str(it, 0), "p"));
	assert(!strcmp(ndb_iter_tag_str(it, 1), HEX_ID));

	ok = ndb_tags_iterate_next(it); assert(ok);
	assert(it->tag->count == 3);
	assert(!strcmp(ndb_iter_tag_str(it, 0), "word"));
	assert(!strcmp(ndb_iter_tag_str(it, 1), "words"));
	assert(!strcmp(ndb_iter_tag_str(it, 2), "w"));
}

int main(int argc, const char *argv[]) {
	test_basic_event();
	test_empty_tags();
	test_parse_json();
	test_parse_contact_list();
}
