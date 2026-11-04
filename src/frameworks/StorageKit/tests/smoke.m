/*
 * Guest behavior fixture for the hdiutil-backed StorageKit provider.
 *
 * It exercises the measured CoreDevice call surface and the provider's real
 * kernel consequences:
 *   initWithURL:error:, SKDiskImageAttachParams init/setIsManagedAttach:/mountParams,
 *   SKMountParams setReadOnly:/setNoBrowse:/setAllParams:/setMountPoint:,
 *   attachWithParams:error:, attachNoMountWithError:, isAttached,
 *   SKManager syncSharedManager/diskForPath:, SKDisk isDiskImage/isWritable,
 *   and detachWithError:.
 *
 * Usage:
 *   smoke IMAGE MOUNT EXPECTED CORRUPT
 *
 * IMAGE     absolute guest path to a mountable image
 * MOUNT     absolute guest mount point; its parent directory must exist
 * EXPECTED  a copy of payload.txt inside the image
 * CORRUPT   the same file with at least one byte changed
 *
 * Every child process is spawned with an argument vector (never a shell string).
 * A child that cannot be started, or whose wait fails, is a fixture failure: no
 * in-process fallback converts it into a pass.
 */

#import "SKInternal.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int failures = 0;

static void sk_check(int condition, const char *what)
{
	if (condition) {
		printf("SK_CHECK=OK %s\n", what);
	} else {
		printf("SK_CHECK=FAIL %s\n", what);
		failures++;
	}
}

static void sk_remove(NSString *path)
{
	if (path != nil) {
		[[NSFileManager defaultManager] removeItemAtPath:path error:NULL];
	}
}

/*
 * Spawn `tool` with an explicit argument vector. Returns the child's exit status
 * (or 128+signal), or 127 when the child could not be spawned or its wait failed;
 * runErrorOut then carries the real errno instead of a fabricated status 0.
 */
static int runTool(NSString *tool, NSArray *arguments, int *runErrorOut, NSString **outputOut)
{
	if (runErrorOut != NULL) {
		*runErrorOut = 0;
	}
	if (outputOut != NULL) {
		*outputOut = nil;
	}

	char temporary[] = "/tmp/storagekit-smoke-XXXXXX";
	int fd = mkstemp(temporary);
	if (fd < 0) {
		if (runErrorOut != NULL) {
			*runErrorOut = errno;
		}
		return 127;
	}
	close(fd);

	NSUInteger argumentCount = [arguments count];
	char **argv = calloc(argumentCount + 2, sizeof(char *));
	if (argv == NULL) {
		if (runErrorOut != NULL) {
			*runErrorOut = ENOMEM;
		}
		unlink(temporary);
		return 127;
	}
	argv[0] = (char *)[tool fileSystemRepresentation];
	for (NSUInteger i = 0; i < argumentCount; i++) {
		argv[i + 1] = (char *)[[arguments objectAtIndex:i] fileSystemRepresentation];
	}
	argv[argumentCount + 1] = NULL;

	posix_spawn_file_actions_t actions;
	bool actionsInitialized = false;
	int actionError = posix_spawn_file_actions_init(&actions);
	if (actionError == 0) {
		actionsInitialized = true;
		actionError = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	}
	if (actionError == 0) {
		actionError = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, temporary,
			O_WRONLY | O_CREAT | O_TRUNC, 0600);
	}
	if (actionError == 0) {
		actionError = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
	}

	pid_t pid = 0;
	int spawnError = -1;
	if (actionError == 0) {
		spawnError = posix_spawn(&pid, [tool fileSystemRepresentation], &actions, NULL, argv, environ);
	}
	if (actionsInitialized) {
		posix_spawn_file_actions_destroy(&actions);
	}
	free(argv);

	if (actionError != 0 || spawnError != 0) {
		if (runErrorOut != NULL) {
			*runErrorOut = actionError != 0 ? actionError : spawnError;
		}
		unlink(temporary);
		return 127;
	}

	int waitStatus = 0;
	for (;;) {
		pid_t waited = waitpid(pid, &waitStatus, 0);
		if (waited >= 0) {
			break;
		}
		if (errno == EINTR) {
			continue;
		}
		if (runErrorOut != NULL) {
			*runErrorOut = errno;
		}
		unlink(temporary);
		return 127;
	}

	NSString *collected = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:temporary]
		encoding:NSUTF8StringEncoding
		error:NULL];
	unlink(temporary);
	if (outputOut != NULL) {
		*outputOut = collected;
	}

	if (WIFEXITED(waitStatus)) {
		return WEXITSTATUS(waitStatus);
	}
	if (WIFSIGNALED(waitStatus)) {
		return 128 + WTERMSIG(waitStatus);
	}
	if (runErrorOut != NULL) {
		*runErrorOut = EIO;
	}
	return 127;
}

