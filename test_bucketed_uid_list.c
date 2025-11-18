#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t ndb_uid_t;

struct uid_list {
	uint32_t count;
	uint16_t u8_offset;
	uint16_t u16_offset;
	uint32_t u32_offset;
	unsigned char data[];
};

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

static int uid_list_contains(struct uid_list *list, ndb_uid_t uid)
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

static struct uid_list *uid_list_create(uint32_t capacity)
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

	for (uint32_t i = 0; i < list->count - 1; i++) {
		for (uint32_t j = i + 1; j < list->count; j++) {
			if (temp_uids[i] > temp_uids[j]) {
				ndb_uid_t tmp = temp_uids[i];
				temp_uids[i] = temp_uids[j];
				temp_uids[j] = tmp;
			}
		}
	}

	uint32_t u8_count = 0, u16_count = 0, u32_count = 0;
	for (uint32_t i = 0; i < list->count; i++) {
		if (temp_uids[i] <= 0xFF)
			u8_count++;
		else if (temp_uids[i] <= 0xFFFF)
			u16_count++;
		else
			u32_count++;
	}

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

static int uid_list_add(struct uid_list **list_ptr, uint32_t *capacity, ndb_uid_t uid)
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

static size_t uid_list_size(struct uid_list *list)
{
	return sizeof(struct uid_list) + list->u32_offset;
}

