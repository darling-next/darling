#define _GNU_SOURCE 1
#include "guest_path.h"
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static void require(int condition, const char* detail)
{
	if (!condition) {
		fprintf(stderr, "FAIL %s: %s\n", detail, strerror(errno));
		exit(1);
	}
}

static void join(char* output, size_t size, const char* root, const char* suffix)
{
	int length = snprintf(output, size, "%s%s", root, suffix);
	require(length >= 0 && (size_t)length < size, "fixture path overflow");
}

static void file(const char* path, const char* content)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	require(fd >= 0, path);
	require(write(fd, content, strlen(content)) == (ssize_t)strlen(content), "write fixture");
	require(close(fd) == 0, "close fixture");
}

static void expect(const char* upper, const char* lower, const char* guest,
	const char* root, const char* suffix, const char* content)
{
	char actual[4096], wanted[4096];
	join(wanted, sizeof(wanted), root, suffix);
	require(mldr_resolve_guest_path(upper, lower, guest, actual, sizeof(actual)) == 0,
		guest);
	require(strcmp(actual, wanted) == 0, "selected incorrect layer");
	int fd = open(actual, O_RDONLY | O_CLOEXEC);
	require(fd >= 0, "open resolved image");
	char bytes[64];
	ssize_t length = read(fd, bytes, sizeof(bytes));
	require(length == (ssize_t)strlen(content) && memcmp(bytes, content, length) == 0,
		"resolved image contents");
	require(close(fd) == 0, "close resolved image");
}

static void reject(const char* upper, const char* lower, const char* guest, int error)
{
	char actual[4096];
	require(mldr_resolve_guest_path(upper, lower, guest, actual, sizeof(actual)) != 0 &&
		errno == error, "incorrect failed resolution");
}

static int remove_entry(const char* path, const struct stat* status, int kind,
	struct FTW* walk)
{
	(void)status;
	(void)kind;
	(void)walk;
	return remove(path);
}

int main(void)
{
	const char* state = getenv("DW_STATE_ROOT");
	require(state != NULL && state[0] == '/', "absolute owned DW_STATE_ROOT required");
	char scratch[4096], upper[4096], lower[4096], path[4096];
	join(scratch, sizeof(scratch), state, "/mldr-guest-path.XXXXXX");
	require(mkdtemp(scratch) != NULL, "create fixture");
	join(upper, sizeof(upper), scratch, "/upper");
	join(lower, sizeof(lower), scratch, "/lower");
	require(mkdir(upper, 0700) == 0 && mkdir(lower, 0700) == 0, "create layers");
	const char* directories[] = { "/sbin", "/usr", "/usr/lib" };
	for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
		join(path, sizeof(path), lower, directories[i]);
		require(mkdir(path, 0700) == 0, "create installed directories");
	}
	join(path, sizeof(path), lower, "/sbin/launchd");
	file(path, "installed-launchd");
	join(path, sizeof(path), lower, "/usr/lib/dyld");
	file(path, "installed-dyld");
	/* Copy-mode roots must not silently acquire an installed lower. */
	reject(upper, lower, "/sbin/launchd", ENOENT);
	join(path, sizeof(path), upper, "/.union-work");
	require(mkdir(path, 0700) == 0, "activate production union");
	expect(upper, lower, "/sbin/launchd", lower, "/sbin/launchd", "installed-launchd");
	expect(upper, lower, "/usr/lib/dyld", lower, "/usr/lib/dyld", "installed-dyld");
	for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
		join(path, sizeof(path), upper, directories[i]);
		require(mkdir(path, 0700) == 0, "create guest upper directories");
	}
	join(path, sizeof(path), upper, "/usr/lib/dyld");
	file(path, "guest-dyld");
	expect(upper, lower, "/usr/lib/dyld", upper, "/usr/lib/dyld", "guest-dyld");
	join(path, sizeof(path), upper, "/usr/lib/absolute");
	require(symlink("/usr/lib/dyld", path) == 0, "absolute guest link");
	expect(upper, lower, "/usr/lib/absolute", upper, "/usr/lib/dyld", "guest-dyld");
	join(path, sizeof(path), upper, "/usr/lib/relative");
	require(symlink("../../sbin/launchd", path) == 0, "relative guest link");
	expect(upper, lower, "/usr/lib/relative", lower, "/sbin/launchd", "installed-launchd");
	join(path, sizeof(path), upper, "/sbin/launchd");
	file(path, "whiteout");
	require(lsetxattr(path, "user.union.whiteout", "y", 1, 0) == 0, "create whiteout");
	reject(upper, lower, "/sbin/launchd", ENOENT);
	require(unlink(path) == 0, "remove whiteout");
	join(path, sizeof(path), upper, "/sbin");
	require(lsetxattr(path, "user.union.opaque", "y", 1, 0) == 0, "create opaque parent");
	reject(upper, lower, "/sbin/launchd", ENOENT);
	require(lremovexattr(path, "user.union.opaque") == 0, "remove opaque parent");
	join(path, sizeof(path), upper, "/loop");
	require(symlink("/loop", path) == 0, "create guest link loop");
	reject(upper, lower, "/loop", ELOOP);
	reject(upper, lower, "/usr/lib/dyld/child", ENOTDIR);
	char tiny[2];
	require(mldr_resolve_guest_path(upper, lower, "/usr/lib/dyld", tiny, sizeof(tiny)) != 0 &&
		errno == ENAMETOOLONG, "bounded image path");
	expect(lower, NULL, "/usr/lib/dyld", lower, "/usr/lib/dyld", "installed-dyld");
	join(path, sizeof(path), lower, "/usr/lib/dyld");
	char bytes[64];
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	require(fd >= 0 && read(fd, bytes, sizeof(bytes)) == (ssize_t)strlen("installed-dyld") &&
		memcmp(bytes, "installed-dyld", strlen("installed-dyld")) == 0,
		"installed lower changed");
	require(close(fd) == 0, "close immutable lower");
	require(nftw(scratch, remove_entry, 32, FTW_DEPTH | FTW_PHYS) == 0, "remove owned fixture");
	puts("DARLING_MLDR_GUEST_PATH_OK");
	return 0;
}
