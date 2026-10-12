/*
 * Darling - libquarantine
 * Copyright (c) 2016 Lubos Dolezel, All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 */

#include "quarantine.h"
#include <errno.h>
#include <limits.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>

const char* _qtn_xattr_name = "com.apple.quarantine";

static const char* error_strings[] = {
	"Undefined error: 0",
	"Item not quarantined",
	"Quarantine is unavailable",
	"Failed to load a compatible version of libMatch"
};

struct _qtn_file_s
{
	char path[NAME_MAX];
};

qtn_file_t _qtn_file_alloc()
{
	qtn_file_t f = (qtn_file_t) malloc(sizeof(struct _qtn_file_s));
	memset(f, 0, sizeof(*f));
	return f;
}

void _qtn_file_free(qtn_file_t f)
{
	free(f);
}

qtn_file_t _qtn_file_clone(qtn_file_t f)
{
	qtn_file_t rv = _qtn_file_alloc();
	memcpy(rv, f, sizeof(*rv));
	return rv;
}

int _qtn_file_init_with_fd(qtn_file_t file, int fd)
{
	return QTN_NOT_QUARANTINED;
}

int _qtn_file_apply_to_fd(qtn_file_t file, int fd)
{
	return QTN_NOT_QUARANTINED;
}

int _qtn_file_apply_to_path(qtn_file_t file, const char* path)
{
	return QTN_NOT_QUARANTINED;
}

int _qtn_file_init_with_path(qtn_file_t file, const char *path)
{
	return QTN_NOT_QUARANTINED;
}

int _qtn_file_init_with_data(qtn_file_t file, const void *data, size_t len)
{
	return QTN_NOT_QUARANTINED;
}

int _qtn_file_to_data(qtn_file_t file, void * data, size_t* len)
{
	return QTN_NOT_QUARANTINED;
}

const char *_qtn_error(int err)
{
	if (err >= 0 || -err >= sizeof(error_strings)/sizeof(error_strings[0]))
		return strerror(err);
	return error_strings[-err];
}

int _qtn_file_set_flags(qtn_file_t file, uint32_t flags)
{
	return 0;
}

uint32_t _qtn_file_get_flags(qtn_file_t file)
{
	return 0;
}

int __esp_enabled()
{
        return QTN_NOT_QUARANTINED;
}

int __esp_check_ns(const char *a, void *b)
{
        return QTN_NOT_QUARANTINED;
}

int __esp_notify_ns(const char *a, void *b)
{
        return QTN_NOT_QUARANTINED;
}

struct _qtn_proc_t
{
	int dummy;
};

qtn_proc_t qtn_proc_alloc(void)
{
	return (qtn_proc_t) malloc(sizeof(qtn_proc_t));
}

void qtn_proc_set_identifier(qtn_proc_t proc, const char* ident)
{
}

void qtn_proc_set_flags(qtn_proc_t proc, unsigned int flags)
{
}

int qtn_proc_apply_to_self(qtn_proc_t proc) {
	return QTN_NOT_QUARANTINED;
};

void qtn_proc_free(qtn_proc_t proc)
{
	free(proc);
}

int qtn_file_apply_to_mount_point(qtn_file_t a, const char *b)
{
	return 0;
}

int qtn_file_init_with_mount_point(qtn_file_t a, char b[1024])
{
	return 0;
}

int qtn_proc_init_with_data(qtn_proc_t proc, void* data, size_t data_len) {
	return 0;
};

/*
 * Apple exports responsibility_spawnattrs_setdisclaim() from libquarantine.dylib
 * (see the SDK's libquarantine.tbd, which also carries the rest of the
 * responsibility_* family) and declares it in <responsibility.h>. The demanding
 * client is DVTInstrumentsFoundation, which calls it as
 * responsibility_spawnattrs_setdisclaim(&attr, 1) on a freshly initialised
 * posix_spawnattr_t, immediately before posix_spawn_file_actions_init, and
 * DISCARDS the result.
 *
 * Darling has no responsibility subsystem: no process is attributed to a
 * responsible parent, and there is no spawn-attribute bit or kernel plumbing
 * that could record the request. Apple's contract is 0 on success, -1 with errno
 * on failure, so the honest answer for a disclaim this guest cannot record is a
 * failure -- never a fabricated success. Clearing a disclaim that was never set
 * is genuinely a no-op, so disclaim == 0 succeeds.
 */
int responsibility_spawnattrs_setdisclaim(posix_spawnattr_t *attrs, int disclaim)
{
	if (attrs == NULL || *attrs == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (disclaim) {
		errno = ENOTSUP;
		return -1;
	}

	return 0;
}
