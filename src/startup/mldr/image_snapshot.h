#ifndef MLDR_IMAGE_SNAPSHOT_H
#define MLDR_IMAGE_SNAPSHOT_H
#include <stdint.h>
#include <sys/types.h>

/* Borrow source_fd, preserving its offset and ownership. Return a CLOEXEC sealed
 * descriptor positioned at zero, or -1 with errno. No authorization is implied;
 * concurrent source writes during capture can produce a mixed, but immutable image. */
int mldr_capture_image(int source_fd, uint64_t *size);
int mldr_image_range(uint64_t size, uint64_t offset, uint64_t length);
/* Validate native-endian load-command/segment bounds, not signatures or authority. */
int mldr_validate_image(int fd, uint64_t offset, uint64_t size, int is64);
#endif
