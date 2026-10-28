#ifndef DARLING_MLDR_SOCKET_BITMAP_H
#define DARLING_MLDR_SOCKET_BITMAP_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct socket_bitmap {
	pthread_mutex_t mutex;
	/* Lowest free index; bit_length means all represented slots are used. */
	size_t next_index;
	uint8_t* bits;
	size_t bit_length;
	size_t byte_capacity;
	size_t used_count;
	int highest;
} socket_bitmap_t;

/* Caller holds mutex and defers asynchronous signals. A set bit reserves the
 * descriptor highest-index; highest stays fixed across resource-limit changes.
 * Keep represented storage while reservations survive, and release it when empty. */
int socket_bitmap_get_locked(socket_bitmap_t* bitmap);
void socket_bitmap_put_locked(socket_bitmap_t* bitmap, int descriptor);

#endif
