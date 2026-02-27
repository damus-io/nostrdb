/* Minimal stubs for libsodium functions needed by chacha20.
   Only crypto_stream_chacha20_ietf_xor_ic is used by nostrdb (nip44.c).
   The keygen functions that call randombytes_buf are never invoked. */

#include <string.h>
#include <stdlib.h>

void sodium_memzero(void * const pnt, const size_t len)
{
    volatile unsigned char *p = (volatile unsigned char *)pnt;
    size_t i;
    for (i = 0; i < len; i++) {
        p[i] = 0;
    }
}

void sodium_misuse(void)
{
    abort();
}

void randombytes_buf(void * const buf, const size_t size)
{
    (void)buf;
    (void)size;
    abort();
}
