#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

extern int lkm_call(int nr, ...);
extern int __darling_vchroot(int dfd);


int main(int argc, const char** argv)
{
    if (argc < 3)
	{
		fprintf(stderr, "vchroot <dir> <binary> [args...]\n");
		return 1;
	}

	char buf[4096];
	sprintf(buf, "%s%s", argv[1], argv[2]);

	if (access(buf, F_OK) != 0)
	{
		fprintf(stderr, "Target executable not found: %s\n", buf);
		return 5;
	}

	int dfd = open(argv[1], O_RDONLY | O_DIRECTORY);
	if (dfd == -1)
	{
		perror("open");
		return 1;
	}

	if (fchdir(dfd) == -1)
	{
		perror("fchdir");
		return 2;
	}

	if (__darling_vchroot(dfd) < 0)
	{
		perror("vchroot");
		return 3;
	}

	close(dfd);

	// The root is needed by the LOADER for the image this helper is about to exec: it is what prefixes the
	// guest's own paths, including the dylinker image the loader opens for the new image. It must not reach the
	// guest under this public name, so it is RENAMED to the loader's private form rather than removed.
	// MEASURED: removing it left the second image of a process with no root at all (its deferred checkin reported
	// status 0 and then the loader built the unprefixed guest path for /usr/lib/dyld and failed with ENOENT),
	// while the first image -- started without this helper -- had the root and loaded its dylinker fine.
	{
		const char* dyld_root = getenv("DYLD_ROOT_PATH");
		if (dyld_root != NULL && dyld_root[0] != '\0') {
			setenv("__mldr_DYLD_ROOT_PATH", dyld_root, 1);
		}
	}
	unsetenv("DYLD_ROOT_PATH");

	// printf("Will execv %s\n", argv[2]);
	execv(argv[2], (char * const *) argv+2);
	perror("execv");

	return 4;
}

