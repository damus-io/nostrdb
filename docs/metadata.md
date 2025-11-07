
# Note Metadata

nostrdb supports a flexible metadata system which allows you to store additional information about a nostr note.

metadata is stored as a sorted list of TVs (tag, value). The lengths are a fixed size but can reference a data table.

The type follows the "it's ok to be odd" rule. odd tags are opaque, user defined types that can store data of any kind.

## Binary format

The binary format starts with a header containing the number of metadata entries. The count is followed by a sorted, aligned list of these entries:

```c

// 16 bytes
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

// 16 bytes
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
	union {
		uint32_t value;

		/* if this is a thread root, this counts the total replies in the thread */
		uint32_t total_reactions;
	} aux;

	// 8 byte metadata payload
	union {
		uint64_t value;

		struct {
			uint32_t offset;
			uint32_t padding;
		} offset;

		struct {
			/* number of direct replies */
			uint16_t direct_replies;
			uint16_t quotes;

			/* number of replies in this thread */
			uint32_t thread_replies;
		} counts;

		// the reaction binmoji[1] for reaction, count is stored in aux
		union ndb_reaction_str reaction_str;
	} payload;
};

```

The offset is to a chunk of potentially unaligned data:

```
size : varint
data : u8[size]
```

### Rationale

We want the following properties:

* The ability to quickly iterate/skip over different metadata fields.  This is achieved by a fixed table format, without needing to encode/decode like blocks
* The ability to quickly update metadata fields.  To update flags or counts can be done via race-safe mutation operations. The mutation can be done inplace (memcpy + poke memory address)
* The table entries can be inplace sorted for binary search lookups on large metadata tables

## Write thread mutation operations

We want to support many common operations:

* Increase reaction count for a specific reaction type

[binmoji]: https://github.com/jb55/binmoji
