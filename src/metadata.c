
#include "nostrdb.h"
#include "binmoji.h"

// these must be byte-aligned, they are directly accessing the serialized data
// representation
#pragma pack(push, 1)

// 16 bytes
struct ndb_note_meta_entry {
	// 4 byte entry header
	uint16_t type;
	uint16_t flags;

	// additional 4 bytes of aux storage for payloads that are >8 bytes
	//
	// for reactions types, this is used for counts
	// normally this would have been padding but we make use of it
	// in our manually packed structure
	uint32_t aux;

	// 8 byte metadata payload
	union {
		uint64_t value;

		struct {
			uint32_t offset;
			uint32_t padding;
		} offset;

		// the reaction binmoji[1] for reaction, count is stored in aux
		union ndb_reaction_str reaction_str;
	} payload;
};
STATIC_ASSERT(sizeof(struct ndb_note_meta_entry) == 16, note_meta_entry_should_be_16_bytes);

/* newtype wrapper around the header entry */
struct ndb_note_meta {
	// 4 bytes
	uint8_t version;
	uint8_t padding;
	uint16_t count;

	// 4 bytes
	uint32_t data_table_size;

	// 8 bytes
	uint64_t flags;
};
STATIC_ASSERT(sizeof(struct ndb_note_meta) == 16, note_meta_entry_should_be_16_bytes);

#pragma pack(pop)

int ndb_reaction_str_is_emoji(union ndb_reaction_str str) 
{
	return binmoji_get_user_flag(str.binmoji) == 0;
}

uint16_t ndb_note_meta_entries_count(struct ndb_note_meta *meta)
{
	return meta->count;
}

static int ndb_reaction_set_emoji(union ndb_reaction_str *str, const char *emoji)
{
	struct binmoji binmoji;
	/* TODO: parse failures? */
	binmoji_parse(emoji, &binmoji);
	str->binmoji = binmoji_encode(&binmoji);
	return 1;
}

static int ndb_reaction_set_str(union ndb_reaction_str *reaction, const char *str) 
{
	int i;
	char c;

	/* this is like memset'ing the packed string to all 0s as well */
	reaction->binmoji = 0;
	
	/* set the binmoji user flag so we can catch corrupt binmojis */
	/* this is in the LSB so it will only touch reaction->packed.flag  */
	reaction->binmoji = binmoji_set_user_flag(reaction->binmoji, 1);
	assert(reaction->packed.flag != 0);

	for (i = 0; i < 7; i++) {
		c = str[i];
		/* string is too big */
		if (i == 6 && c != '\0')
			return 0;
		reaction->packed.str[i] = c;
		if (c == '\0')
			return 1;
	}

	return 0;
}

/* set the value of an ndb_reaction_str to an emoji or small string */
int ndb_reaction_set(union ndb_reaction_str *reaction, const char *str)
{
	struct binmoji binmoji;
	char output_emoji[136];

	/* our variant of emoji detection is to simply try to create
	 * a binmoji and parse it again. if we round-trip successfully
	 * then we know its an emoji, or at least a simple string
	 */
	binmoji_parse(str, &binmoji);
	reaction->binmoji = binmoji_encode(&binmoji);
	binmoji_to_string(&binmoji, output_emoji, sizeof(output_emoji));

	/* round trip is successful, let's just use binmojis for this encoding */
	if (!strcmp(output_emoji, str))
		return 1;

	/* no round trip? let's just set a non-emoji string */
	return ndb_reaction_set_str(reaction, str);
}

static void ndb_note_meta_header_init(struct ndb_note_meta *meta)
{
	meta->version = 1;
	meta->flags = 0;
	meta->count = 0;
	meta->data_table_size = 0;
}

static inline size_t ndb_note_meta_entries_size(struct ndb_note_meta *meta)
{
	return (sizeof(struct ndb_note_meta_entry) * meta->count);
}

void *ndb_note_meta_data_table(struct ndb_note_meta *meta, size_t *size)
{
	return meta + ndb_note_meta_entries_size(meta);
}

size_t ndb_note_meta_total_size(struct ndb_note_meta *header)
{
	size_t total_size = sizeof(*header) + header->data_table_size + ndb_note_meta_entries_size(header);
	assert((total_size % 8) == 0);
	return total_size;
}

struct ndb_note_meta_entry *ndb_note_meta_add_entry(struct ndb_note_meta_builder *builder)
{
	struct ndb_note_meta *header = (struct ndb_note_meta *)builder->cursor.start;
	struct ndb_note_meta_entry *entry = NULL;

