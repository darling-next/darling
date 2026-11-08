#ifndef MLDR_IMAGE_SNAPSHOT_H
#define MLDR_IMAGE_SNAPSHOT_H
#include <stdint.h>
#include <sys/types.h>

/* An owned result, published only after copying and all required seals succeed.
 * Treat fd/size as read-only until closing fd; never replace fd with a raw source.
 * Capture borrows source_fd synchronously: keep it open and do not rebind it
 * during the call. Its offset and ownership are unchanged. On failure, result
 * is unchanged and no partial descriptor escapes. On success, caller closes fd.
 * Do not overwrite an already-owned result without first closing it.
 * Immutability is not atomic source capture, cryptographic validation, or
 * integrity of executable mappings: concurrent source writes can mix bytes. */
struct mldr_sealed_image {
	int fd;
	uint64_t size;
};
int mldr_capture_image(int source_fd, struct mldr_sealed_image *result);
/* Check the descriptor's kernel-enforced publication state at loader boundaries. */
int mldr_require_sealed_image(int fd);
int mldr_image_range(uint64_t size, uint64_t offset, uint64_t length);
/* Validate native-endian load-command/segment bounds, not signatures or authority. */
int mldr_validate_image(int fd, uint64_t offset, uint64_t size, int is64);
#endif
