/* Copyright (C) 2026 Darling contributors. SPDX-License-Identifier: GPL-3.0-or-later */

#import "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

/* OS_object owns the reference-count prefix; the remaining payload is C data. */

_OS_OBJECT_DECL_SUBCLASS_INTERFACE(cryptex, object)

@implementation OS_cryptex
- (void)dealloc
{
	struct cryptex_s *cryptex = (struct cryptex_s *)self;

	[cryptex->variant release];
	[cryptex->signatures release];
	[cryptex->version release];
	[cryptex->preauthorization_version release];
	for (size_t i = 0; i < CRYPTEX_ASSET_COUNT; i++) {
		if (cryptex->assets[i].fd >= 0) {
			close(cryptex->assets[i].fd);
		}
		free(cryptex->assets[i].path);
	}
	[super dealloc];
}
@end

_OS_OBJECT_DECL_SUBCLASS_INTERFACE(cryptex_attr, object)

@implementation OS_cryptex_attr
@end

_OS_OBJECT_DECL_SUBCLASS_INTERFACE(cryptex_bundle, object)

@implementation OS_cryptex_bundle
- (void)dealloc
{
	struct cryptex_bundle_s *bundle = (struct cryptex_bundle_s *)self;

	if (bundle->directory_fd >= 0) {
		close(bundle->directory_fd);
	}
	if (bundle->restore_fd >= 0) {
		close(bundle->restore_fd);
	}
	[bundle->manifest release];

	struct cryptex_s *entry = bundle->entries;
	while (entry != NULL) {
		struct cryptex_s *next = entry->next;
		[(id)entry release];
		entry = next;
	}
	[super dealloc];
}
@end

struct cryptex_s *
_cryptex_alloc(void)
{
	struct cryptex_s *cryptex = (struct cryptex_s *)_os_object_alloc(
	    [OS_cryptex class], sizeof(*cryptex));
	for (size_t i = 0; i < CRYPTEX_ASSET_COUNT; i++) {
		cryptex->assets[i].fd = -1;
	}

	return cryptex;
}

struct cryptex_bundle_s *
_cryptex_bundle_alloc(void)
{
	struct cryptex_bundle_s *bundle = (struct cryptex_bundle_s *)_os_object_alloc(
	    [OS_cryptex_bundle class], sizeof(*bundle));
	bundle->directory_fd = -1;
	bundle->restore_fd = -1;

	return bundle;
}

/* Resolve the owned canonical path before transferring descriptor ownership. */
int
_cryptex_asset_claim(struct cryptex_asset *asset, int *fd)
{
	char path[MAXPATHLEN];
	char *owned_path;
	int local_fd = *fd;

	if (fcntl(local_fd, F_GETPATH, path) != 0) {
		return errno;
	}
	owned_path = strdup(path);
	if (owned_path == NULL) {
		return ENOMEM;
	}

	asset->fd = local_fd;
	asset->path = owned_path;
	*fd = -1;
	return 0;
}

/* All assets constructed here own their descriptors. */
void
_cryptex_asset_close(struct cryptex_asset *asset)
{
	if (asset->fd < 0) {
		return;
	}
	close(asset->fd);
	asset->fd = -1;
}

int
_cryptex_assets_open(struct cryptex_s *cryptex)
{
	for (size_t i = 0; i < CRYPTEX_ASSET_COUNT; i++) {
		struct cryptex_asset *asset = &cryptex->assets[i];

		if (asset->path == NULL || asset->fd >= 0) {
			continue;
		}
		asset->fd = open(asset->path, O_RDONLY);
		if (asset->fd < 0) {
			return errno;
		}
	}
	return 0;
}

void
_cryptex_assets_close(struct cryptex_s *cryptex)
{
	for (size_t i = 0; i < CRYPTEX_ASSET_COUNT; i++) {
		_cryptex_asset_close(&cryptex->assets[i]);
	}
}

cryptex_attr_t
cryptex_attr_create(void)
{
	struct cryptex_attr_s *attributes = (struct cryptex_attr_s *)_os_object_alloc(
	    [OS_cryptex_attr class], sizeof(*attributes));
	return (cryptex_attr_t)attributes;
}

void
cryptex_attr_set_authentication(cryptex_attr_t attributes, uint64_t value)
{
	((struct cryptex_attr_s *)attributes)->authentication = value;
}

void
cryptex_attr_set_personalization_flags(cryptex_attr_t attributes, uint64_t value)
{
	((struct cryptex_attr_s *)attributes)->personalization_flags = value;
}

void
cryptex_attr_set_mount_flags(cryptex_attr_t attributes, uint64_t value)
{
	((struct cryptex_attr_s *)attributes)->mount_flags = value;
}
