/* Encapsulation for pubkeys used as node ids: more compact, more dangerous. */
#ifndef LIGHTNING_COMMON_NODE_ID_H
#define LIGHTNING_COMMON_NODE_ID_H
#include "../config.h"
#include "ccan/short_types/short_types.h"
#include "ccan/tal/tal.h"

struct node_id {
    u8 k[33];
};

static inline bool node_id_eq(const struct node_id *a,
                  const struct node_id *b)
{
    return memcmp(a->k, b->k, sizeof(a->k)) == 0;
}

/* Is this actually a valid pubkey?  Relatively expensive. */
//bool node_id_valid(const struct node_id *id);

/* Convert to hex string of SEC1 encoding. */
char *node_id_to_hexstr(const tal_t *ctx, const struct node_id *id);

/* Convert from hex string of SEC1 encoding: checks validity! */
bool node_id_from_hexstr(const char *str, size_t slen, struct node_id *id);

/* Compare the keys `a` and `b`. Return <0 if `a`<`b`, 0 if equal and >0 otherwise */
int node_id_cmp(const struct node_id *a, const struct node_id *b);

/* If the two nodes[] are id1 and id2, which index would id1 be? */
static inline int node_id_idx(const struct node_id *id1,
                  const struct node_id *id2)
{
    return node_id_cmp(id1, id2) > 0;
}

/* marshal/unmarshal functions */
#endif /* LIGHTNING_COMMON_NODE_ID_H */
