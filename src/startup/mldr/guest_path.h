#ifndef DARLING_MLDR_GUEST_PATH_H
#define DARLING_MLDR_GUEST_PATH_H

#include <stddef.h>

/* Resolve an absolute guest image path; returns -1 with host errno on failure.
 * lower is enabled only for an upper provisioned with .union-work, matching
 * the emulation resolver's activation convention. NULL root means host root. */
int mldr_resolve_guest_path(const char* root, const char* lower,
	const char* guest, char* output, size_t capacity);

#endif