static int runCmp(NSString *left, NSString *right, int *runErrorOut, NSString **outputOut)
{
	NSArray *arguments = [NSArray arrayWithObjects:@"-s", left, right, nil];
	return runTool(@"/usr/bin/cmp", arguments, runErrorOut, outputOut);
}

static int runExternalDetach(NSString *mountPoint, int *runErrorOut, NSString **outputOut)
{
	NSArray *arguments = [NSArray arrayWithObjects:@"detach", mountPoint, nil];
	return runTool(@"/usr/bin/hdiutil", arguments, runErrorOut, outputOut);
}

static bool outputIsLoaderFailure(NSString *output)
{
	if (output == nil) {
		return false;
	}
	return [output rangeOfString:@"Library not loaded"].location != NSNotFound ||
		[output rangeOfString:@"Symbol not found"].location != NSNotFound ||
		[output rangeOfString:@"image not found"].location != NSNotFound ||
		[output rangeOfString:@"dyld: "].location != NSNotFound;
}

int main(int argc, char **argv)
{
	if (argc != 5) {
		fprintf(stderr, "usage: %s IMAGE MOUNT EXPECTED CORRUPT\n", argv[0]);
		return 2;
	}

	NSString *imagePath = [NSString stringWithUTF8String:argv[1]];
	NSString *mountPoint = [NSString stringWithUTF8String:argv[2]];
	NSString *expectedPath = [NSString stringWithUTF8String:argv[3]];
	NSString *corruptPath = [NSString stringWithUTF8String:argv[4]];
	if (![imagePath isAbsolutePath] || ![mountPoint isAbsolutePath] ||
		![expectedPath isAbsolutePath] || ![corruptPath isAbsolutePath]) {
		fprintf(stderr, "IMAGE, MOUNT, EXPECTED and CORRUPT must be absolute guest paths\n");
		return 2;
	}
	mountPoint = [mountPoint stringByStandardizingPath];
	NSString *mountParent = [mountPoint stringByDeletingLastPathComponent];
	NSString *mountLeaf = [mountPoint lastPathComponent];

	@autoreleasepool {
		NSError *error = nil;
		SKManager *manager = [SKManager syncSharedManager];
		sk_check(manager != nil, "manager");
		SKDisk *root = [manager diskForPath:@"/"];
		printf("SK_ROOT_MOUNT=%s\n", root != nil ? [[root mountPoint] UTF8String] : "(nil)");
		sk_check(root != nil && [root isDiskImage] == 0, "host-mount-not-image");

		NSURL *url = [NSURL fileURLWithPath:imagePath];

		/*
		 * attachNoMount must fail closed: this provider's only attach primitive
		 * mounts, so the call reports ENOTSUP without mounting anything.
		 */
		SKDiskImage *noMountImage = [[SKDiskImage alloc] initWithURL:url error:&error];
		sk_check(noMountImage != nil, "no-mount-init");
		sk_check([noMountImage isAttached] == 0, "no-mount-initially-detached");
		error = nil;
		id noMountResult = [noMountImage attachNoMountWithError:&error];
		printf("SK_NO_MOUNT_ERROR_CODE=%ld\n", error != nil ? (long)[error code] : -1L);
		sk_check(noMountResult == nil, "no-mount-result-nil");
		sk_check(error != nil && [error code] == ENOTSUP, "no-mount-enotsup");
		sk_check([noMountImage isAttached] == 0, "no-mount-no-live-attachment");
		[noMountImage release];

		/* Real attach with a caller-specified mount point and requested read-only. */
		SKDiskImage *image = [[SKDiskImage alloc] initWithURL:url error:&error];
		sk_check(image != nil, "image-init");

		SKDiskImageAttachParams *params = [[SKDiskImageAttachParams alloc] init];
		SKMountParams *mountParams = [params mountParams];
		sk_check(mountParams != nil, "mount-params");
		[params setIsManagedAttach:YES];
		[mountParams setReadOnly:YES];
		[mountParams setNoBrowse:YES];
		[mountParams setAllParams:[NSSet setWithObject:@"storagekit-smoke"]];
		[mountParams setMountPoint:[NSURL fileURLWithPath:mountPoint]];

		error = nil;
		SKDisk *disk = [image attachWithParams:params error:&error];
		if (disk == nil) {
			printf("SK_ATTACH_ERROR=%ld %s\n", (long)[error code],
				error != nil ? [[error description] UTF8String] : "(no error)");
		}
		sk_check(disk != nil, "attach");
		if (disk != nil) {
			sk_check([disk isDiskImage] == 1, "attached-is-disk-image");
			sk_check([[disk mountPoint] isEqualToString:mountPoint], "attached-mount-point");
			sk_check([disk isWritable] == 0, "attached-kernel-readonly");
			printf("SK_MOUNT=%s\n", [[disk mountPoint] UTF8String]);
			printf("SK_FSTYPE=%s\n", [[disk filesystemType] UTF8String]);
		}
		sk_check([image isAttached] == 1, "image-attached");

		manager = [SKManager syncSharedManager];
		SKDisk *found = [manager diskForPath:mountPoint];
		sk_check(found != nil, "manager-disk");
		sk_check(found != nil && [found isDiskImage] == 1, "manager-is-disk-image");
		sk_check(found != nil && [[found mountPoint] isEqualToString:mountPoint], "manager-mount-point");

		NSString *knownFile = [mountPoint stringByAppendingPathComponent:@"payload.txt"];
		sk_check(access([knownFile fileSystemRepresentation], R_OK) == 0, "mounted-file-readable");

		/*
		 * Content oracle: the real guest /usr/bin/cmp must accept the reference
		 * copy and reject the one-byte corruption. If cmp cannot run, this is a
		 * failure with its concrete output; there is no byte-compare fallback.
		 */
		int goodRunError = 0;
		int badRunError = 0;
		NSString *goodOutput = nil;
		NSString *badOutput = nil;
		int goodRc = runCmp(knownFile, expectedPath, &goodRunError, &goodOutput);
		int badRc = runCmp(knownFile, corruptPath, &badRunError, &badOutput);
		printf("SK_CMP_GOOD_RC=%d RUN_ERROR=%d\n", goodRc, goodRunError);
		printf("SK_CMP_BAD_RC=%d RUN_ERROR=%d\n", badRc, badRunError);
		if ([goodOutput length] > 0) {
			printf("SK_CMP_GOOD_OUT=%s\n", [goodOutput UTF8String]);
		}
		if ([badOutput length] > 0) {
			printf("SK_CMP_BAD_OUT=%s\n", [badOutput UTF8String]);
		}
		sk_check(goodRunError == 0 && badRunError == 0, "cmp-spawned");
		BOOL loaderFailure = outputIsLoaderFailure(goodOutput) || outputIsLoaderFailure(badOutput);
		if (loaderFailure) {
			printf("SK_CMP_LOADER_ERROR=1\n");
		}
		sk_check(!loaderFailure && goodRc == 0 && badRc == 1,
			"cmp-exact-content-negative-corrupt");

		/*
		 * Busy-detach negative control: keep a real descriptor on the mounted
		 * file. A successful detach here would be a false green; hdiutil cannot
		 * unmount it and the provider must report failure and keep the record.
		 */
		int heldFd = open([knownFile fileSystemRepresentation], O_RDONLY);
		sk_check(heldFd >= 0, "busy-fd-open");
		error = nil;
		BOOL busyDetached = [image detachWithError:&error];
		printf("SK_BUSY_DETACH=%s\n", busyDetached ? "ACCEPTED" : "REJECTED");
		printf("SK_BUSY_ERROR_DOMAIN=%s\n",
			error != nil ? [[error domain] UTF8String] : "(nil)");
		printf("SK_BUSY_ERROR_CODE=%ld\n", error != nil ? (long)[error code] : -1L);
		printf("SK_BUSY_ERROR_DESC=%s\n",
			error != nil ? [[error description] UTF8String] : "(nil)");
		sk_check(!busyDetached && error != nil, "busy-detach-rejected");
		manager = [SKManager syncSharedManager];
		SKDisk *stillAttached = [manager diskForPath:mountPoint];
		sk_check(stillAttached != nil && [stillAttached isDiskImage] == 1,
			"busy-detach-retains-registration");
		sk_check(access([knownFile fileSystemRepresentation], R_OK) == 0,
			"busy-detach-mount-still-readable");
		if (heldFd >= 0) {
			close(heldFd);
		}

		error = nil;
		BOOL detached = [image detachWithError:&error];
		if (!detached) {
			printf("SK_DETACH_ERROR=%ld %s\n", (long)[error code],
				error != nil ? [[error description] UTF8String] : "(no error)");
		}
		sk_check(detached, "detach");
		printf("SK_DETACH=%s\n", detached ? "OK" : "FAIL");

		manager = [SKManager syncSharedManager];
		SKDisk *after = [manager diskForPath:mountPoint];
		sk_check(after == nil, "manager-cleared");
		sk_check(access([mountPoint fileSystemRepresentation], F_OK) != 0, "mountpoint-gone");
		sk_check([image isAttached] == 0, "image-detached");

		/*
		 * Lifetime control: deallocating the image is not a detach. The real
		 * manager registration and mount must remain until an actual detach.
		 */
		NSString *lifetimeMountPoint = [mountPoint stringByAppendingString:@"-lifetime"];
		SKDiskImage *lifetimeImage = [[SKDiskImage alloc] initWithURL:url error:NULL];
		SKDiskImageAttachParams *lifetimeParams = [[SKDiskImageAttachParams alloc] init];
		[[lifetimeParams mountParams] setReadOnly:YES];
		[[lifetimeParams mountParams] setMountPoint:[NSURL fileURLWithPath:lifetimeMountPoint]];
		error = nil;
		SKDisk *lifetimeDisk = [lifetimeImage attachWithParams:lifetimeParams error:&error];
		sk_check(lifetimeDisk != nil, "lifetime-attach");
		[lifetimeImage release];
		manager = [SKManager syncSharedManager];
		SKDisk *lifetimeLive = [manager diskForPath:lifetimeMountPoint];
		sk_check(lifetimeLive != nil && [lifetimeLive isDiskImage] == 1,
			"dealloc-retains-registration");
		NSString *lifetimeFile = [lifetimeMountPoint stringByAppendingPathComponent:@"payload.txt"];
		sk_check(access([lifetimeFile fileSystemRepresentation], R_OK) == 0,
			"dealloc-mount-still-live");

		int lifetimeRunError = 0;
		NSString *lifetimeOutput = nil;
		int lifetimeDetachStatus = runExternalDetach(lifetimeMountPoint, &lifetimeRunError, &lifetimeOutput);
		printf("SK_LIFETIME_EXTERNAL_DETACH=%d RUN_ERROR=%d\n",
			lifetimeDetachStatus, lifetimeRunError);
		if ([lifetimeOutput length] > 0) {
			printf("SK_LIFETIME_EXTERNAL_OUT=%s\n", [lifetimeOutput UTF8String]);
		}
		sk_check(lifetimeRunError == 0 && lifetimeDetachStatus == 0,
			"lifetime-external-detach-command");
		manager = [SKManager syncSharedManager];
		SKDisk *lifetimeAfterExternal = [manager diskForPath:lifetimeMountPoint];
		sk_check(lifetimeAfterExternal == nil, "external-detach-manager-cleared");
		sk_check([manager diskForPath:lifetimeMountPoint] == nil,
			"external-detach-not-returned-twice");
		[lifetimeParams release];

		/*
		 * Cleanup-error control: the unmount is real, but rmdir cannot remove
		 * the directory because its parent is not writable. Detach must still
		 * succeed and clear the manager; the directory is not recursively
		 * deleted and must remain for the owner to inspect/remove.
		 */
		NSString *cleanupParent = [mountParent stringByAppendingPathComponent:@"sk-cleanup-parent"];
		NSString *cleanupMountPoint = [cleanupParent stringByAppendingPathComponent:@"target"];
		sk_remove(cleanupMountPoint);
		sk_remove(cleanupParent);
		sk_check([[NSFileManager defaultManager] createDirectoryAtPath:cleanupParent
			withIntermediateDirectories:YES attributes:nil error:NULL], "cleanup-parent-mkdir");
		sk_check(mkdir([cleanupMountPoint fileSystemRepresentation], 0755) == 0,
			"cleanup-mountdir-mkdir");
		chmod([cleanupParent fileSystemRepresentation], 0755);

		SKDiskImage *cleanupImage = [[SKDiskImage alloc] initWithURL:url error:NULL];
		SKDiskImageAttachParams *cleanupParams = [[SKDiskImageAttachParams alloc] init];
		[[cleanupParams mountParams] setReadOnly:YES];
		[[cleanupParams mountParams] setMountPoint:[NSURL fileURLWithPath:cleanupMountPoint]];
		error = nil;
		SKDisk *cleanupDisk = [cleanupImage attachWithParams:cleanupParams error:&error];
		sk_check(cleanupDisk != nil, "cleanup-error-attach");
		sk_check(chmod([cleanupParent fileSystemRepresentation], 0555) == 0,
			"cleanup-parent-readonly");

		error = nil;
		BOOL cleanupDetached = [cleanupImage detachWithError:&error];
		printf("SK_CLEANUP_DETACH=%s\n", cleanupDetached ? "OK" : "FAIL");
		sk_check(cleanupDetached, "cleanup-error-detach-success");
		manager = [SKManager syncSharedManager];
		SKDisk *cleanupAfter = [manager diskForPath:cleanupMountPoint];
		sk_check(cleanupAfter == nil, "cleanup-error-manager-cleared");
		sk_check(access([cleanupMountPoint fileSystemRepresentation], F_OK) == 0,
			"cleanup-error-mountdir-retained");

		chmod([cleanupParent fileSystemRepresentation], 0755);
		rmdir([cleanupMountPoint fileSystemRepresentation]);
		rmdir([cleanupParent fileSystemRepresentation]);
		[cleanupParams release];
		[cleanupImage release];

		/*
		 * Mount-identity control: A is <parent>/<leaf>-identity-a and B is a
		 * peer-prefixed copy of A's guest path, so A's guest path is a suffix of
		 * B's host path. A suffix guess would mistake B for A and retain A after
		 * an external detach.
		 */
		NSString *identityA = [mountParent stringByAppendingPathComponent:
			[mountLeaf stringByAppendingString:@"-identity-a"]];
		NSString *identityPeer = [mountParent stringByAppendingPathComponent:@"sk-identity-peer"];
		NSString *identityB = [identityPeer stringByAppendingString:identityA];
		sk_remove(identityB);
		sk_remove(identityPeer);
		sk_remove(identityA);
		sk_check([[NSFileManager defaultManager] createDirectoryAtPath:[identityB stringByDeletingLastPathComponent]
			withIntermediateDirectories:YES attributes:nil error:NULL], "identity-peer-mkdir");

		SKDiskImageAttachParams *identityAParams = [[SKDiskImageAttachParams alloc] init];
		[[identityAParams mountParams] setReadOnly:YES];
		[[identityAParams mountParams] setMountPoint:[NSURL fileURLWithPath:identityA]];
		SKDiskImage *identityImageA = [[SKDiskImage alloc] initWithURL:url error:NULL];
		error = nil;
		id identityDiskA = [identityImageA attachWithParams:identityAParams error:&error];
		sk_check(identityDiskA != nil, "identity-a-attach");

		SKDiskImageAttachParams *identityBParams = [[SKDiskImageAttachParams alloc] init];
		[[identityBParams mountParams] setReadOnly:YES];
		[[identityBParams mountParams] setMountPoint:[NSURL fileURLWithPath:identityB]];
		SKDiskImage *identityImageB = [[SKDiskImage alloc] initWithURL:url error:NULL];
		error = nil;
		id identityDiskB = [identityImageB attachWithParams:identityBParams error:&error];
		sk_check(identityDiskB != nil, "identity-b-attach");

		int identityRunError = 0;
		NSString *identityOutput = nil;
		int identityDetachStatus = runExternalDetach(identityA, &identityRunError, &identityOutput);
		printf("SK_IDENTITY_EXTERNAL_DETACH=%d RUN_ERROR=%d\n",
			identityDetachStatus, identityRunError);
		if ([identityOutput length] > 0) {
			printf("SK_IDENTITY_EXTERNAL_OUT=%s\n", [identityOutput UTF8String]);
		}
		sk_check(identityRunError == 0 && identityDetachStatus == 0,
			"identity-a-external-detach");
		manager = [SKManager syncSharedManager];
		SKDisk *identityAAfter = [manager diskForPath:identityA];
		SKDisk *identityBAfter = [manager diskForPath:identityB];
		sk_check(identityAAfter == nil, "identity-a-not-retained");
		sk_check(identityBAfter != nil && [identityBAfter isDiskImage] == 1,
			"identity-b-still-live");
		NSString *identityBFile = [identityB stringByAppendingPathComponent:@"payload.txt"];
		sk_check(access([identityBFile fileSystemRepresentation], R_OK) == 0,
			"identity-b-still-readable");

		error = nil;
		sk_check([identityImageB detachWithError:&error], "identity-b-cleanup-detach");
		[identityBParams release];
		[identityImageB release];
		[identityAParams release];
		[identityImageA release];
		sk_remove(identityB);
		sk_remove(identityPeer);
		sk_remove(identityA);

		/*
		 * Actual read-only regression: request the default (readOnly NO). The
		 * backend mounts every image read-only, so the provider's disk must
		 * report the mounted state from the kernel record, and a real write on
		 * the live mount must be rejected by the kernel with EROFS.
		 */
		NSString *readOnlyMountPoint = [mountPoint stringByAppendingString:@"-readonly"];
		SKDiskImage *readOnlyImage = [[SKDiskImage alloc] initWithURL:url error:NULL];
		SKDiskImageAttachParams *readOnlyParams = [[SKDiskImageAttachParams alloc] init];
		[[readOnlyParams mountParams] setMountPoint:[NSURL fileURLWithPath:readOnlyMountPoint]];
		sk_check([readOnlyParams readOnly] == false, "readonly-params-default-writable");
		error = nil;
		SKDisk *readOnlyDisk = [readOnlyImage attachWithParams:readOnlyParams error:&error];
		sk_check(readOnlyDisk != nil, "readonly-attach");
		sk_check(readOnlyDisk != nil && [readOnlyDisk isWritable] == 0,
			"readonly-mounted-writable-false");
		NSString *readOnlyProbe = [readOnlyMountPoint stringByAppendingPathComponent:
			@"storagekit-readonly-probe"];
		errno = 0;
		int probeFd = open([readOnlyProbe fileSystemRepresentation],
			O_WRONLY | O_CREAT | O_EXCL, 0600);
		int probeError = errno;
		printf("SK_READONLY_PROBE_FD=%d ERRNO=%d\n", probeFd, probeError);
		sk_check(probeFd < 0 && probeError == EROFS, "readonly-write-rejected-EROFS");
		if (probeFd >= 0) {
			close(probeFd);
			unlink([readOnlyProbe fileSystemRepresentation]);
		}
		error = nil;
		sk_check([readOnlyImage detachWithError:&error], "readonly-detach");
		manager = [SKManager syncSharedManager];
		sk_check([manager diskForPath:readOnlyMountPoint] == nil, "readonly-manager-cleared");
		[readOnlyParams release];
		[readOnlyImage release];

		/*
		 * External detach object query and real reattach: after an external
		 * detach, the image object must not claim the old mount or return the
		 * stale disk, and attaching again must perform a real new mount.
		 */
		NSString *externalMountPoint = [mountPoint stringByAppendingString:@"-external"];
		SKDiskImageAttachParams *externalParams = [[SKDiskImageAttachParams alloc] init];
		[[externalParams mountParams] setReadOnly:YES];
		[[externalParams mountParams] setMountPoint:[NSURL fileURLWithPath:externalMountPoint]];
		SKDiskImage *externalImage = [[SKDiskImage alloc] initWithURL:url error:NULL];
		error = nil;
		SKDisk *externalDisk = [externalImage attachWithParams:externalParams error:&error];
		sk_check(externalDisk != nil, "external-attach");
		sk_check([externalImage isAttached] == 1, "external-initially-attached");
		NSString *externalFile = [externalMountPoint stringByAppendingPathComponent:@"payload.txt"];
		sk_check(access([externalFile fileSystemRepresentation], R_OK) == 0,
			"external-initially-readable");

		int externalRunError = 0;
		NSString *externalOutput = nil;
		int externalDetachStatus = runExternalDetach(externalMountPoint, &externalRunError, &externalOutput);
		printf("SK_EXTERNAL_DETACH=%d RUN_ERROR=%d\n", externalDetachStatus, externalRunError);
		if ([externalOutput length] > 0) {
			printf("SK_EXTERNAL_OUT=%s\n", [externalOutput UTF8String]);
		}
		sk_check(externalRunError == 0 && externalDetachStatus == 0,
			"external-detach-command");
		sk_check([externalImage isAttached] == 0, "external-detach-object-query");
		sk_check([externalImage mountPoint] == nil, "external-detach-object-mountpoint-cleared");
		manager = [SKManager syncSharedManager];
		sk_check([manager diskForPath:externalMountPoint] == nil,
			"external-detach-manager-cleared");

		error = nil;
		SKDisk *reattachedDisk = [externalImage attachWithParams:externalParams error:&error];
		if (reattachedDisk == nil) {
			printf("SK_REATTACH_ERROR=%ld %s\n", (long)[error code],
				error != nil ? [[error description] UTF8String] : "(no error)");
		}
		sk_check(reattachedDisk != nil, "reattach");
		sk_check([externalImage isAttached] == 1, "reattach-object-attached");
		sk_check(access([externalFile fileSystemRepresentation], R_OK) == 0,
			"reattach-readable");
		error = nil;
		sk_check([externalImage detachWithError:&error], "reattach-detach");
		manager = [SKManager syncSharedManager];
		sk_check([manager diskForPath:externalMountPoint] == nil,
			"reattach-detach-manager-cleared");
		[externalParams release];
		[externalImage release];

		/* Argument and input errors must be real errors, not attached state. */
		error = nil;
		id nilResult = [image attachWithParams:nil error:&error];
		sk_check(nilResult == nil && [error code] == EINVAL, "nil-params-einval");

		NSURL *missingURL = [NSURL fileURLWithPath:[imagePath stringByAppendingString:@".missing"]];
		SKDiskImage *missingImage = [[SKDiskImage alloc] initWithURL:missingURL error:NULL];
		SKDiskImageAttachParams *missingParams = [[SKDiskImageAttachParams alloc] init];
		[[missingParams mountParams] setMountPoint:
			[NSURL fileURLWithPath:[mountPoint stringByAppendingString:@"-missing"]]];
		error = nil;
		id missingResult = [missingImage attachWithParams:missingParams error:&error];
		sk_check(missingResult == nil && [error code] == ENOENT, "missing-image-enoent");
		[missingParams release];
		[missingImage release];

		[params release];
		[image release];
	}
	printf("SK_FAILURES=%d\n", failures);
	if (failures == 0) {
		printf("SK_SMOKE=OK\n");
	}
	return failures == 0 ? 0 : 1;
}