	assert(builder->cursor.p != builder->cursor.start);

	if (!(entry = cursor_malloc(&builder->cursor, sizeof(*entry))))
		return NULL;

	/* increase count entry count */
	header->count++;

	return entry;
}

int ndb_note_meta_builder_init(struct ndb_note_meta_builder *builder, unsigned char *buf, size_t bufsize)
{
	make_cursor(buf, buf + bufsize, &builder->cursor);

	/* allocate some space for the header */
	if (!cursor_malloc(&builder->cursor, sizeof(struct ndb_note_meta)))
		return 0;

	ndb_note_meta_header_init((struct ndb_note_meta*)builder->cursor.start);

	return 1;
}

/* note flags are stored in the header entry */
uint32_t ndb_note_meta_flags(struct ndb_note_meta *meta)
{
	return meta->flags;
}

/* note flags are stored in the header entry */
void ndb_note_meta_set_flags(struct ndb_note_meta *meta, uint32_t flags)
{
	meta->flags = flags;
}

static int compare_entries(const void *a, const void *b)
{
	struct ndb_note_meta_entry *entry_a, *entry_b;
	uint64_t binmoji_a, binmoji_b;
	int res;

	entry_a = (struct ndb_note_meta_entry *)a;
	entry_b = (struct ndb_note_meta_entry *)b;

	res = entry_a->type - entry_b->type;

	if (res == 0 && entry_a->type == NDB_NOTE_META_REACTION) {
		/* we sort by reaction string for stability */
		binmoji_a = entry_a->payload.reaction_str.binmoji;
		binmoji_b = entry_b->payload.reaction_str.binmoji;

		if (binmoji_a < binmoji_b) {
			return -1;
		} else if (binmoji_a > binmoji_b) {
			return 1;
		} else {
			return 0;
		}
	} else {
		return res;
	}
}

struct ndb_note_meta_entry *ndb_note_meta_entries(struct ndb_note_meta *meta)
{
	/* entries start at the end of the header record */
	return (struct ndb_note_meta_entry *)((uint8_t*)meta + sizeof(*meta));
}

void ndb_note_meta_build(struct ndb_note_meta_builder *builder, struct ndb_note_meta **meta)
{
	/* sort entries */
	struct ndb_note_meta_entry *entries;
	struct ndb_note_meta *header = (struct ndb_note_meta*)builder->cursor.start;

	/* not initialized */
	assert(builder->cursor.start != builder->cursor.p);

	if (header->count > 0) {
		entries = ndb_note_meta_entries(header);

		/* ensure entries are always sorted so bsearch is possible for large metadata
		 * entries. probably won't need that for awhile though */
		qsort(entries, header->count, sizeof(struct ndb_note_meta_entry), compare_entries);
	}

	*meta = header;
	return;
}

/* find a metadata entry, optionally matching a payload */
struct ndb_note_meta_entry *ndb_note_meta_find_entry(struct ndb_note_meta *meta, uint16_t type, uint64_t *payload)
{
	struct ndb_note_meta_entry *entries, *entry;
	int i;

	if (meta->count == 0)
		return NULL;

	entries = ndb_note_meta_entries(meta);

	for (i = 0; i < meta->count; i++) {
		entry = &entries[i];
		if (entry->type != type)
			continue;
		if (payload && *payload != entry->payload.value)
			continue;
		return entry;
	}

	return NULL;
}

void ndb_note_meta_reaction_set(struct ndb_note_meta_entry *entry, uint32_t count, union ndb_reaction_str str)
{
	entry->type = NDB_NOTE_META_REACTION;
	entry->flags = 0;
	entry->aux = count;
	entry->payload.reaction_str = str;
}

/* sets the quote repost count for this note */
void ndb_note_meta_quotes_set(struct ndb_note_meta_entry *entry, uint32_t count)
{
	entry->type = NDB_NOTE_META_QUOTES;
	entry->flags = 0;
	entry->aux = count;
	/* unused */
	entry->payload.value = 0;
}

uint32_t ndb_note_meta_reaction_count(struct ndb_note_meta_entry *entry)
{
	return entry->aux;
}

void ndb_note_meta_reaction_set_count(struct ndb_note_meta_entry *entry, uint32_t count)
{
	entry->aux = count;
}

union ndb_reaction_str ndb_note_meta_reaction_str(struct ndb_note_meta_entry *entry)
{
	return entry->payload.reaction_str;
}
