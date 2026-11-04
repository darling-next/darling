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

static void SKStoreError(NSError **slot, NSError *error)
{
	if (slot != NULL) {
		*slot = error;
	}
}

static NSString *SKPathFromMountURL(NSURL *url)
{
	if (url == nil) {
		return nil;
	}
	if (![url isKindOfClass:[NSURL class]]) {
		return nil;
	}
	NSString *path = [url path];
	if (path == nil || [path length] == 0) {
		return nil;
	}
	return [path stringByStandardizingPath];
}

static NSString *SKTrimmedLine(NSString *line)
{
	return [line stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

static NSString *SKParseMountPointFromOutput(NSString *output)
{
	NSString *result = nil;
	for (NSString *rawLine in [output componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]]) {
		NSString *line = SKTrimmedLine(rawLine);
		if ([line length] == 0 || ![line hasPrefix:@"/"]) {
			continue;
		}
		if ([line hasPrefix:@"/dev/"]) {
			continue;
		}
		result = line;
	}
	return result;
}

/*
 * Run hdiutil in the guest. stdout and stderr go to one private temporary file: the
 * provider must not inherit a caller's pipe (a short-lived child writing a pipe whose
 * reader is not draining can convert a mount failure into an apparent hang).
 *
 * A failed spawn or a waitpid error that is not EINTR is reported as an error. It is
 * never converted into the child's exit status 0.
 */
static int SKRunHdiutil(NSArray *arguments, NSString **output, NSError **error)
{
	char temporary[] = "/tmp/storagekit-hdiutil-XXXXXX";
	int fd = mkstemp(temporary);
	if (fd < 0) {
		SKStoreError(error, SKPOSIXError(errno));
		return -1;
	}
	close(fd);

	NSUInteger argumentCount = [arguments count];
	char **argv = calloc(argumentCount + 2, sizeof(char *));
	if (argv == NULL) {
		unlink(temporary);
		SKStoreError(error, SKPOSIXError(ENOMEM));
		return -1;
	}
	argv[0] = (char *)"/usr/bin/hdiutil";
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
		spawnError = posix_spawn(&pid, "/usr/bin/hdiutil", &actions, NULL, argv, environ);
	}
	if (actionsInitialized) {
		posix_spawn_file_actions_destroy(&actions);
	}
	free(argv);

	if (actionError != 0 || spawnError != 0) {
		int code = actionError != 0 ? actionError : spawnError;
		unlink(temporary);
		SKStoreError(error, SKPOSIXError(code));
		return -1;
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
		int waitError = errno;
		unlink(temporary);
		SKStoreError(error, SKPOSIXError(waitError));
		return -1;
	}

	int status;
	if (WIFEXITED(waitStatus)) {
		status = WEXITSTATUS(waitStatus);
	} else if (WIFSIGNALED(waitStatus)) {
		status = 128 + WTERMSIG(waitStatus);
	} else {
		status = 128;
	}

	NSString *collected = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:temporary]
		encoding:NSUTF8StringEncoding
		error:NULL];
	unlink(temporary);
	if (output != NULL) {
		*output = collected != nil ? collected : @"";
	}
	return status;
}

static NSError *SKHdiutilFailure(NSString *action, int status, NSString *output)
{
	NSString *detail = output != nil ? SKTrimmedLine(output) : @"";
	NSString *message = [NSString stringWithFormat:@"hdiutil %@ failed with status %d%@%@",
		action, status, [detail length] > 0 ? @": " : @"", detail];
	int code = EIO;
	if ([detail rangeOfString:@"No such file" options:NSCaseInsensitiveSearch].location != NSNotFound) {
		code = ENOENT;
	}
	return SKErrorWithMessage(code, message);
}

/*
 * Wait for the exact kernel mount instance of a guest mount point. A guest statfs
 * is not proof of a new mount: hdiutil creates the mount directory before mounting,
 * and an ordinary directory also answers statfs. The host kernel mount table is the
 * authority, and the guest path is resolved through the vchroot prefix before the
 * exact mount point comparison, so no prefix/suffix guess can match a sibling.
 */
static SKMountRecord *SKWaitForMountRecord(NSString *guestMountPoint,
	NSTimeInterval timeout,
	SKMountLookupStatus *finalStatus)
{
	SKMountLookupStatus status = SKMountLookupUnknown;
	NSUInteger attempts = (NSUInteger)(timeout / 0.05);
	if (attempts < 1) {
		attempts = 1;
	}
	for (NSUInteger i = 0; i < attempts; i++) {
		SKMountRecord *record = SKLookupGuestMountRecord(guestMountPoint, &status);
		if (status == SKMountLookupPresent && record != nil) {
			if (finalStatus != NULL) {
				*finalStatus = status;
			}
			return record;
		}
		usleep(50000);
	}
	if (finalStatus != NULL) {
		*finalStatus = status;
	}
	return nil;
}

