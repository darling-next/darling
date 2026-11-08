#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "image_snapshot.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mach-o/loader.h>

int mldr_image_range(uint64_t size, uint64_t offset, uint64_t length)
{
	if (offset > size || length > size - offset) {
		errno = ENOEXEC;
		return -1;
	}
	return 0;
}

static int read_exact(int fd, void *buffer, size_t size, uint64_t offset)
{
	char *p = buffer;
	while (size) {
		ssize_t n = pread(fd, p, size, (off_t)offset);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) {
			if (!n) errno = ENOEXEC;
			return -1;
		}
		p += n;
		size -= n;
		offset += n;
	}
	return 0;
}

int mldr_capture_image(int source_fd, uint64_t *size)
{
	struct stat st;
	char buffer[16384];
	uint64_t offset = 0;
	if (!size) { errno = EINVAL; return -1; }
	if (fstat(source_fd, &st) < 0) return -1;
	if (!S_ISREG(st.st_mode) || st.st_size < 0) { errno = EINVAL; return -1; }
	int fd = memfd_create("mldr-image", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (fd < 0) return -1;
	while (offset < (uint64_t)st.st_size) {
		uint64_t remaining = (uint64_t)st.st_size - offset;
		size_t count = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
		if (read_exact(source_fd, buffer, count, offset) < 0) goto fail;
		size_t written = 0;
		while (written < count) {
			ssize_t n = write(fd, buffer + written, count - written);
			if (n < 0 && errno == EINTR) continue;
			if (n <= 0) { if (!n) errno = EIO; goto fail; }
			written += n;
		}
		offset += count;
	}
	if (fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) < 0 ||
		lseek(fd, 0, SEEK_SET) < 0) goto fail;
	*size = offset;
	return fd;
fail:
	{
		int error = errno;
		close(fd);
		errno = error;
		return -1;
	}
}

int mldr_validate_image(int fd, uint64_t offset, uint64_t size, int is64)
{
	union { struct mach_header h; struct mach_header_64 h64; } header;
	size_t hsize = is64 ? sizeof(header.h64) : sizeof(header.h);
	if (offset > INT64_MAX || size > INT64_MAX - offset ||
		mldr_image_range(size, 0, hsize) < 0 || read_exact(fd, &header, hsize, offset) < 0) return -1;
	if (header.h.magic != (is64 ? MH_MAGIC_64 : MH_MAGIC)) goto malformed;
	uint64_t end = hsize + (uint64_t)header.h.sizeofcmds;
	if (offset > (uint64_t)(SIZE_MAX >> 1) || size > (uint64_t)(SIZE_MAX >> 1) - offset ||
		end > SIZE_MAX - 4095) goto malformed;
	if (mldr_image_range(size, hsize, header.h.sizeofcmds) < 0) return -1;
	uint64_t p = hsize;
	for (uint32_t i = 0; i < header.h.ncmds; ++i) {
		struct load_command lc;
		if (mldr_image_range(end, p, sizeof(lc)) < 0 || read_exact(fd, &lc, sizeof(lc), offset + p) < 0) return -1;
		if (lc.cmdsize < sizeof(lc) || (lc.cmdsize & 3) || mldr_image_range(end, p, lc.cmdsize) < 0) goto malformed;
		size_t minimum = sizeof(lc);
		if (lc.cmd == (is64 ? LC_SEGMENT_64 : LC_SEGMENT)) {
			union { struct segment_command s; struct segment_command_64 s64; } seg;
			minimum = is64 ? sizeof(seg.s64) : sizeof(seg.s);
			if (lc.cmdsize < minimum || read_exact(fd, &seg, minimum, offset + p) < 0) goto malformed;
			uint64_t fileoff = is64 ? seg.s64.fileoff : seg.s.fileoff;
			uint64_t filesize = is64 ? seg.s64.filesize : seg.s.filesize;
			uint64_t vmaddr = is64 ? seg.s64.vmaddr : seg.s.vmaddr;
			uint64_t vmsize = is64 ? seg.s64.vmsize : seg.s.vmsize;
			uint32_t sections = is64 ? seg.s64.nsects : seg.s.nsects;
			size_t section_size = is64 ? sizeof(struct section_64) : sizeof(struct section);
			if (sections > (lc.cmdsize - minimum) / section_size || filesize > vmsize ||
				vmaddr > UINTPTR_MAX || vmsize > UINTPTR_MAX - vmaddr ||
				mldr_image_range(size, fileoff, filesize) < 0) goto malformed;
		} else if (lc.cmd == LC_LOAD_DYLINKER) minimum = sizeof(struct dylinker_command);
		else if (lc.cmd == LC_MAIN) minimum = sizeof(struct entry_point_command);
		else if (lc.cmd == LC_UUID) minimum = sizeof(struct uuid_command);
		else if (lc.cmd == LC_UNIXTHREAD) minimum = is64 ? 19 * sizeof(uint64_t) : 15 * sizeof(uint32_t);
		if (lc.cmdsize < minimum) goto malformed;
		p += lc.cmdsize;
	}
	if (p != end) goto malformed;
	return 0;
malformed:
	errno = ENOEXEC;
	return -1;
}