// Get UID at index (for iteration)
static ndb_uid_t uid_list_get(struct uid_list *list, uint32_t index)
{
	if (index >= list->count)
		return 0;

	// Count UIDs in each bucket
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

// Remove UID at index (for iteration + removal)
static void uid_list_remove_at(struct uid_list *list, uint32_t index)
{
	if (index >= list->count)
		return;

	// Extract all UIDs except the one at index
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

void test_u8_bucket() {
	printf("Test: u8 bucket (1,2,42,128,255)...\n");

	struct uid_list *list = uid_list_create(64);
	uint32_t capacity = 64;

	assert(uid_list_add(&list, &capacity, 42));
	assert(uid_list_add(&list, &capacity, 1));
	assert(uid_list_add(&list, &capacity, 255));
	assert(uid_list_add(&list, &capacity, 2));
	assert(uid_list_add(&list, &capacity, 128));

	assert(list->count == 5);
	assert(list->u8_offset == 5);
	assert(list->u16_offset == 5);
	assert(list->u32_offset == 5);

	assert(uid_list_contains(list, 1));
	assert(uid_list_contains(list, 2));
	assert(uid_list_contains(list, 42));
	assert(uid_list_contains(list, 128));
	assert(uid_list_contains(list, 255));
	assert(!uid_list_contains(list, 3));
	assert(!uid_list_contains(list, 256));

	size_t size = uid_list_size(list);
	assert(size == sizeof(struct uid_list) + 5);

	free(list);
	printf("  PASS\n");
}

void test_u16_bucket() {
	printf("Test: u16 bucket (1024,4086,65535)...\n");

	struct uid_list *list = uid_list_create(64);
	uint32_t capacity = 64;

	assert(uid_list_add(&list, &capacity, 4086));
	assert(uid_list_add(&list, &capacity, 1024));
	assert(uid_list_add(&list, &capacity, 65535));

	assert(list->count == 3);
	assert(list->u8_offset == 0);     // No u8 values
	assert(list->u16_offset == 6);     // End of u16 bucket (3 u16s × 2 bytes)
	assert(list->u32_offset == 6);     // No u32 values

	assert(uid_list_contains(list, 1024));
	assert(uid_list_contains(list, 4086));
	assert(uid_list_contains(list, 65535));
	assert(!uid_list_contains(list, 1023));
	assert(!uid_list_contains(list, 65536));

	free(list);
	printf("  PASS\n");
}

void test_u32_bucket() {
	printf("Test: u32 bucket (70000,1000000,0xFFFFFFFF)...\n");

	struct uid_list *list = uid_list_create(64);
	uint32_t capacity = 64;

	assert(uid_list_add(&list, &capacity, 1000000));
	assert(uid_list_add(&list, &capacity, 70000));
	assert(uid_list_add(&list, &capacity, 0xFFFFFFFF));

	assert(list->count == 3);
	assert(list->u8_offset == 0);
	assert(list->u16_offset == 0);
	assert(list->u32_offset == 12);

	assert(uid_list_contains(list, 70000));
	assert(uid_list_contains(list, 1000000));
	assert(uid_list_contains(list, 0xFFFFFFFF));
	assert(!uid_list_contains(list, 69999));

	free(list);
	printf("  PASS\n");
}

void test_mixed_buckets() {
	printf("Test: mixed buckets (1,256,70000)...\n");

	struct uid_list *list = uid_list_create(64);
	uint32_t capacity = 64;

	assert(uid_list_add(&list, &capacity, 70000));
	assert(uid_list_add(&list, &capacity, 1));
	assert(uid_list_add(&list, &capacity, 256));
	assert(uid_list_add(&list, &capacity, 42));
	assert(uid_list_add(&list, &capacity, 1024));

	assert(list->count == 5);
	assert(list->u8_offset == 2);
	assert(list->u16_offset == 6);
	assert(list->u32_offset == 10);

	assert(uid_list_contains(list, 1));
	assert(uid_list_contains(list, 42));
	assert(uid_list_contains(list, 256));
	assert(uid_list_contains(list, 1024));
	assert(uid_list_contains(list, 70000));

	free(list);
	printf("  PASS\n");
}

void test_duplicates() {
	printf("Test: duplicate prevention...\n");

	struct uid_list *list = uid_list_create(64);
	uint32_t capacity = 64;

	assert(uid_list_add(&list, &capacity, 42));
	assert(uid_list_add(&list, &capacity, 42));
	assert(uid_list_add(&list, &capacity, 42));

	assert(list->count == 1);

	free(list);
	printf("  PASS\n");
}

void test_space_savings() {
	printf("Test: space savings vs u64 array...\n");

	struct uid_list *list = uid_list_create(1024);
	uint32_t capacity = 1024;

	for (uint32_t i = 1; i <= 1000; i++) {
		assert(uid_list_add(&list, &capacity, i));
	}

	size_t bucketed_size = uid_list_size(list);
	size_t u64_size = sizeof(struct {uint32_t count; uint64_t uids[1000];});

	printf("  Bucketed: %zu bytes\n", bucketed_size);
	printf("  u64 array: %zu bytes\n", u64_size);
	printf("  Savings: %.1f%%\n", 100.0 * (u64_size - bucketed_size) / u64_size);

	assert(bucketed_size < u64_size);

	for (uint32_t i = 1; i <= 1000; i++) {
		assert(uid_list_contains(list, i));
	}
	assert(!uid_list_contains(list, 0));
	assert(!uid_list_contains(list, 1001));

	free(list);
	printf("  PASS\n");
}

void test_removal() {
	printf("Test: removal from mixed buckets...\n");

	struct uid_list *list = uid_list_create(64);
	uint32_t capacity = 64;

	// Add mixed values: u8, u16, u32
	assert(uid_list_add(&list, &capacity, 1));      // u8
	assert(uid_list_add(&list, &capacity, 42));     // u8
	assert(uid_list_add(&list, &capacity, 255));    // u8
	assert(uid_list_add(&list, &capacity, 256));    // u16
	assert(uid_list_add(&list, &capacity, 1024));   // u16
	assert(uid_list_add(&list, &capacity, 70000));  // u32

	assert(list->count == 6);

	// Remove from u16 bucket (index 3, which is 256)
	assert(uid_list_get(list, 3) == 256);
	uid_list_remove_at(list, 3);
	assert(list->count == 5);
	assert(!uid_list_contains(list, 256));
	assert(uid_list_contains(list, 1));
	assert(uid_list_contains(list, 42));
	assert(uid_list_contains(list, 255));
	assert(uid_list_contains(list, 1024));
	assert(uid_list_contains(list, 70000));

	// Verify bucket structure after removal
	assert(list->u8_offset == 3);  // Still 3 u8 values
	assert(list->u16_offset == 5);  // 3 + 1*2 (only one u16 left)
	assert(list->u32_offset == 9);  // 3 + 2 + 1*4

	// Remove from u8 bucket (index 1, which is 42)
	assert(uid_list_get(list, 1) == 42);
	uid_list_remove_at(list, 1);
	assert(list->count == 4);
	assert(!uid_list_contains(list, 42));
	assert(uid_list_contains(list, 1));
	assert(uid_list_contains(list, 255));

	// Remove from u32 bucket (index 3, which is 70000)
	assert(uid_list_get(list, 3) == 70000);
	uid_list_remove_at(list, 3);
	assert(list->count == 3);
	assert(!uid_list_contains(list, 70000));

	// Verify final state: should have 1,255,1024
	assert(list->count == 3);
	assert(uid_list_get(list, 0) == 1);
	assert(uid_list_get(list, 1) == 255);
	assert(uid_list_get(list, 2) == 1024);

	free(list);
	printf("  PASS\n");
}

void test_removal_all() {
	printf("Test: remove all elements...\n");

	struct uid_list *list = uid_list_create(64);
	uint32_t capacity = 64;

	assert(uid_list_add(&list, &capacity, 10));
	assert(uid_list_add(&list, &capacity, 20));
	assert(uid_list_add(&list, &capacity, 30));

	uid_list_remove_at(list, 0);
	uid_list_remove_at(list, 0);
	uid_list_remove_at(list, 0);

	assert(list->count == 0);
	assert(list->u8_offset == 0);
	assert(list->u16_offset == 0);
	assert(list->u32_offset == 0);

	// Can still add after removing all
	assert(uid_list_add(&list, &capacity, 99));
	assert(list->count == 1);
	assert(uid_list_contains(list, 99));

	free(list);
	printf("  PASS\n");
}

void test_bucket_reorg() {
	printf("Test: bucket reorganization after removals...\n");

	struct uid_list *list = uid_list_create(64);
	uint32_t capacity = 64;

	// Add values that will be in different buckets
	for (uint32_t i = 1; i <= 10; i++) {
		assert(uid_list_add(&list, &capacity, i));  // u8
	}
	for (uint32_t i = 256; i <= 265; i++) {
		assert(uid_list_add(&list, &capacity, i));  // u16
	}

	assert(list->count == 20);
	assert(list->u8_offset == 10);

	// Remove all u8 values
	for (int i = 0; i < 10; i++) {
		uid_list_remove_at(list, 0);
	}

	assert(list->count == 10);
	assert(list->u8_offset == 0);  // No u8 values left

	// All remaining values should be u16
	for (uint32_t i = 0; i < 10; i++) {
		ndb_uid_t val = uid_list_get(list, i);
		assert(val >= 256 && val <= 265);
	}

	free(list);
	printf("  PASS\n");
}

int main() {
	printf("Running bucketed UID list tests...\n\n");

	test_u8_bucket();
	test_u16_bucket();
	test_u32_bucket();
	test_mixed_buckets();
	test_duplicates();
	test_space_savings();
	test_removal();
	test_removal_all();
	test_bucket_reorg();

	printf("\nAll tests passed!\n");
	return 0;
}
