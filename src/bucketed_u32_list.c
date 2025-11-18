#include "bucketed_u32_list.h"
#include <stdlib.h>
#include <string.h>

// Binary search helpers
static int bsearch_u16(const uint16_t *arr, uint32_t count, uint16_t val)
{
	uint32_t left = 0, right = count;
	while (left < right) {
		uint32_t mid = (left + right) / 2;
		if (arr[mid] == val)
			return 1;
		if (arr[mid] < val)
			left = mid + 1;
		else
			right = mid;
	}
	return 0;
}

static int bsearch_u32(const uint32_t *arr, uint32_t count, uint32_t val)
{
	uint32_t left = 0, right = count;
	while (left < right) {
		uint32_t mid = (left + right) / 2;
		if (arr[mid] == val)
			return 1;
		if (arr[mid] < val)
			left = mid + 1;
		else
			right = mid;
	}
	return 0;
}

int uid_list_contains(struct uid_list *list, ndb_uid_t uid)
{
	if (list->count == 0)
		return 0;

	if (uid <= 0xFF) {
		const uint8_t *u8_data = (const uint8_t*)list->data;
		for (uint32_t i = 0; i < list->u8_offset; i++) {
			if (u8_data[i] == (uint8_t)uid)
				return 1;
		}
		return 0;
	} else if (uid <= 0xFFFF) {
		const uint16_t *u16_data = (const uint16_t*)(list->data + list->u8_offset);
		uint32_t u16_count = (list->u16_offset - list->u8_offset) / sizeof(uint16_t);
		return bsearch_u16(u16_data, u16_count, (uint16_t)uid);
	} else {
		const uint32_t *u32_data = (const uint32_t*)(list->data + list->u16_offset);
		uint32_t u32_count = (list->u32_offset - list->u16_offset) / sizeof(uint32_t);
		return bsearch_u32(u32_data, u32_count, uid);
	}
}

struct uid_list *uid_list_create(uint32_t capacity)
{
	size_t size = sizeof(struct uid_list) + capacity * sizeof(uint32_t);
	struct uid_list *list = malloc(size);
	if (list) {
		list->count = 0;
		list->u8_offset = 0;
		list->u16_offset = 0;
		list->u32_offset = 0;
	}
	return list;
}

static void uid_list_rebuild_buckets(struct uid_list *list, ndb_uid_t *temp_uids)
{
	if (list->count == 0) {
		list->u8_offset = 0;
		list->u16_offset = 0;
		list->u32_offset = 0;
		return;
	}

	// Sort UIDs
	for (uint32_t i = 0; i < list->count - 1; i++) {
		for (uint32_t j = i + 1; j < list->count; j++) {
			if (temp_uids[i] > temp_uids[j]) {
				ndb_uid_t tmp = temp_uids[i];
				temp_uids[i] = temp_uids[j];
				temp_uids[j] = tmp;
			}
		}
	}

	// Count buckets
	uint32_t u8_count = 0, u16_count = 0, u32_count = 0;
	for (uint32_t i = 0; i < list->count; i++) {
		if (temp_uids[i] <= 0xFF)
			u8_count++;
		else if (temp_uids[i] <= 0xFFFF)
			u16_count++;
		else
			u32_count++;
	}

	// Pack into buckets
	uint8_t *u8_ptr = (uint8_t*)list->data;
	uint16_t *u16_ptr = (uint16_t*)(list->data + u8_count);
	uint32_t *u32_ptr = (uint32_t*)(list->data + u8_count + u16_count * 2);

	uint32_t u8_idx = 0, u16_idx = 0, u32_idx = 0;
	for (uint32_t i = 0; i < list->count; i++) {
		if (temp_uids[i] <= 0xFF)
			u8_ptr[u8_idx++] = (uint8_t)temp_uids[i];
		else if (temp_uids[i] <= 0xFFFF)
			u16_ptr[u16_idx++] = (uint16_t)temp_uids[i];
		else
			u32_ptr[u32_idx++] = temp_uids[i];
	}

	list->u8_offset = u8_count;
	list->u16_offset = u8_count + u16_count * 2;
	list->u32_offset = u8_count + u16_count * 2 + u32_count * 4;
}

