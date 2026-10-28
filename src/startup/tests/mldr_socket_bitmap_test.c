#include "socket_bitmap.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
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
	/* Release complete groups without allowing reuse to alias the
	 * still-live first group of reservations. */
	for (size_t i = 8; i < 24; ++i)
		socket_bitmap_put_locked(&bitmap, descriptors[i]);
	for (size_t i = 8; i < 24; ++i) {
		int descriptor = socket_bitmap_get_locked(&bitmap);
		require(descriptor == descriptors[i], "reallocate without aliasing survivors");
	}
	socket_bitmap_put_locked(&bitmap, descriptors[23]);
	require(socket_bitmap_get_locked(&bitmap) == descriptors[23], "reuse partial-byte tail");
	socket_bitmap_put_locked(&bitmap, descriptors[3]);
	require(socket_bitmap_get_locked(&bitmap) == descriptors[3], "reuse interior hole");
	for (size_t i = 0; i < 24; ++i)
		socket_bitmap_put_locked(&bitmap, descriptors[i]);
	for (unsigned round = 0; round < 128; ++round) {
		for (size_t i = 0; i < 24; ++i)
			require(socket_bitmap_get_locked(&bitmap) == descriptors[i], "allocate after releasing every reservation");
		for (size_t i = 24; i > 0; --i)
			socket_bitmap_put_locked(&bitmap, descriptors[i - 1]);
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
	for (size_t i = 0; i < 16; ++i) {
		require(fcntl(descriptors[i], F_GETFD) >= 0, "parent retained descriptor survives");
		require(close(descriptors[i]) == 0, "close parent descriptor");
		socket_bitmap_put_locked(&bitmap, descriptors[i]);
	}
	require(close(source) == 0, "close descriptor source");
}

static void test_changing_descriptor_limit(void)
{
	pid_t child = fork();
	require(child >= 0, "fork isolated resource-limit owner");
	if (child == 0) {
		struct rlimit original;
		require(getrlimit(RLIMIT_NOFILE, &original) == 0 && original.rlim_cur > 64,
			"obtain reducible descriptor limit");
		int source = open("/dev/null", O_RDONLY | O_CLOEXEC);
		require(source >= 0, "open limit-change descriptor source");
		socket_bitmap_t bitmap = { .mutex = PTHREAD_MUTEX_INITIALIZER, .highest = -1 };
		int high = socket_bitmap_get_locked(&bitmap);
		require(high >= 64 && fcntl(source, F_DUPFD_CLOEXEC, high) == high,
			"retain descriptor above the future soft limit");

		struct rlimit reduced = original;
		reduced.rlim_cur = 64;
		require(setrlimit(RLIMIT_NOFILE, &reduced) == 0, "lower only the soft descriptor limit");
		int low[12];
		for (size_t i = 0; i < 12; ++i) {
			low[i] = socket_bitmap_get_locked(&bitmap);
			require(low[i] >= 3 && low[i] < 64,
				"new reservation respects the current soft limit");
			require(fcntl(source, F_DUPFD_CLOEXEC, low[i]) == low[i],
				"native duplication admits the new reservation");
		}
		require(fcntl(high, F_GETFD) == FD_CLOEXEC,
			"lowering the limit preserves an existing high descriptor");
		require(close(low[4]) == 0, "close one low descriptor");
		socket_bitmap_put_locked(&bitmap, low[4]);
		require(socket_bitmap_get_locked(&bitmap) == low[4] &&
			fcntl(source, F_DUPFD_CLOEXEC, low[4]) == low[4],
			"reuse a low hole without aliasing surviving reservations");

		require(close(high) == 0, "close the retained high descriptor below the new limit");
		socket_bitmap_put_locked(&bitmap, high);
		int extra = socket_bitmap_get_locked(&bitmap);
		require(extra >= 3 && extra < 64 && fcntl(source, F_DUPFD_CLOEXEC, extra) == extra,
			"a high free hole cannot escape the reduced limit");
		require(setrlimit(RLIMIT_NOFILE, &original) == 0, "restore the original soft limit");
		require(socket_bitmap_get_locked(&bitmap) == high &&
			fcntl(source, F_DUPFD_CLOEXEC, high) == high,
			"raising the limit makes the high hole reusable");
		require(setrlimit(RLIMIT_NOFILE, &reduced) == 0, "lower the limit before fork");

		pid_t descendant = fork();
		require(descendant >= 0, "fork with mixed high and low reservations");
		if (descendant == 0) {
			for (size_t i = 0; i < 12; ++i) {
				require(close(low[i]) == 0, "child releases inherited low descriptor");
				socket_bitmap_put_locked(&bitmap, low[i]);
			}
			require(close(extra) == 0 && close(high) == 0, "child releases other inherited descriptors");
			socket_bitmap_put_locked(&bitmap, extra);
			socket_bitmap_put_locked(&bitmap, high);
			int reused = socket_bitmap_get_locked(&bitmap);
			require(reused >= 3 && reused < 64 &&
				fcntl(source, F_DUPFD_CLOEXEC, reused) == reused,
				"empty inherited registry remains usable under the lower limit");
			_exit(0);
		}
		int status;
		require(waitpid(descendant, &status, 0) == descendant &&
			WIFEXITED(status) && WEXITSTATUS(status) == 0, "child cleanup respects the inherited limit");
		for (size_t i = 0; i < 12; ++i)
			require(fcntl(low[i], F_GETFD) == FD_CLOEXEC, "child cleanup preserves parent low descriptors");
		require(fcntl(high, F_GETFD) == FD_CLOEXEC, "child cleanup preserves parent high descriptor");
		_exit(0);
	}
	int status;
	require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
		"descriptor ownership survives soft-limit transitions");
}

int main(void)
{
	test_surviving_slots();
	test_fork_descriptor_cleanup();
	test_changing_descriptor_limit();
	puts("DARLING_MLDR_SOCKET_BITMAP_OK");
	return 0;
}
