
#ifndef NDB_INVOICE_H
#define NDB_INVOICE_H

#include <inttypes.h>
#include "cursor.h"
#include "nostrdb.h"

struct bolt11;
struct rcur;

// ENCODING
int ndb_encode_invoice(struct cursor *cur, struct bolt11 *invoice);
bool ndb_decode_invoice(struct rcur *rcur, struct ndb_invoice *invoice);

#endif /* NDB_INVOICE_H */