int uid_list_add(struct uid_list **list_ptr, uint32_t *capacity, ndb_uid_t uid)
{
	struct uid_list *list = *list_ptr;

	if (uid_list_contains(list, uid))
		return 1;

	if (list->count >= *capacity) {
		uint32_t new_capacity = *capacity * 2;
		size_t new_size = sizeof(struct uid_list) + new_capacity * sizeof(uint32_t);
		struct uid_list *new_list = realloc(list, new_size);
		if (!new_list)
			return 0;
		*list_ptr = new_list;
		*capacity = new_capacity;
		list = new_list;
	}

	ndb_uid_t *temp_uids = malloc((list->count + 1) * sizeof(ndb_uid_t));
	if (!temp_uids)
		return 0;

	uint32_t idx = 0;
	const uint8_t *u8_data = (const uint8_t*)list->data;
	for (uint32_t i = 0; i < list->u8_offset; i++)
		temp_uids[idx++] = u8_data[i];

	const uint16_t *u16_data = (const uint16_t*)(list->data + list->u8_offset);
	uint32_t u16_count = (list->u16_offset - list->u8_offset) / sizeof(uint16_t);
	for (uint32_t i = 0; i < u16_count; i++)
		temp_uids[idx++] = u16_data[i];

	const uint32_t *u32_data = (const uint32_t*)(list->data + list->u16_offset);
	uint32_t u32_count = (list->u32_offset - list->u16_offset) / sizeof(uint32_t);
	for (uint32_t i = 0; i < u32_count; i++)
		temp_uids[idx++] = u32_data[i];

	temp_uids[list->count] = uid;
	list->count++;

	uid_list_rebuild_buckets(list, temp_uids);

	free(temp_uids);
	return 1;
}

size_t uid_list_size(struct uid_list *list)
{
	return sizeof(struct uid_list) + list->u32_offset;
}

ndb_uid_t uid_list_get(struct uid_list *list, uint32_t index)
{
	if (index >= list->count)
		return 0;

	uint32_t u8_count = list->u8_offset;
	uint32_t u16_count = (list->u16_offset - list->u8_offset) / sizeof(uint16_t);

	if (index < u8_count) {
		const uint8_t *u8_data = (const uint8_t*)list->data;
		return u8_data[index];
	} else if (index < u8_count + u16_count) {
		const uint16_t *u16_data = (const uint16_t*)(list->data + list->u8_offset);
		return u16_data[index - u8_count];
	} else {
		const uint32_t *u32_data = (const uint32_t*)(list->data + list->u16_offset);
		return u32_data[index - u8_count - u16_count];
	}
}

void uid_list_remove_at(struct uid_list *list, uint32_t index)
{
	if (index >= list->count)
		return;

	ndb_uid_t *temp_uids = malloc(list->count * sizeof(ndb_uid_t));
	if (!temp_uids)
		return;

	uint32_t idx = 0;
	for (uint32_t i = 0; i < list->count; i++) {
		if (i != index)
			temp_uids[idx++] = uid_list_get(list, i);
	}

	list->count--;
	uid_list_rebuild_buckets(list, temp_uids);
	free(temp_uids);
}

int uid_list_is_legacy(const struct uid_list *list)
{
	return list->u8_offset == LEGACY_UID_LIST_MARKER;
}

struct uid_list *uid_list_from_legacy(const struct uid_list *legacy)
{
	const ndb_uid_t *legacy_uids = (const ndb_uid_t*)(legacy->data);

	size_t size = sizeof(struct uid_list) + legacy->count * sizeof(uint32_t);
	struct uid_list *list = malloc(size);
	if (!list)
		return NULL;

	list->count = 0;
	list->u8_offset = 0;
	list->u16_offset = 0;
	list->u32_offset = 0;

	ndb_uid_t *temp_uids = malloc(legacy->count * sizeof(ndb_uid_t));
	if (!temp_uids) {
		free(list);
		return NULL;
	}

	memcpy(temp_uids, legacy_uids, legacy->count * sizeof(ndb_uid_t));
	list->count = legacy->count;
	uid_list_rebuild_buckets(list, temp_uids);

	free(temp_uids);
	return list;
}
