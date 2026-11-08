#define _GNU_SOURCE
#include "image_snapshot.h"
#include <errno.h>
#include <fcntl.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s (errno=%d)\n", __LINE__, #x, errno); exit(1); } } while (0)

/* Link-time injection exercises the production partial-result cleanup. */
static int deny_sealing, fail_read, last_created = -1;
int __real_memfd_create(const char *, unsigned int);
ssize_t __real_pread64(int, void *, size_t, off64_t);
int __wrap_memfd_create(const char *name, unsigned int flags)
{
	last_created = __real_memfd_create(name,
		deny_sealing ? flags & ~MFD_ALLOW_SEALING : flags);
	return last_created;
}
ssize_t __wrap_pread64(int fd, void *buffer, size_t length, off64_t offset)
{
	if (fail_read && offset >= 16384) { errno = EIO; return -1; }
	return __real_pread64(fd, buffer, length, offset);
}

static void publication_failures(void)
{
	int source = memfd_create("failure-source", MFD_CLOEXEC);
	CHECK(source >= 0 && write(source, "data", 4) == 4);
	CHECK(ftruncate(source, 32771) == 0);
	CHECK(lseek(source, 2, SEEK_SET) == 2);
	CHECK(mldr_require_sealed_image(source) == -1 && errno == EPERM);
	for (int mode = 0; mode < 2; ++mode) {
		struct mldr_sealed_image result = { .fd = 123, .size = 456 };
		deny_sealing = mode == 0;
		fail_read = mode == 1;
		CHECK(mldr_capture_image(source, &result) == -1);
		CHECK(errno == (mode == 0 ? EPERM : EIO));
		CHECK(result.fd == 123 && result.size == 456);
		CHECK(last_created >= 0);
		CHECK(fcntl(last_created, F_GETFD) == -1 && errno == EBADF);
		CHECK(lseek(source, 0, SEEK_CUR) == 2);
		deny_sealing = fail_read = 0;
	}
	CHECK(close(source) == 0);
}

static int snapshot(const void *data, size_t length)
{
	int source = memfd_create("snapshot-fixture", MFD_CLOEXEC);
	CHECK(source >= 0);
	CHECK(write(source, data, length) == (ssize_t)length);
	struct mldr_sealed_image image = { .fd = -1, .size = UINT64_MAX };
	CHECK(mldr_capture_image(source, &image) == 0);
	int fd = image.fd;
	CHECK(fd >= 0 && image.size == length);
	CHECK(lseek(source, 0, SEEK_CUR) == (off_t)length);
	CHECK(lseek(fd, 0, SEEK_CUR) == 0);
	CHECK(fcntl(fd, F_GETFD) & FD_CLOEXEC);
	CHECK(close(source) == 0);
	return fd;
}

