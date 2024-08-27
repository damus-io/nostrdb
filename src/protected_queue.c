#include "protected_queue.h"

/* 
 * Push an element onto the queue.
 * Params:
 * q    - Pointer to the queue.
 * data - Pointer to the data element to be pushed.
 *
 * Blocks if no space is available.
 */
int prot_queue_push(struct prot_queue* q, void *data)
{
	int cap;

	pthread_mutex_lock(&q->mutex);

	// Wait until there's room.
	while ((cap = prot_queue_capacity(q)) == q->count)
		pthread_cond_wait(&q->cond_removed, &q->mutex);

	memcpy(&q->buf[q->tail * q->elem_size], data, q->elem_size);
	q->tail = (q->tail + 1) % cap;
	q->count++;

	pthread_cond_signal(&q->cond_added);
	pthread_mutex_unlock(&q->mutex);
	return 1;
}

/*
 * Push multiple elements onto the queue.
 * Params:
 * q      - Pointer to the queue.
 * data   - Pointer to the data elements to be pushed.
 * count  - Number of elements to push.
 *
 * Returns the number of elements successfully pushed, 0 if the queue is full or if there is not enough contiguous space.
 */
int prot_queue_push_all(struct prot_queue* q, void *data, int count)
{
	int cap;
	int first_copy_count, second_copy_count;

	cap = prot_queue_capacity(q);
	assert(count <= cap);
	pthread_mutex_lock(&q->mutex);

	while (q->count + count > cap)
		pthread_cond_wait(&q->cond_removed, &q->mutex);

	first_copy_count = min(count, cap - q->tail); // Elements until the end of the buffer
	second_copy_count = count - first_copy_count; // Remaining elements if wrap around

	memcpy(&q->buf[q->tail * q->elem_size], data, first_copy_count * q->elem_size);
	q->tail = (q->tail + first_copy_count) % cap;

	if (second_copy_count > 0) {
		// If there is a wrap around, copy the remaining elements
		memcpy(&q->buf[q->tail * q->elem_size], (char *)data + first_copy_count * q->elem_size, second_copy_count * q->elem_size);
		q->tail = (q->tail + second_copy_count) % cap;
	}

	q->count += count;

	pthread_cond_signal(&q->cond_added); // Signal a waiting thread
	pthread_mutex_unlock(&q->mutex);

	return count;
}

/* 
 * Try to pop an element from the queue without blocking.
 * Params:
 * q    - Pointer to the queue.
 * data - Pointer to where the popped data will be stored.
 * Returns 1 if successful, 0 if the queue is empty.
 */
int prot_queue_try_pop_all(struct prot_queue *q, void *data, int max_items) {
	int items_to_pop, items_until_end;

	pthread_mutex_lock(&q->mutex);

	if (q->count == 0) {
		pthread_mutex_unlock(&q->mutex);
		return 0;
	}

	items_until_end = (q->buflen - q->head * q->elem_size) / q->elem_size;
	items_to_pop = min(q->count, max_items);
	items_to_pop = min(items_to_pop, items_until_end);

	memcpy(data, &q->buf[q->head * q->elem_size], items_to_pop * q->elem_size);
	q->head = (q->head + items_to_pop) % prot_queue_capacity(q);
	q->count -= items_to_pop;

	pthread_cond_signal(&q->cond_removed); // Signal a waiting thread
	pthread_mutex_unlock(&q->mutex);
	return items_to_pop;
}

/* 
 * Wait until we have elements, and then pop multiple elements from the queue
 * up to the specified maximum.
 *
 * Params:
 * q		 - Pointer to the queue.
 * buffer	 - Pointer to the buffer where popped data will be stored.
 * max_items - Maximum number of items to pop from the queue.
 * Returns the actual number of items popped.
 */
int prot_queue_pop_all(struct prot_queue *q, void *dest, int max_items) {
	pthread_mutex_lock(&q->mutex);

	// Wait until there's at least one item to pop
	while (q->count == 0) {
		pthread_cond_wait(&q->cond_added, &q->mutex);
	}

	int items_until_end = (q->buflen - q->head * q->elem_size) / q->elem_size;
	int items_to_pop = min(q->count, max_items);
	items_to_pop = min(items_to_pop, items_until_end);

	memcpy(dest, &q->buf[q->head * q->elem_size], items_to_pop * q->elem_size);
	q->head = (q->head + items_to_pop) % prot_queue_capacity(q);
	q->count -= items_to_pop;

	pthread_cond_signal(&q->cond_removed);
	pthread_mutex_unlock(&q->mutex);

	return items_to_pop;
}

/* 
 * Pop an element from the queue. Blocks if the queue is empty.
 * Params:
 * q    - Pointer to the queue.
 * data - Pointer to where the popped data will be stored.
 */
void prot_queue_pop(struct prot_queue *q, void *data) {
	pthread_mutex_lock(&q->mutex);

	while (q->count == 0)
		pthread_cond_wait(&q->cond_added, &q->mutex);

	memcpy(data, &q->buf[q->head * q->elem_size], q->elem_size);
	q->head = (q->head + 1) % prot_queue_capacity(q);
	q->count--;

	pthread_cond_signal(&q->cond_removed);
	pthread_mutex_unlock(&q->mutex);
}

/* 
 * Destroy the queue. Releases resources associated with the queue.
 * Params:
 * q - Pointer to the queue.
 */
void prot_queue_destroy(struct prot_queue* q) {
	pthread_mutex_destroy(&q->mutex);
	pthread_cond_destroy(&q->cond_added);
	pthread_cond_destroy(&q->cond_removed);
}

