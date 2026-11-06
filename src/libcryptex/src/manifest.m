/* Copyright (C) 2026 Darling contributors. SPDX-License-Identifier: GPL-3.0-or-later */

#import "internal.h"

#include <copyfile.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* The manifest is retained as a descriptor-backed asset, not authenticated here. */

static struct cryptex_asset *
_cryptex_manifest_asset(struct cryptex_s *cryptex, bool *present)
{
	struct cryptex_asset *asset = &cryptex->assets[CRYPTEX_ASSET_MANIFEST];
	*present = (asset->path != NULL || asset->fd >= 0);
	return asset;
}

int
cryptex_attach_personalized_manifest(cryptex_t cryptex, int descriptor)
{
	struct cryptex_asset *asset;
	bool present;
	struct cryptex_asset claimed = { .fd = -1, .path = NULL };
	int local_fd;
	int error;

	asset = _cryptex_manifest_asset((struct cryptex_s *)cryptex, &present);
	if (present) {
		return EEXIST;
	}

	local_fd = dup(descriptor);
	if (local_fd < 0) {
		return errno;
	}

	error = _cryptex_asset_claim(&claimed, &local_fd);
	if (error != 0) {
		if (local_fd >= 0) {
			close(local_fd);
		}
		/* The public wrapper maps asset-construction failure to ENOMEM. */
		return ENOMEM;
	}

	*asset = claimed;
	return 0;
}

int
cryptex_fcopy_personalized_manifest(cryptex_t cryptex, int descriptor)
{
	struct cryptex_asset *asset;
	bool present;
	int duplicate;
	int result;
	int error;

	/* Missing source takes precedence over an invalid destination descriptor. */
	asset = _cryptex_manifest_asset((struct cryptex_s *)cryptex, &present);
	if (!present) {
		return ENOENT;
	}

	duplicate = dup(descriptor);
	if (duplicate < 0) {
		return errno;
	}

	if (asset->fd < 0) {
		close(duplicate);
		return EBADF;
	}

	result = fcopyfile(asset->fd, descriptor, NULL, COPYFILE_ALL);
	error = (result == 0) ? 0 : errno;
	close(duplicate);
	return error;
}
