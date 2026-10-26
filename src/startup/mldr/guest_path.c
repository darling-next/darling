#define _GNU_SOURCE 1
#include "guest_path.h"
#include <linux_premigration/eunion_resolver.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static int path_status(void* context, const char* path)
{
	(void)context;
	struct stat status;
	return lstat(path, &status) == 0 ? 0 : -errno;
}

static int path_kind(void* context, const char* path)
{
	(void)context;
	struct stat status;
	if (lstat(path, &status) != 0)
		return -errno;
	return S_ISDIR(status.st_mode) ? EUNION_RESOLVER_DIRECTORY :
		EUNION_RESOLVER_NON_DIRECTORY;
}

static int marker_status(void* context, const char* path, const char* marker)
{
	if (context == NULL)
		return 0;
	char value;
	ssize_t length = lgetxattr(path, marker, &value, sizeof(value));
	if (length >= 0)
		return length > 0 && value == 'y';
	if (errno == ERANGE) {
		char values[4096];
		length = lgetxattr(path, marker, values, sizeof(values));
		if (length >= 0)
			return length > 0 && values[0] == 'y';
	}
	return errno == ENODATA || errno == ENOTSUP || errno == ENOENT ? 0 : -errno;
}

static int path_error(int error)
{
	errno = error;
	return -1;
}

/* Neither upstream mldr nor the reviewed Vibe loader has E-UNION lookup.
 * Reuse emulation's eunion_resolver.c for all upper/lower/whiteout/opaque
 * decisions; only native filesystem callbacks and symlink walking live here.
 * Resolve each component before following a link: host open() alone would
 * incorrectly follow an absolute guest symlink against the Linux host root. */
int mldr_resolve_guest_path(const char* root, const char* lower,
	const char* guest, char* output, size_t capacity)
{
	if (guest == NULL || guest[0] != '/' || output == NULL || capacity == 0)
		return path_error(EINVAL);
	if (root == NULL || root[0] == '\0') {
		root = "";
		lower = NULL;
	}
	char pending[EUNION_RESOLVER_PATH_MAX];
	char resolved[EUNION_RESOLVER_PATH_MAX] = "";
	char candidate[EUNION_RESOLVER_PATH_MAX];
	if (strlen(guest) >= sizeof(pending))
		return path_error(ENAMETOOLONG);
	strcpy(pending, guest);
	if (lower != NULL) {
		int count = snprintf(candidate, sizeof(candidate), "%s/.union-work", root);
		if (count < 0 || (size_t)count >= sizeof(candidate))
			return path_error(ENAMETOOLONG);
		struct stat status;
		if (lstat(candidate, &status) != 0) {
			if (errno != ENOENT)
				return -1;
			lower = NULL;
		} else if (!S_ISDIR(status.st_mode)) {
			return path_error(ENOTDIR);
		}
	}
	struct eunion_resolver resolver = {
		.upper_root = root, .lower_root = lower != NULL ? lower : root,
		.whiteout_marker = "user.union.whiteout",
		.opaque_marker = "user.union.opaque",
		.missing_status = -ENOENT, .not_directory_status = -ENOTDIR,
		.context = lower != NULL ? (void*)root : NULL,
		.ops = { path_status, marker_status, path_kind },
	};
	unsigned links = 0;
	for (;;) {
		char* component = pending;
		while (*component == '/')
			++component;
		if (*component == '\0') {
			struct eunion_resolver_result result;
			if (eunion_resolve_path(&resolver, resolved[0] ? resolved : "/", &result) != 0)
				return path_error(ENAMETOOLONG);
			if (result.layer == EUNION_RESOLVER_ERROR)
				return path_error(-result.error);
			if (result.layer == EUNION_RESOLVER_MISSING || result.layer == EUNION_RESOLVER_WHITEOUT)
				return path_error(ENOENT);
			if (strlen(result.host_path) >= capacity)
				return path_error(ENAMETOOLONG);
			strcpy(output, result.host_path);
			return 0;
		}
		char* rest = strchr(component, '/');
		if (rest != NULL)
			*rest++ = '\0';
		else
			rest = component + strlen(component);
		if (strcmp(component, ".") == 0) {
			memmove(pending, rest, strlen(rest) + 1);
			continue;
		}
		if (strcmp(component, "..") == 0) {
			char* slash = strrchr(resolved, '/');
			if (slash != NULL)
				*slash = '\0';
			memmove(pending, rest, strlen(rest) + 1);
			continue;
		}
		int count = snprintf(candidate, sizeof(candidate), "%s/%s", resolved, component);
		if (count < 0 || (size_t)count >= sizeof(candidate))
			return path_error(ENAMETOOLONG);
		struct eunion_resolver_result result;
		if (eunion_resolve_path(&resolver, candidate, &result) != 0)
			return path_error(ENAMETOOLONG);
		if (result.layer == EUNION_RESOLVER_ERROR)
			return path_error(-result.error);
		if (result.layer == EUNION_RESOLVER_MISSING || result.layer == EUNION_RESOLVER_WHITEOUT)
			return path_error(ENOENT);
		struct stat status;
		if (lstat(result.host_path, &status) != 0)
			return -1;
		if (S_ISLNK(status.st_mode)) {
			if (++links > 40)
				return path_error(ELOOP);
			char target[EUNION_RESOLVER_PATH_MAX];
			ssize_t length = readlink(result.host_path, target, sizeof(target) - 1);
			if (length < 0)
				return -1;
			if ((size_t)length >= sizeof(target) - 1)
				return path_error(ENAMETOOLONG);
			target[length] = '\0';
			count = snprintf(candidate, sizeof(candidate), "%s/%s", target, rest);
			if (count < 0 || (size_t)count >= sizeof(candidate))
				return path_error(ENAMETOOLONG);
			if (target[0] == '/')
				resolved[0] = '\0';
			strcpy(pending, candidate);
			continue;
		}
		if (*rest != '\0' && !S_ISDIR(status.st_mode))
			return path_error(ENOTDIR);
		strcpy(resolved, candidate);
		memmove(pending, rest, strlen(rest) + 1);
	}
}
