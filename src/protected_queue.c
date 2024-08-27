#include "protected_queue.h"

static void copy_bytes_in(struct prot_queue *q,
			  const void *src,
			  size_t bytes)
{
	/* If it wraps, we do it in two parts */
	if (unlikely(q->tail + bytes > q->buflen)) {
		size_t part1 = q->buflen - q->tail;
		memcpy(q->buf + q->tail, src, part1);
		q->tail = 0;
		q->bytes += part1;
		src = (const char *)src + part1;
		bytes -= part1;
	}
	memcpy(q->buf + q->tail, src, bytes);
	q->tail += bytes;
	q->bytes += bytes;
}

static void copy_bytes_out(struct prot_queue *q,
			   void *dst,
			   size_t bytes)
{
	/* If it wraps, we do it in two parts */
	if (unlikely(q->head + bytes > q->buflen)) {
		size_t part1 = q->buflen - q->head;
		memcpy(dst, q->buf + q->head, part1);
		q->head = 0;
		q->bytes -= part1;
		dst = (char *)dst + part1;
		bytes -= part1;
	}
	memcpy(dst, q->buf + q->head, bytes);
	q->head += bytes;
	q->bytes -= bytes;
}

static bool push(struct prot_queue* q, const void *data, size_t len,
		 bool block)
{
	pthread_mutex_lock(&q->mutex);

	// Wait until there's room.
	while (q->bytes + len > q->buflen) {
		if (!block) {
			pthread_mutex_unlock(&q->mutex);
			return false;
		}
		pthread_cond_wait(&q->cond_removed, &q->mutex);
	}

	copy_bytes_in(q, data, len);

	pthread_cond_signal(&q->cond_added);
	pthread_mutex_unlock(&q->mutex);
	return true;
}

static size_t pull(struct prot_queue* q,
		   void *data,
		   size_t min_len, size_t max_len,
		   bool block)
{
	size_t len;
	pthread_mutex_lock(&q->mutex);

	// Wait until there's enough contents.
	while (q->bytes < min_len) {
		if (!block)  {
			pthread_mutex_unlock(&q->mutex);
			return false;
		}
		pthread_cond_wait(&q->cond_added, &q->mutex);
	}

	len = q->bytes;
	if (len > max_len)
		len = max_len;
	copy_bytes_out(q, data, len);

	pthread_cond_signal(&q->cond_removed);
	pthread_mutex_unlock(&q->mutex);
	return len;
}

/* Push an element onto the queue.  Blocks if it needs to */
void prot_queue_push(struct prot_queue* q, const void *data)
{
	prot_queue_push_many(q, data, 1);
}

/* Push elements onto the queue.  Blocks if it needs to */
void prot_queue_push_many(struct prot_queue* q,
			  const void *data,
			  size_t count)
{
	push(q, data, count * q->elem_size, true);
}

/* Push an element onto the queue.  Returns false if it would block. */
bool prot_queue_try_push(struct prot_queue* q, const void *data)
{
	return push(q, data, q->elem_size, false);
}

size_t prot_queue_try_pop_many(struct prot_queue *q, void *data,
			       size_t max_items)
{
	return pull(q, data, q->elem_size, max_items * q->elem_size, false)
		/ q->elem_size;
}

size_t prot_queue_pop_many(struct prot_queue *q, void *data, size_t max_items)
{
	return pull(q, data, q->elem_size, max_items * q->elem_size, true)
		/ q->elem_size;
}

void prot_queue_pop(struct prot_queue *q, void *data)
{
	pull(q, data, q->elem_size, q->elem_size, true);
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

