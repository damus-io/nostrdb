/* A read-only cursor into a buffer.  You can pull as many times as you want,
 * and check at the end if it is valid. */
#ifndef JB55_RCUR_H
#define JB55_RCUR_H

#include "ccan/compiler/compiler.h"
#include "ccan/likely/likely.h"
#include "cursor.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

struct rcur {
    const unsigned char *cur;
    size_t len;
};

/* Is this valid?  Pulling too much (or invalid values) sets this true */
static inline bool rcur_valid(const struct rcur *rcur)
{
    return unlikely(rcur->cur != NULL);
}

/* How many bytes left to pull?  0 if invalid */
static inline size_t rcur_bytes_remaining(struct rcur rcur)
{
    return rcur.len;
}

/* Pull @len bytes from rcur, if space available.
 * Return NULL if not valid.
 */
const void *rcur_pull(struct rcur *rcur, size_t len);

/* Access the remaining bytes.  Returns NULL and *len=0 if invalid. */
const void *rcur_peek_remainder(const struct rcur *rcur, size_t *len);

/* Copy @len bytes from rcur, if space available.
 * Return false if not valid.
 */
bool rcur_copy(struct rcur *rcur, void *dst, size_t len);

/* Look ahead: returns NULL if @len too long.  Does *not* alter
 * rcur */
const void *rcur_peek(const struct rcur *rcur, size_t len);

/* Mark this rcur invalid: return false for convenience. */
static inline bool COLD rcur_fail(struct rcur *rcur)
{
    rcur->cur = NULL;
    rcur->len = 0;
    return false;
}

/* Create rcur for buffer */
static inline struct rcur rcur_forbuf(const void *buf, size_t len)
{
    struct rcur rcur;
    rcur.cur = buf;
    rcur.len = len;

    return rcur;
}

/* Create rcur for string */
static inline struct rcur rcur_forstr(const char *str)
{
    struct rcur rcur;
    rcur.cur = (const unsigned char *)str;
    rcur.len = strlen(str);

    return rcur;
}

/* Create rcur for next len bytes in rcur */
static inline struct rcur rcur_pull_slice(struct rcur *rcur, size_t len)
{
    struct rcur slice;
    const unsigned char *p;

    p = rcur_pull(rcur, len);
    if (likely(p))
        slice = rcur_forbuf(p, len);
    else
        rcur_fail(&slice);
    return slice;
}

/* Get rcur between these two: newer has been pulled from */
static inline struct rcur rcur_between(const struct rcur *orig,
                                       const struct rcur *newer)
{
    struct rcur rcur;

    if (rcur_valid(newer)) {
        assert(newer->cur >= orig->cur);
        assert(newer->cur + newer->len <= orig->cur + orig->len);
        rcur = rcur_forbuf(orig->cur, newer->cur - orig->cur);
    } else {
        rcur_fail(&rcur);
    }
    return rcur;
}

/* All these are based on rcur_pull. */
bool rcur_skip(struct rcur *rcur, size_t len);
unsigned char rcur_pull_byte(struct rcur *rcur);
uint16_t rcur_pull_u16(struct rcur *rcur);
uint32_t rcur_pull_u32(struct rcur *rcur);
uint64_t rcur_pull_varint(struct rcur *rcur);
uint32_t rcur_pull_varint_u32(struct rcur *rcur);

/* Trim this character from the end of buffer, if present.  Returns
 * true if trimmed. */
bool rcur_trim_if_char(struct rcur *rcur, char c);

/* Remove is_whitespace(), return bytes removed */
size_t rcur_pull_whitespace(struct rcur *rcur);

/* Remove is_punctuation(), return bytes removed */
size_t rcur_pull_punctuation(struct rcur *rcur);

/* Remove !is_alphanumeric(), return bytes removed */
size_t rcur_pull_non_alphanumeric(struct rcur *rcur);

/* Note: returns non-zero-terminated string, and sets len (or NULL) */
const char *rcur_pull_prefixed_str(struct rcur *rcur, size_t *len);

/* Returns up to next whitespace / punctuation.
 * Returns NULL for invalid / at end. */
const char *rcur_pull_word(struct rcur *rcur, size_t *len);

/* Returns to \0 terminator.  NULL if invalid / at end */
const char *rcur_pull_c_string(struct rcur *rcur);

/* Skip over this if it matches.  Return true if skipped */
bool rcur_skip_if_match(struct rcur *rcur, const void *p, size_t len);

/* Skpi over if this matches string (case insentive) */
bool rcur_skip_if_str_anycase(struct rcur *rcur, const char *str);

#endif /* JB55_RCUR_H */
