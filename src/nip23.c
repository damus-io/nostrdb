/**
 * nip23.c - NIP-23 long-form content helpers
 *
 * Provides metadata extraction for NIP-23 articles (kind 30023/30024).
 * Extracts title, summary, image, identifier, published_at, and hashtags.
 */

#include "nostrdb.h"
#include <string.h>
#include <stdlib.h>

#define NIP23_KIND_LONGFORM       30023
#define NIP23_KIND_LONGFORM_DRAFT 30024

// Check if a note is a NIP-23 long-form article
int ndb_note_is_longform(struct ndb_note *note)
{
	uint32_t kind = ndb_note_kind(note);
	return kind == NIP23_KIND_LONGFORM || kind == NIP23_KIND_LONGFORM_DRAFT;
}

// Check if a note is a NIP-23 draft
int ndb_note_is_longform_draft(struct ndb_note *note)
{
	return ndb_note_kind(note) == NIP23_KIND_LONGFORM_DRAFT;
}

// Helper to find a tag with a specific key and return the value
static struct ndb_str find_tag_value(struct ndb_note *note, const char *tag_key)
{
	struct ndb_iterator iter;
	struct ndb_str str;
	struct ndb_str empty = {0, {NULL}};

	ndb_tags_iterate_start(note, &iter);

	while (ndb_tags_iterate_next(&iter)) {
		if (ndb_tag_count(iter.tag) < 2)
			continue;

		str = ndb_iter_tag_str(&iter, 0);
		if (str.str == NULL)
			continue;

		// Check if this is the tag we're looking for
		if (strcmp(str.str, tag_key) == 0) {
			return ndb_iter_tag_str(&iter, 1);
		}
	}

	return empty;
}

// Get the article title from a NIP-23 note
struct ndb_str ndb_note_longform_title(struct ndb_note *note)
{
	return find_tag_value(note, "title");
}

// Get the article image URL from a NIP-23 note
struct ndb_str ndb_note_longform_image(struct ndb_note *note)
{
	return find_tag_value(note, "image");
}

// Get the article summary from a NIP-23 note
struct ndb_str ndb_note_longform_summary(struct ndb_note *note)
{
	return find_tag_value(note, "summary");
}

// Get the d-tag (identifier) from a NIP-23 note
struct ndb_str ndb_note_longform_identifier(struct ndb_note *note)
{
	return find_tag_value(note, "d");
}

// Get the published_at timestamp from a NIP-23 note
// Returns 0 if not found or invalid
uint64_t ndb_note_longform_published_at(struct ndb_note *note)
{
	struct ndb_str str = find_tag_value(note, "published_at");

	if (str.str == NULL)
		return 0;

	// Parse the string as a Unix timestamp
	char *endptr;
	uint64_t timestamp = strtoull(str.str, &endptr, 10);

	// Check if parsing was successful
	if (endptr == str.str)
		return 0;

	return timestamp;
}

// Get all hashtags from a NIP-23 note
// Returns the number of hashtags found, fills tags array up to max_tags
int ndb_note_longform_hashtags(struct ndb_note *note, struct ndb_str *tags, int max_tags)
{
	struct ndb_iterator iter;
	struct ndb_str str;
	int count = 0;

	ndb_tags_iterate_start(note, &iter);

	while (ndb_tags_iterate_next(&iter) && count < max_tags) {
		if (ndb_tag_count(iter.tag) < 2)
			continue;

		str = ndb_iter_tag_str(&iter, 0);
		if (str.str == NULL)
			continue;

		// Check if this is a 't' tag (hashtag)
		if (strcmp(str.str, "t") == 0) {
			tags[count] = ndb_iter_tag_str(&iter, 1);
			count++;
		}
	}

	return count;
}
