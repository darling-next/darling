#include "socket_bitmap.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static void require(int condition, const char* detail)
{
	if (!condition) {
		fprintf(stderr, "FAIL descriptor bitmap: %s (errno=%d)\n", detail, errno);
		exit(1);
	}
}

static void test_surviving_slots(void)
{
	socket_bitmap_t bitmap = { .mutex = PTHREAD_MUTEX_INITIALIZER, .highest = 127 };
	int descriptors[24];
	for (size_t i = 0; i < 24; ++i) {
		descriptors[i] = socket_bitmap_get_locked(&bitmap);
		require(descriptors[i] == 127 - (int)i, "allocate descending descriptors");
	}
	/* Remove two complete trailing bytes without erasing the still-live
	 * first byte. The old inverted predicate trimmed those live slots too. */
	for (size_t i = 8; i < 24; ++i)
		socket_bitmap_put_locked(&bitmap, descriptors[i]);
	require(bitmap.bit_length == 8 && bitmap.next_index == 8,
		"trim stops at surviving allocated byte");
	for (size_t i = 8; i < 24; ++i) {
		int descriptor = socket_bitmap_get_locked(&bitmap);
		require(descriptor == descriptors[i], "reallocate without aliasing survivors");
	}
	socket_bitmap_put_locked(&bitmap, descriptors[23]);
	require(bitmap.bit_length == 23, "trim single free trailing bit");
	require(socket_bitmap_get_locked(&bitmap) == descriptors[23], "reuse partial-byte tail");
	socket_bitmap_put_locked(&bitmap, descriptors[3]);
	require(bitmap.bit_length == 24 && bitmap.next_index == 3, "preserve higher live slots");
	require(socket_bitmap_get_locked(&bitmap) == descriptors[3], "reuse interior hole");
	for (size_t i = 0; i < 24; ++i)
		socket_bitmap_put_locked(&bitmap, descriptors[i]);
	require(bitmap.bits == NULL && bitmap.byte_capacity == 0 &&
		bitmap.bit_length == 0 && bitmap.next_index == 0, "empty bitmap owns no freed pointer");
	for (unsigned round = 0; round < 128; ++round) {
		for (size_t i = 0; i < 24; ++i)
			require(socket_bitmap_get_locked(&bitmap) == descriptors[i], "allocate after shrink to zero");
		for (size_t i = 24; i > 0; --i)
			socket_bitmap_put_locked(&bitmap, descriptors[i - 1]);
		require(bitmap.bits == NULL && bitmap.bit_length == 0 && bitmap.next_index == 0,
			"repeated empty bitmap remains reusable");
	}
}

static void test_fork_descriptor_cleanup(void)
{
	socket_bitmap_t bitmap = { .mutex = PTHREAD_MUTEX_INITIALIZER, .highest = -1 };
	int source = open("/dev/null", O_RDONLY | O_CLOEXEC);
	require(source >= 0, "open descriptor source");
	int descriptors[16];
	for (size_t i = 0; i < 16; ++i) {
		descriptors[i] = socket_bitmap_get_locked(&bitmap);
		require(descriptors[i] >= 3, "reserve real descriptor");
		int duplicate = fcntl(source, F_DUPFD_CLOEXEC, descriptors[i]);
		require(duplicate == descriptors[i], "reserve unoccupied real descriptor number");
	}
	pid_t child = fork();
	require(child >= 0, "fork real descriptor owner");
	if (child == 0) {
		for (size_t i = 0; i < 16; ++i) {
			require(close(descriptors[i]) == 0, "close inherited native descriptor");
			socket_bitmap_put_locked(&bitmap, descriptors[i]);
		}
		require(bitmap.bits == NULL, "fork cleanup clears allocation pointer");
		for (size_t i = 0; i < 16; ++i) {
			int reserved = socket_bitmap_get_locked(&bitmap);
			require(reserved == descriptors[i], "child reserves after full inherited cleanup");
			require(fcntl(source, F_DUPFD_CLOEXEC, reserved) == reserved, "child recreates real descriptors");
		}
		_exit(0);
	}
	int status;
	require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
		"child descriptor cleanup and recreation");
	require(bitmap.bit_length == 16, "child cleanup does not mutate parent registry");
	for (size_t i = 0; i < 16; ++i) {
		require(fcntl(descriptors[i], F_GETFD) >= 0, "parent retained descriptor survives");
		require(close(descriptors[i]) == 0, "close parent descriptor");
		socket_bitmap_put_locked(&bitmap, descriptors[i]);
	}
	require(close(source) == 0, "close descriptor source");
}

int main(void)
{
	test_surviving_slots();
	test_fork_descriptor_cleanup();
	puts("DARLING_MLDR_SOCKET_BITMAP_OK");
	return 0;
}