int main(void)
{
	publication_failures();
	/* Cross the production helper's bounded copy-buffer boundary. */
	char original[32771], actual[sizeof(original)];
	memset(original, 'A', sizeof(original));
	int source = memfd_create("mutable-source", MFD_CLOEXEC);
	CHECK(source >= 0);
	CHECK(write(source, original, sizeof(original)) == sizeof(original));
	CHECK(lseek(source, 7, SEEK_SET) == 7);
	struct mldr_sealed_image image_result = { .fd = -1, .size = 123 };
	CHECK(mldr_capture_image(source, &image_result) == 0);
	uint64_t size = image_result.size;
	int fd = image_result.fd;
	CHECK(fd >= 0 && size == sizeof(original));
	CHECK(lseek(source, 0, SEEK_CUR) == 7);
	CHECK(pwrite(source, "changed", 7, 0) == 7);
	CHECK(ftruncate(source, 0) == 0);
	CHECK(close(source) == 0);
	CHECK(read(fd, actual, sizeof(actual)) == sizeof(actual));
	CHECK(memcmp(actual, original, sizeof(actual)) == 0);
	int seals = F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
	CHECK(fcntl(fd, F_GET_SEALS) == seals);
	CHECK(mldr_require_sealed_image(fd) == 0);
	CHECK(pwrite(fd, "X", 1, 0) == -1 && errno == EPERM);
	CHECK(ftruncate(fd, 0) == -1 && errno == EPERM);
	CHECK(ftruncate(fd, size + 1) == -1 && errno == EPERM);
	CHECK(fcntl(fd, F_ADD_SEALS, 0) == -1 && errno == EPERM);
	CHECK(mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) == MAP_FAILED && errno == EPERM);
	CHECK(close(fd) == 0);
	/* Failure does not publish a size or consume the caller's descriptor. */
	struct mldr_sealed_image failed = { .fd = 456, .size = 123 };
	CHECK(mldr_capture_image(-1, &failed) == -1 && errno == EBADF);
	CHECK(failed.fd == 456 && failed.size == 123);
	int pipefd[2];
	CHECK(pipe(pipefd) == 0);
	CHECK(mldr_capture_image(pipefd[0], &failed) == -1 && errno == EINVAL);
	CHECK(failed.fd == 456 && failed.size == 123);
	CHECK(fcntl(pipefd[0], F_GETFD) >= 0);
	CHECK(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
	CHECK(mldr_capture_image(-1, NULL) == -1 && errno == EINVAL);
	fd = snapshot("", 0);
	CHECK(mldr_validate_image(fd, 0, 0, 1) == -1);
	CHECK(close(fd) == 0);

	struct { struct mach_header_64 h; struct segment_command_64 s; } image = {0};
	image.h.magic = MH_MAGIC_64;
	image.h.ncmds = 1;
	image.h.sizeofcmds = sizeof(image.s);
	image.s.cmd = LC_SEGMENT_64;
	image.s.cmdsize = sizeof(image.s);
	image.s.filesize = sizeof(image);
	image.s.vmsize = 4096;
	fd = snapshot(&image, sizeof(image));
	CHECK(mldr_validate_image(fd, 0, sizeof(image), 1) == 0);
	CHECK(mldr_validate_image(fd, 0, sizeof(image.h) - 1, 1) == -1);
	CHECK(mldr_validate_image(fd, 0, sizeof(image) - 1, 1) == -1);
	CHECK(close(fd) == 0);
	image.s.fileoff = UINT64_MAX;
	fd = snapshot(&image, sizeof(image));
	CHECK(mldr_validate_image(fd, 0, sizeof(image), 1) == -1);
	CHECK(close(fd) == 0);
	image.s.fileoff = 1;
	fd = snapshot(&image, sizeof(image));
	CHECK(mldr_validate_image(fd, 0, sizeof(image), 1) == -1);
	CHECK(close(fd) == 0);
	image.s.fileoff = 0;
	image.s.cmdsize = 0;
	fd = snapshot(&image, sizeof(image));
	CHECK(mldr_validate_image(fd, 0, sizeof(image), 1) == -1);
	CHECK(close(fd) == 0);
	/* Embedded 32-bit slice, with segment bounds relative to the slice. */
	struct {
		char prefix[4096];
		struct mach_header h;
		struct segment_command s;
	} thin32 = {0};
	thin32.h.magic = MH_MAGIC;
	thin32.h.ncmds = 1;
	thin32.h.sizeofcmds = sizeof(thin32.s);
	thin32.s.cmd = LC_SEGMENT;
	thin32.s.cmdsize = sizeof(thin32.s);
	thin32.s.filesize = sizeof(thin32.h) + sizeof(thin32.s);
	thin32.s.vmsize = 4096;
	fd = snapshot(&thin32, sizeof(thin32));
	CHECK(mldr_validate_image(fd, 4096, sizeof(thin32) - 4096, 0) == 0);
	CHECK(mldr_validate_image(fd, 4096, sizeof(thin32) - 4097, 0) == -1);
	CHECK(mldr_validate_image(fd, UINT64_MAX, 1, 0) == -1);
	CHECK(mldr_validate_image(fd, 4096, sizeof(thin32) - 4096, 1) == -1);
	CHECK(close(fd) == 0);
	/* The same subtraction-based range check gates fat tables and selected slices. */
	CHECK(mldr_image_range(4096, 4096, 0) == 0);
	CHECK(mldr_image_range(4096, 4095, 1) == 0);
	CHECK(mldr_image_range(4096, 4096, 1) == -1);
	CHECK(mldr_image_range(4096, 4097, 0) == -1);
	CHECK(mldr_image_range(UINT64_MAX, UINT64_MAX - 1, 3) == -1);
	puts("mldr immutable image capture: PASS");
	return 0;
}
