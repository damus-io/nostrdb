# Changelog

## [Unreleased]

### Added
- NIP-23 long-form content support (kind 30023/30024)
- Markdown parsing via libcmark integration
- 14 new block types for markdown elements: HEADING, PARAGRAPH, CODE_BLOCK, BLOCKQUOTE, LIST, LIST_ITEM, THEMATIC_BREAK, EMPH, STRONG, LINK, IMAGE, CODE_INLINE, LINEBREAK, SOFTBREAK
- `ndb_parse_markdown_content()` - parse markdown into nostrdb blocks
- NIP-23 metadata helpers: `ndb_note_is_longform()`, `ndb_note_longform_title()`, `ndb_note_longform_image()`, `ndb_note_longform_summary()`, `ndb_note_longform_identifier()`, `ndb_note_longform_published_at()`, `ndb_note_longform_hashtags()`
- Automatic `nostr:` URI detection in markdown links (converted to BLOCK_MENTION_BECH32)
- Image alt text capture from markdown
- Version field in block iterator for v1/v2 format distinction

### Fixed
- `cursor_skip()` off-by-one bug preventing reads to exact buffer end
