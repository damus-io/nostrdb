#ifndef NDB_BUCKETED_UID_LIST_H
#define NDB_BUCKETED_UID_LIST_H

#include "ndb_uid.h"
#include <stdint.h>
#include <stddef.h>

// Bucketed variable-length UID list
// Stores UIDs sorted and bucketed by encoded size for space efficiency
// Format v2: [count][u8_offset][u16_offset][u32_offset][data...]
// Format v1 (legacy): [count][uids...] (detected when u8_offset > count)
struct uid_list {
	uint32_t count;      // Total number of UIDs
	uint16_t u8_offset;  // Offset in bytes to u16 bucket (or LEGACY_MARKER for v1)
	uint16_t u16_offset; // Offset in bytes to u32 bucket
	uint32_t u32_offset; // Offset in bytes to end (unused)
	unsigned char data[];
};

#define LEGACY_UID_LIST_MARKER 0xFFFF

// Create empty uid_list with given capacity
struct uid_list *uid_list_create(uint32_t capacity);

// Check if UID exists in list (O(log n) for u16/u32, O(n) for u8)
int uid_list_contains(struct uid_list *list, ndb_uid_t uid);

// Add UID to list (O(n) - rebuilds buckets)
int uid_list_add(struct uid_list **list_ptr, uint32_t *capacity, ndb_uid_t uid);

// Get UID at index (for iteration)
ndb_uid_t uid_list_get(struct uid_list *list, uint32_t index);

// Remove UID at index (O(n) - rebuilds buckets)
void uid_list_remove_at(struct uid_list *list, uint32_t index);

// Get serialized size of list
size_t uid_list_size(struct uid_list *list);

// Legacy format support
int uid_list_is_legacy(const struct uid_list *list);
struct uid_list *uid_list_from_legacy(const struct uid_list *legacy);

#endif // NDB_BUCKETED_UID_LIST_H
