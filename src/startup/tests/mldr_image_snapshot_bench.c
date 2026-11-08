#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "image_snapshot.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void die(const char *what) { perror(what); exit(1); }
static uint64_t now(void)
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t)) die("clock_gettime");
	return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static void read_file(int fd, uint64_t size)
{
	char buffer[16384];
	uint64_t offset = 0;
	while (offset < size) {
		size_t count = size - offset > sizeof(buffer) ? sizeof(buffer) : (size_t)(size - offset);
		ssize_t n = pread(fd, buffer, count, (off_t)offset);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) { if (!n) errno = EIO; die("baseline pread"); }
		offset += n;
	}
}
int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s ITERATIONS GENUINE_MAIN GENUINE_DYLD [OTHER_IMAGE...]\n", argv[0]);
		return 2;
	}
	char *end;
	unsigned long iterations = strtoul(argv[1], &end, 10);
	if (!iterations || *end || iterations > 1000000) return 2;
	puts("scope=warm-source-page-cache; baseline=full-file-pread-16KiB; capture=copy+seal+close; excludes=open,parse,map,guest-execution");
	for (int i = 2; i < argc; ++i) {
		int fd = open(argv[i], O_RDONLY | O_CLOEXEC);
		if (fd < 0) die("open");
		struct stat st;
		if (fstat(fd, &st)) die("fstat");
		if (!S_ISREG(st.st_mode) || st.st_size <= 0) return 2;
		uint64_t size = st.st_size, baseline = 0, capture = 0;
		read_file(fd, size); /* Deliberately warm the source; never drop global caches. */
		for (unsigned long j = 0; j < iterations; ++j) {
			/* Alternate order to reduce systematic order/thermal bias. */
			for (int k = 0; k < 2; ++k) {
				int capturing = (j + k) & 1;
				uint64_t start = now();
				if (capturing) {
					struct mldr_sealed_image image;
					if (mldr_capture_image(fd, &image)) die("capture");
					if (image.size != size) { fprintf(stderr, "source size changed\n"); return 1; }
					if (close(image.fd)) die("close snapshot");
				} else read_file(fd, size);
				uint64_t elapsed = now() - start;
				if (capturing) capture += elapsed; else baseline += elapsed;
			}
		}
		printf("path=%s iterations=%lu file_bytes=%" PRIu64 " baseline_read_bytes=%" PRIu64 " capture_read_bytes=%" PRIu64 " capture_written_bytes=%" PRIu64 " baseline_ns=%" PRIu64 " capture_ns=%" PRIu64 " delta_ns_per_image=%.1f ratio=%.3f\n",
			argv[i], iterations, size, size * iterations, size * iterations, size * iterations,
			baseline, capture, ((double)capture - baseline) / iterations, baseline ? (double)capture / baseline : 0);
		if (close(fd)) die("close source");
	}
	return 0;
}
