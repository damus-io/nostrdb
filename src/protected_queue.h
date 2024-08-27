/*
v *    This header file provides a thread-safe queue implementation for generic
 *    data elements. It uses POSIX threads (pthreads) to ensure thread safety.
 *    The queue allows for pushing and popping elements, with the ability to
 *    block or non-block on pop operations. Users are responsible for providing
 *    memory for the queue buffer and ensuring its correct lifespan.
 *
 *         Author:  William Casarin
 *         Inspired-by: https://github.com/hoytech/hoytech-cpp/blob/master/hoytech/protected_queue.h
 */

#ifndef PROT_QUEUE_H
#define PROT_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "cursor.h"
#include "util.h"

/* 
 * The prot_queue structure represents a thread-safe queue that can hold
 * generic data elements.
 */
struct prot_queue {
	/* These don't change */
	unsigned char *buf;
	size_t buflen;
	int elem_size;

	/* These do: you must hold mutex!  Offsets in *bytes* */
	size_t head;
	size_t tail;
	/* This seems redundant, but it isn't: head == tail means either
	 * full, or empty! */
	size_t bytes;

	pthread_mutex_t mutex;
	/* Added */
	pthread_cond_t cond_added;
	/* Removed */
	pthread_cond_t cond_removed;
};


/* 
 * Initialize the queue. 
 * Params:
 * q         - Pointer to the queue.
 * buf       - Buffer for holding data elements.
 * buflen    - Length of the buffer.
 * elem_size - Size of each data element.
 * Returns 1 if successful, 0 otherwise.
 */
static inline int prot_queue_init(struct prot_queue* q, void* buf,
				  size_t buflen, int elem_size)
{
	if (buflen < elem_size)
		assert(!"What is this, a queue for ants?");

	q->head = 0;
	q->tail = 0;
	q->bytes = 0;
	q->buf = buf;
	q->buflen = buflen;
	q->elem_size = elem_size;

	pthread_mutex_init(&q->mutex, NULL);
	pthread_cond_init(&q->cond_added, NULL);
	pthread_cond_init(&q->cond_removed, NULL);

	return 1;
}

/* Push an element onto the queue.  Blocks if it needs to */
void prot_queue_push(struct prot_queue* q, const void *data);

/* Push elements onto the queue.  Blocks if it needs to */
void prot_queue_push_many(struct prot_queue* q, const void *data, size_t count);

/* Push an element onto the queue.  Returns false if it would block. */
bool prot_queue_try_push(struct prot_queue* q, const void *data);

/* 
 * Try to pop an element from the queue without blocking.
 * Params:
 * q    - Pointer to the queue.
 * data - Pointer to where the popped data will be stored.
 * Returns number popped (<= max_items)
 */
size_t prot_queue_try_pop_many(struct prot_queue *q, void *data, size_t max_items);

/* 
 * Wait until we have elements, and then pop multiple elements from the queue
 * up to the specified maximum.
 *
 * Params:
 * q		 - Pointer to the queue.
 * buffer	 - Pointer to the buffer where popped data will be stored.
 * max_items - Maximum number of items to pop from the queue.
 * Returns the actual number of items popped (> 0).
 */
size_t prot_queue_pop_many(struct prot_queue *q, void *data, size_t max_items);

/* 
 * Pop an element from the queue. Blocks if the queue is empty.
 * Params:
 * q    - Pointer to the queue.
 * data - Pointer to where the popped data will be stored.
 */
void prot_queue_pop(struct prot_queue *q, void *data);

/* 
 * Destroy the queue. Releases resources associated with the queue.
 * Params:
 * q - Pointer to the queue.
 */
void prot_queue_destroy(struct prot_queue* q);

#endif // PROT_QUEUE_H
