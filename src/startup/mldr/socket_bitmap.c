#include "socket_bitmap.h"
#include <stdlib.h>
#include <sys/resource.h>

/* Extracted from mldr's descriptor registry so the host regression exercises
 * production code. Upstream and the reviewed Vibe copy have the same inverted
 * shrink predicate and realloc(ptr, 0) stale-pointer defect. */
int socket_bitmap_get_locked(socket_bitmap_t* bitmap)
{
	if (bitmap->highest == -1) {
		struct rlimit limit;
		if (getrlimit(RLIMIT_NOFILE, &limit) < 0)
			return -1;
		if (limit.rlim_cur == RLIM_INFINITY)
			limit.rlim_cur = 1024;
		bitmap->highest = limit.rlim_cur - 1;
	}
	if (bitmap->highest < 3 || bitmap->next_index > (size_t)(bitmap->highest - 3))
		return -1;
	if (bitmap->next_index >= bitmap->bit_length) {
		size_t required = bitmap->bit_length / 8 + 1;
		if (required > bitmap->byte_capacity) {
			uint8_t* bits = realloc(bitmap->bits, required);
			if (bits == NULL)
				return -1;
			bits[required - 1] = 0;
			bitmap->bits = bits;
			bitmap->byte_capacity = required;
		}
		++bitmap->bit_length;
	}
	int descriptor = bitmap->highest - bitmap->next_index;
	bitmap->bits[bitmap->next_index / 8] |= 1u << (bitmap->next_index % 8);
	++bitmap->next_index;
	while (bitmap->next_index < bitmap->bit_length &&
		(bitmap->bits[bitmap->next_index / 8] & (1u << (bitmap->next_index % 8))) != 0)
		++bitmap->next_index;
	return descriptor;
}

void socket_bitmap_put_locked(socket_bitmap_t* bitmap, int descriptor)
{
	size_t index = bitmap->highest - descriptor;
	bitmap->bits[index / 8] &= ~(1u << (index % 8));
	if (index < bitmap->next_index)
		bitmap->next_index = index;
	while (bitmap->bit_length > 0) {
		size_t last = bitmap->bit_length - 1;
		if ((bitmap->bits[last / 8] & (1u << (last % 8))) != 0)
			break;
		--bitmap->bit_length;
	}
	if (bitmap->next_index > bitmap->bit_length)
		bitmap->next_index = bitmap->bit_length;
	if (bitmap->bit_length == 0) {
		/* realloc(ptr, 0) may free ptr and return NULL. Never leave that
		 * freed address cached as the next allocation's input. */
		free(bitmap->bits);
		bitmap->bits = NULL;
		bitmap->byte_capacity = 0;
	}
	/* Keep nonempty capacity: freeing a slot need not allocate/copy, and a
	 * later reservation can reuse storage without shrinking then regrowing. */
}
