#include "rcur.h"
#include "ccan/utf8/utf8.h"
#include "cursor.h"
#include <errno.h>
#include <string.h>

/* Note that only rcur_pull and rcur_peek_remainder actually access
 * rcur.  The rest are built on top of them! */

/* Pull @len bytes from rcur, if space available.
 * Return NULL if not valid.
 */
const void *rcur_pull(struct rcur *rcur, size_t len)
{
    const void *p = rcur_peek(rcur, len);
    if (!p) {
	rcur_fail(rcur);
	return NULL;
    }
    rcur->p += len;
    return rcur->p - len;
}

/* Access the remaining bytes.  Returns NULL and *len=0 if invalid. */
const void *rcur_peek_remainder(const struct rcur *rcur, size_t *len)
{
    if (!rcur_valid(rcur)) {
	*len = 0;
	return NULL;
    }
    *len = rcur->end - rcur->p;
    return rcur->p;
}

bool rcur_copy(struct rcur *rcur, void *dst, size_t len)
{
    const void *src = rcur_pull(rcur, len);
    if (!src)
	return false;
    memcpy(dst, src, len);
    return true;
}

/* Look ahead: returns NULL if @len too long.  Does *not* alter
 * rcur */
const void *rcur_peek(const struct rcur *rcur, size_t len)
{
    size_t actual_len;
    const void *p = rcur_peek_remainder(rcur, &actual_len);

    if (len > actual_len)
	return NULL;
    return p;
}

/* All these are based on rcur_pull. */
bool rcur_skip(struct rcur *rcur, size_t len)
{
    return rcur_pull(rcur, len) != NULL;
}

unsigned char rcur_pull_byte(struct rcur *rcur)
{
    unsigned char v;

    if (!rcur_copy(rcur, &v, sizeof(v)))
	return 0;
    return v;
}

uint16_t rcur_pull_u16(struct rcur *rcur)
{
    uint16_t v;

    if (!rcur_copy(rcur, &v, sizeof(v)))
	return 0;
    return v;
}

uint32_t rcur_pull_u32(struct rcur *rcur)
{
    uint32_t v;

    if (!rcur_copy(rcur, &v, sizeof(v)))
	return 0;
    return v;
}

uint64_t rcur_pull_varint(struct rcur *rcur)
{
    uint64_t v = 0;
    unsigned char c;

    for (size_t i = 0; i < 10; i++) { // Loop up to 10 bytes for 64-bit
	c = rcur_pull_byte(rcur);
	v |= ((uint64_t)c & 0x7F) << (i * 7);

	if ((c & 0x80) == 0)
	    break;
    }
    return v;
}
	
uint32_t rcur_pull_varint_u32(struct rcur *rcur)
{
    uint64_t v = rcur_pull_varint(rcur);
    if (v >= UINT32_MAX) {
	rcur_fail(rcur);
	v = 0;
    }
    return v;
}

size_t rcur_pull_whitespace(struct rcur *rcur)
{
    size_t len, wslen;
    const unsigned char *c;

    c = rcur_peek_remainder(rcur, &len);
    for (wslen = 0; wslen < len; wslen++) {
	if (!is_whitespace(c[wslen]))
	    break;
    }

    rcur_skip(rcur, wslen);
    return wslen;
}

// Returns 0 on error.  Adds length to *totlen.
static uint32_t rcur_pull_utf8(struct rcur *rcur, size_t *totlen)
{
    struct utf8_state state = UTF8_STATE_INIT;

    /* Since 0 is treated as a bad encoding, this terminated if we run
     * out of chars */
    while (!utf8_decode(&state, rcur_pull_byte(rcur)))
	(*totlen)++;

    if (errno != 0) {
	rcur_fail(rcur);
	return 0;
    }
    return state.c;
}

/* Remove is_punctuation(), return bytes removed */
size_t rcur_pull_punctuation(struct rcur *rcur)
{
    size_t totlen = 0;

    while (rcur_bytes_remaining(*rcur)) {
	uint32_t c = rcur_pull_utf8(rcur, &totlen);
	if (!is_punctuation(c))
	    break;
    }

    if (!rcur_valid(rcur))
	return 0;
    return totlen;
}

/* Remove !is_alphanumeric(), return bytes removed */
size_t rcur_pull_non_alphanumeric(struct rcur *rcur)
{
    size_t len, nonalpha_len;
    const char *p = rcur_peek_remainder(rcur, &len);

    for (nonalpha_len = 0; nonalpha_len < len; nonalpha_len++) {
	if (!is_alphanumeric(p[nonalpha_len]))
	    break;
    }

    rcur_skip(rcur, nonalpha_len);
    return nonalpha_len;
}

const char *rcur_pull_word(struct rcur *rcur, size_t *len)
{
    const char *start = rcur_peek(rcur, 0);

    /* consume_until_boundary */
    *len = 0;
    while (rcur_bytes_remaining(*rcur)) {
	uint32_t c = rcur_pull_utf8(rcur, len);
	if (!c || is_right_boundary(c))
	    break;
    }

    if (!rcur_valid(rcur) || *len == 0)
	return NULL;

    return start;
}

const char *rcur_pull_c_string(struct rcur *rcur)
{
    size_t len;
    const char *p = rcur_peek_remainder(rcur, &len);

    for (size_t i = 0; i < len; i++) {
	if (p[i] == '\0')
	    return rcur_pull(rcur, i+1);
    }
    rcur_fail(rcur);
    return NULL;
}

bool rcur_skip_if_match(struct rcur *rcur, const void *p, size_t len)
{
    const void *peek = rcur_peek(rcur, len);

    if (!peek)
	return false;

    if (memcmp(p, peek, len) != 0)
	return false;
    rcur_skip(rcur, len);
    return true;
}

bool rcur_skip_if_str_anycase(struct rcur *rcur, const char *str)
{
    size_t len = strlen(str);
    const char *peek = rcur_peek(rcur, len);

    if (!peek)
	return false;

    for (size_t i = 0; i < len; i++) {
	if (tolower(peek[i]) != tolower(str[i]))
	    return false;
    }
    rcur_skip(rcur, len);
    return true;
}

const char *rcur_pull_prefixed_str(struct rcur *rcur, size_t *len)
{
    *len = rcur_pull_varint(rcur);
    return rcur_pull(rcur, *len);
}

bool rcur_trim_if_char(struct rcur *rcur, char c)
{
        const char *p;
        size_t len;

        p = rcur_peek_remainder(rcur, &len);
        if (len > 0 && p[len-1] == c) {
                rcur->p--;
                return true;
        }
        return false;
}