@interface SKDiskImage ()
- (SKMountLookupStatus)refreshMountState;
- (void)forgetMountState;
@end

@implementation SKDiskImage

- (instancetype)initWithURL:(NSURL *)url error:(NSError **)error
{
	return [self initWithURL:url diParams:nil shadowURLs:nil error:error];
}

- (instancetype)initWithURL:(NSURL *)url diParams:(id)diParams error:(NSError **)error
{
	return [self initWithURL:url diParams:diParams shadowURLs:nil error:error];
}

- (instancetype)initWithURL:(NSURL *)url shadowURLs:(NSArray *)shadowURLs error:(NSError **)error
{
	return [self initWithURL:url diParams:nil shadowURLs:shadowURLs error:error];
}

- (instancetype)initWithURL:(NSURL *)url
	diParams:(id)diParams
	shadowURLs:(NSArray *)shadowURLs
	error:(NSError **)error
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_imageURL = [url retain];
	_shadowURLs = [shadowURLs retain];
	_diParams = [diParams retain];
	_deviceHandle = nil;
	_mountPoint = nil;
	_mountedDisk = nil;
	_mountRecord = nil;
	SKStoreError(error, nil);
	return self;
}

- (void)dealloc
{
	/*
	 * Deallocation is not a detach. If the image still has a live mount, the
	 * manager's real registration must outlive this object until an actual
	 * successful detach removes it.
	 */
	if (_mountedDisk != nil) {
		SKLog("image deallocated with live mount %s; registration retained",
			[_mountPoint UTF8String]);
	}
	[_imageURL release];
	[_shadowURLs release];
	[_diParams release];
	[_deviceHandle release];
	[_mountPoint release];
	[_mountedDisk release];
	[_mountRecord release];
	[super dealloc];
}

- (NSURL *)imageURL
{
	return _imageURL;
}

- (NSArray *)shadowURLs
{
	return _shadowURLs;
}

- (id)diParams
{
	return _diParams;
}

- (void)setDiParams:(id)diParams
{
	if (_diParams == diParams) {
		return;
	}
	[diParams retain];
	[_diParams release];
	_diParams = diParams;
}

- (id)deviceHandle
{
	return _deviceHandle;
}

- (void)setDeviceHandle:(id)deviceHandle
{
	if (_deviceHandle == deviceHandle) {
		return;
	}
	[deviceHandle retain];
	[_deviceHandle release];
	_deviceHandle = deviceHandle;
}

/*
 * Compare the recorded mount instance against the current kernel mount table.
 *
 * Present means the same mount ID/device/filesystem is still mounted at the same
 * host path. Absent means the table was read and our instance is gone (external
 * detach or replacement); the stale local registration is dropped. Unknown means
 * the table could not be read: local state is kept so a possibly live mount is
 * never silently disowned.
 */
- (SKMountLookupStatus)refreshMountState
{
	if (_mountRecord == nil || _mountPoint == nil) {
		return _mountedDisk != nil ? SKMountLookupUnknown : SKMountLookupAbsent;
	}
	SKMountLookupStatus status = SKMountLookupUnknown;
	SKMountRecord *current = SKLookupMountRecord([_mountRecord hostMountPoint], &status);
	if (status == SKMountLookupPresent && SKMountRecordIsSameMount(_mountRecord, current)) {
		return SKMountLookupPresent;
	}
	if (status == SKMountLookupUnknown) {
		return SKMountLookupUnknown;
	}
	SKLog("image mount %s is no longer the recorded kernel mount instance; dropping stale registration",
		[_mountPoint UTF8String]);
	[self forgetMountState];
	return SKMountLookupAbsent;
}

- (void)forgetMountState
{
	if (_mountedDisk != nil) {
		[[SKManager sharedManager] removeAttachedDisk:_mountedDisk];
	}
	[_mountedDisk release];
	_mountedDisk = nil;
	[_mountPoint release];
	_mountPoint = nil;
	[_mountRecord release];
	_mountRecord = nil;
}

- (char)isAttached
{
	if (_mountedDisk == nil) {
		return 0;
	}
	return [self refreshMountState] == SKMountLookupPresent ? 1 : 0;
}

- (NSString *)mountPoint
{
	return _mountPoint;
}

