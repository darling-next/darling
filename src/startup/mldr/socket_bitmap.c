#include "socket_bitmap.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <sys/resource.h>

/* Extracted from mldr's descriptor registry so the host regression exercises
 * production code. Upstream and the reviewed Vibe copy have the same inverted
 * shrink predicate and realloc(ptr, 0) stale-pointer defect. */
int socket_bitmap_get_locked(socket_bitmap_t* bitmap)
{
	struct rlimit limit;
	if (getrlimit(RLIMIT_NOFILE, &limit) < 0)
		return -1;
	rlim_t upper = limit.rlim_cur == RLIM_INFINITY ? 1024 : limit.rlim_cur;
	if (upper <= 3) {
		errno = EMFILE;
		return -1;
	}
	--upper;
	if (upper > INT_MAX)
		upper = INT_MAX;
	if (bitmap->highest == -1)
		bitmap->highest = (int)upper;
	if (bitmap->highest < 3) {
		errno = EMFILE;
		return -1;
	}

	/* Existing high reservations remain valid after a limit decrease.
	 * Search below the live limit without rebasing their bitmap indices. */
	size_t index = bitmap->highest > upper ? bitmap->highest - upper : 0;
	if (index < bitmap->next_index)
		index = bitmap->next_index;
	while (index < bitmap->bit_length &&
		(bitmap->bits[index / 8] & (1u << (index % 8))) != 0)
		++index;
	if (index > (size_t)(bitmap->highest - 3)) {
		errno = EMFILE;
		return -1;
	}
	if (index >= bitmap->bit_length) {
		size_t required = index / 8 + 1;
		if (required > bitmap->byte_capacity) {
			uint8_t* bits = realloc(bitmap->bits, required);
			if (bits == NULL)
				return -1;
			memset(bits + bitmap->byte_capacity, 0, required - bitmap->byte_capacity);
			bitmap->bits = bits;
			bitmap->byte_capacity = required;
		}
		bitmap->bit_length = index + 1;
	}
	bitmap->bits[index / 8] |= 1u << (index % 8);
	++bitmap->used_count;
	if (index == bitmap->next_index) {
		++bitmap->next_index;
		while (bitmap->next_index < bitmap->bit_length &&
			(bitmap->bits[bitmap->next_index / 8] & (1u << (bitmap->next_index % 8))) != 0)
			++bitmap->next_index;
	}
	return bitmap->highest - index;
}

void socket_bitmap_put_locked(socket_bitmap_t* bitmap, int descriptor)
{
	size_t index = bitmap->highest - descriptor;
	bitmap->bits[index / 8] &= ~(1u << (index % 8));
	if (index < bitmap->next_index)
		bitmap->next_index = index;
	/* Sparse low-limit reservations can be far from the initial ceiling.
	 * Do not rescan that entire gap on each transient descriptor release. */
	if (--bitmap->used_count == 0) {
		free(bitmap->bits);
		bitmap->bits = NULL;
		bitmap->byte_capacity = 0;
		bitmap->bit_length = 0;
		bitmap->next_index = 0;
	}
}
