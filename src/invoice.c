
#include "cursor.h"
#include "rcur.h"
#include "invoice.h"
#include "nostrdb.h"
#include "bolt11/bolt11.h"
#include "bolt11/amount.h"

int ndb_encode_invoice(struct cursor *cur, struct bolt11 *invoice) {
	if (!invoice->description && !invoice->description_hash)
		return 0;

	if (!cursor_push_byte(cur, 1))
		return 0;

	if (!cursor_push_varint(cur, invoice->msat == NULL ? 0 : invoice->msat->millisatoshis))
		return 0;

	if (!cursor_push_varint(cur, invoice->timestamp))
		return 0;

	if (!cursor_push_varint(cur, invoice->expiry))
		return 0;

	if (invoice->description) {
		if (!cursor_push_byte(cur, 1))
			return 0;
		if (!cursor_push_c_str(cur, invoice->description))
			return 0;
	} else {
		if (!cursor_push_byte(cur, 2))
			return 0;
		if (!cursor_push(cur, invoice->description_hash->u.u8, 32))
			return 0;
	}

	return 1;
}

bool ndb_decode_invoice(struct rcur *rcur, struct ndb_invoice *invoice)
{
	unsigned char desc_type;

	invoice->version = rcur_pull_byte(rcur);
	invoice->amount = rcur_pull_varint(rcur);
	invoice->timestamp = rcur_pull_varint(rcur);
	invoice->expiry = rcur_pull_varint(rcur);
	desc_type = rcur_pull_byte(rcur);

	if (desc_type == 1) {
		invoice->description = rcur_pull_c_string(rcur);
		if (!invoice->description)
			return false;
	} else if (desc_type == 2) {
		invoice->description_hash = rcur_pull(rcur, 32);
		if (!invoice->description_hash)
			return false;
	} else {
		rcur_fail(rcur);
		return false;
	}

	return true;
}