- (id)attachWithParams:(SKDiskImageAttachParams *)params error:(NSError **)error
{
	SKLog("attachWithParams: url=%s params=%s",
		_imageURL != nil ? [[_imageURL path] UTF8String] : "(nil)",
		params != nil ? "present" : "nil");

	SKStoreError(error, nil);
	if (params == nil) {
		SKStoreError(error, SKPOSIXError(EINVAL));
		return nil;
	}
	if (_imageURL == nil) {
		SKStoreError(error, SKPOSIXError(EINVAL));
		return nil;
	}

	NSString *imagePath = [_imageURL path];
	if (imagePath == nil || ![[NSFileManager defaultManager] fileExistsAtPath:imagePath]) {
		SKStoreError(error, SKPOSIXError(ENOENT));
		return nil;
	}

	if (_mountedDisk != nil) {
		SKMountLookupStatus current = [self refreshMountState];
		if (current == SKMountLookupPresent) {
			return _mountedDisk;
		}
		if (current == SKMountLookupUnknown) {
			SKStoreError(error, SKErrorWithMessage(EIO,
				@"cannot read the kernel mount table; refusing to attach over a possibly live mount"));
			return nil;
		}
		/* Externally detached or replaced: the stale state is gone; attach for real. */
	}

	SKMountParams *mountParams = [params mountParams];
	NSURL *mountURL = mountParams != nil ? [mountParams mountPoint] : nil;
	NSString *mountPoint = SKPathFromMountURL(mountURL);
	if (mountURL != nil && mountPoint == nil) {
		SKStoreError(error, SKPOSIXError(EINVAL));
		return nil;
	}
	bool readOnly = [params readOnly] || (mountParams != nil && [mountParams readOnly]);

	NSMutableArray *arguments = [NSMutableArray arrayWithObject:@"attach"];
	if (mountPoint != nil) {
		[arguments addObject:@"-mountpoint"];
		[arguments addObject:mountPoint];
	}
	if (readOnly) {
		[arguments addObject:@"-readonly"];
	}
	[arguments addObject:imagePath];

	SKLog("hdiutil attach image=%s mount=%s readonly=%d", [imagePath UTF8String],
		mountPoint != nil ? [mountPoint UTF8String] : "(default)", readOnly ? 1 : 0);

	NSString *output = nil;
	int status = SKRunHdiutil(arguments, &output, error);
	SKLog("hdiutil attach status=%d output=%s", status,
		output != nil ? [SKTrimmedLine(output) UTF8String] : "(nil)");
	if (status < 0) {
		return nil;
	}
	if (status != 0) {
		SKStoreError(error, SKHdiutilFailure(@"attach", status, output));
		return nil;
	}

	if (mountPoint == nil) {
		NSString *parsed = SKParseMountPointFromOutput(output);
		if ([parsed length] > 0) {
			mountPoint = [parsed stringByStandardizingPath];
		}
	}
	if (mountPoint == nil) {
		SKStoreError(error, SKErrorWithMessage(EIO,
			[NSString stringWithFormat:@"hdiutil attach returned no mount point: %@", output]));
		return nil;
	}

	SKMountLookupStatus mountStatus = SKMountLookupUnknown;
	SKMountRecord *record = SKWaitForMountRecord(mountPoint, 5.0, &mountStatus);
	if (record == nil) {
		/* Do not leave a half-created mount behind, but do not claim success either. */
		NSMutableArray *detachArguments = [NSMutableArray arrayWithObjects:@"detach", mountPoint, nil];
		NSString *detachOutput = nil;
		(void)SKRunHdiutil(detachArguments, &detachOutput, NULL);
		if (mountStatus == SKMountLookupUnknown) {
			SKStoreError(error, SKErrorWithMessage(EIO,
				[NSString stringWithFormat:@"cannot read the kernel mount table to confirm %@", mountPoint]));
		} else {
			SKStoreError(error, SKErrorWithMessage(EIO,
				[NSString stringWithFormat:@"hdiutil reported success but %@ is not mounted", mountPoint]));
		}
		return nil;
	}

	/*
	 * The writability the consumer sees is the mounted state from the same kernel
	 * record that proves the mount, not the caller's requested parameter.
	 */
	SKDisk *disk = [[SKDisk alloc] initWithMountPoint:mountPoint
		filesystemType:[record filesystemType]
		readOnly:[record readOnly]];
	[disk setKernelMountRecord:record];
	[[SKManager sharedManager] addAttachedDisk:disk];
	_mountPoint = [mountPoint copy];
	_mountRecord = [record retain];
	_mountedDisk = [disk retain];
	SKLog("attach complete: mount=%s disk=%s", [mountPoint UTF8String], [[disk description] UTF8String]);
	return [disk autorelease];
}

- (id)attachWithError:(NSError **)error
{
	SKDiskImageAttachParams *params = [[SKDiskImageAttachParams alloc] init];
	id result = [self attachWithParams:params error:error];
	[params release];
	return result;
}

- (id)attachNoMountWithError:(NSError **)error
{
	/*
	 * The backend's only attach primitive mounts. It has no no-mount operation, so
	 * this entry point fails closed instead of mounting unexpectedly or returning a
	 * handle that nothing backs.
	 */
	SKStoreError(error, SKErrorWithMessage(ENOTSUP,
		@"attachNoMount is not supported: this provider's hdiutil attach always mounts"));
	return nil;
}

- (id)diAttachWithParams:(SKDiskImageAttachParams *)params error:(NSError **)error
{
	/*
	 * The measured native contract performs diAttach and mount; the consumer calls
	 * attachWithParams:. Keep this entry point real (mounted) rather than returning
	 * a handle that no mounted disk backs.
	 */
	return [self attachWithParams:params error:error];
}

- (char)mount:(NSURL *)mountPoint params:(SKMountParams *)params outError:(NSError **)error
{
	if (_mountedDisk != nil) {
		SKMountLookupStatus current = [self refreshMountState];
		if (current == SKMountLookupPresent) {
			return 1;
		}
		if (current == SKMountLookupUnknown) {
			SKStoreError(error, SKErrorWithMessage(EIO,
				@"cannot read the kernel mount table; refusing to claim the image is mounted"));
			return 0;
		}
	}
	SKDiskImageAttachParams *attachParams = [[SKDiskImageAttachParams alloc] init];
	if (params != nil) {
		[attachParams setMountParams:params];
	}
	if (mountPoint != nil) {
		[[attachParams mountParams] setMountPoint:mountPoint];
	}
	id disk = [self attachWithParams:attachParams error:error];
	[attachParams release];
	return disk != nil ? 1 : 0;
}

- (char)detachWithError:(NSError **)error
{
	SKStoreError(error, nil);
	if (_mountedDisk == nil) {
		return 1;
	}

	/*
	 * Verify that the path still carries our exact mount instance before asking
	 * hdiutil to detach by path. If it was externally detached (or replaced by an
	 * unrelated mount), there is nothing of ours to detach and the stale record is
	 * dropped without touching the replacement.
	 */
	SKMountLookupStatus before = [self refreshMountState];
	if (before == SKMountLookupUnknown) {
		SKStoreError(error, SKErrorWithMessage(EIO,
			@"cannot read the kernel mount table; refusing to claim a detach"));
		return 0;
	}
	if (before == SKMountLookupAbsent) {
		return 1;
	}

	NSArray *arguments = [NSArray arrayWithObjects:@"detach", _mountPoint, nil];
	NSString *output = nil;
	int status = SKRunHdiutil(arguments, &output, error);
	if (status < 0) {
		return 0;
	}
	if (status != 0) {
		SKStoreError(error, SKHdiutilFailure(@"detach", status, output));
		return 0;
	}

	SKMountLookupStatus after = SKMountLookupUnknown;
	SKMountRecord *current = SKLookupMountRecord([_mountRecord hostMountPoint], &after);
	if (after == SKMountLookupUnknown) {
		SKStoreError(error, SKErrorWithMessage(EIO,
			[NSString stringWithFormat:@"cannot read the kernel mount table to confirm that %@ was unmounted",
				_mountPoint]));
		return 0;
	}
	if (after == SKMountLookupPresent && SKMountRecordIsSameMount(_mountRecord, current)) {
		SKStoreError(error, SKErrorWithMessage(EBUSY,
			[NSString stringWithFormat:@"hdiutil detach returned success but %@ is still mounted",
				_mountPoint]));
		return 0;
	}

	[self forgetMountState];
	return 1;
}

- (char)ejectWithError:(NSError **)error
{
	return [self detachWithError:error];
}

- (NSString *)description
{
	/*
	 * isAttached refreshes state and may release the mount point, so hold the
	 * description arguments stable before asking.
	 */
	NSString *mountPoint = [[_mountPoint retain] autorelease];
	return [NSString stringWithFormat:@"<SKDiskImage url=%@ attached=%d mount=%@>",
		_imageURL, (int)[self isAttached], mountPoint];
}

@end
