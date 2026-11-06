#import "DIInternal.h"

#import <DiskArbitration/DiskArbitration.h>
#import <CoreFoundation/CoreFoundation.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Real DiskArbitration mount of an image device. The producer performs the
 * mount (through its real hdiutil/FUSE path) and reports the kernel mount
 * instance; success here additionally requires the mount table to show a
 * distinct mounted file system whose root directory is readable.
 */

static NSString *DISanitizedMountName(NSString *name)
{
	if (name == nil || [name length] == 0) {
		return @"Untitled";
	}
	NSMutableString *result = [NSMutableString stringWithCapacity:[name length]];
	NSCharacterSet *unsafe = [NSCharacterSet characterSetWithCharactersInString:@"/\\:"];
	for (NSUInteger index = 0; index < [name length] && [result length] < 128; index++) {
		unichar character = [name characterAtIndex:index];
		[result appendFormat:@"%C", [unsafe characterIsMember:character] ? '_' : character];
	}
	NSString *trimmed = [result stringByTrimmingCharactersInSet:
		[NSCharacterSet whitespaceAndNewlineCharacterSet]];
	return [trimmed length] != 0 ? trimmed : @"Untitled";
}

static NSString *DIMountUnescape(NSString *field)
{
	if ([field rangeOfString:@"\\"].location == NSNotFound) {
		return field;
	}
	NSMutableString *result = [NSMutableString stringWithCapacity:[field length]];
	NSUInteger length = [field length];
	for (NSUInteger index = 0; index < length; index++) {
		unichar character = [field characterAtIndex:index];
		if (character == '\\' && index + 3 < length) {
			unichar a = [field characterAtIndex:index + 1];
			unichar b = [field characterAtIndex:index + 2];
			unichar c = [field characterAtIndex:index + 3];
			if (a >= '0' && a <= '7' && b >= '0' && b <= '7' && c >= '0' && c <= '7') {
				[result appendFormat:@"%C", (unichar)((a - '0') * 64 + (b - '0') * 8 + (c - '0'))];
				index += 3;
				continue;
			}
		}
		[result appendFormat:@"%C", character];
	}
	return result;
}

/*
 * The host kernel mount table is the authority for a guest mount point. A
 * guest statfs is not proof: hdiutil creates the mount directory before
 * mounting, and an ordinary directory also answers statfs. The guest path is
 * resolved through the vchroot prefix and matched exactly, the same check the
 * in-guest hdiutil producer uses.
 */
BOOL DIMountPointIsReal(NSString *path)
{
	if (path == nil || [path length] == 0) {
		return NO;
	}
	char expanded[DA_IMAGE_PATH];
	if (__darling_vchroot_expand([path fileSystemRepresentation], expanded) != 0) {
		return NO;
	}
	NSString *hostPath = [NSString stringWithUTF8String:expanded];
	if (hostPath == nil || [hostPath length] == 0) {
		return NO;
	}
	NSString *mountTable = [NSString stringWithContentsOfFile:
		@"/Volumes/SystemRoot/proc/self/mountinfo"
		encoding:NSUTF8StringEncoding
		error:NULL];
	if (mountTable == nil) {
		return NO;
	}
	BOOL found = NO;
	for (NSString *line in [mountTable componentsSeparatedByCharactersInSet:
		[NSCharacterSet newlineCharacterSet]]) {
		NSArray *rawFields = [line componentsSeparatedByCharactersInSet:
			[NSCharacterSet whitespaceCharacterSet]];
		NSMutableArray *fields = [NSMutableArray arrayWithCapacity:[rawFields count]];
		for (NSString *field in rawFields) {
			if ([field length] != 0) {
				[fields addObject:field];
			}
		}
		if ([fields count] < 10) {
			continue;
		}
		NSString *mountPoint = DIMountUnescape([fields objectAtIndex:4]);
		if ([mountPoint isEqualToString:hostPath]) {
			found = YES;
			break;
		}
	}
	if (!found) {
		return NO;
	}
	struct statfs filesystem;
	if (statfs([path fileSystemRepresentation], &filesystem) != 0) {
		return NO;
	}
	/* The mounted volume must actually serve a root directory. */
	DIR *directory = opendir([path fileSystemRepresentation]);
	if (directory == NULL) {
		return NO;
	}
	errno = 0;
	BOOL readable = readdir(directory) != NULL || errno == 0;
	return closedir(directory) == 0 && readable;
}

typedef struct {
	BOOL done;
	DAReturn status;
	CFStringRef message;
} DIOperationContext;

static void DIOperationCallback(DADiskRef disk, DADissenterRef dissenter, void *context)
{
	(void)disk;
	DIOperationContext *operation = context;
	if (dissenter != NULL) {
		operation->status = DADissenterGetStatus(dissenter);
		CFStringRef message = DADissenterGetStatusString(dissenter);
		if (message != NULL) {
			operation->message = CFRetain(message);
		}
	} else {
		operation->status = kDAReturnSuccess;
	}
	operation->done = YES;
}

static void DIWaitForOperation(DIOperationContext *operation, NSTimeInterval timeout)
{
	NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
	while (!operation->done && [deadline timeIntervalSinceNow] > 0.0) {
		NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
		[pool release];
	}
}

static BOOL DIMountBSDName(NSString *BSDName,
	NSString *suggestedMountPoint,
	NSString **mountPoint,
	NSError **error)
{
	if (mountPoint != NULL) {
		*mountPoint = nil;
	}
	if (BSDName == nil || [BSDName length] == 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeMissingBSDName, @"The image device has no BSD name to mount");
		}
		return NO;
	}

	NSString *name = [suggestedMountPoint lastPathComponent];
	NSString *candidate = nil;
	for (int attempt = 0; attempt < 10; attempt++) {
		NSString *suffix = attempt == 0 ? DISanitizedMountName(name) :
			[NSString stringWithFormat:@"%@-%d", DISanitizedMountName(name), attempt];
		NSString *path = [@"/Volumes" stringByAppendingPathComponent:suffix];
		if (!DIMountPointIsReal(path)) {
			candidate = path;
			break;
		}
	}
	if (candidate == nil) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"No unused mount point is available for image device %@", BSDName);
		}
		return NO;
	}

	DASessionRef session = DASessionCreate(NULL);
	if (session == NULL) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot create a DiskArbitration session");
		}
		return NO;
	}
	CFRunLoopRef loop = CFRunLoopGetCurrent();
	DASessionScheduleWithRunLoop(session, loop, kCFRunLoopDefaultMode);

	DADiskRef disk = DADiskCreateFromBSDName(NULL, session, [BSDName UTF8String]);
	if (disk == NULL) {
		DASessionUnscheduleFromRunLoop(session, loop, kCFRunLoopDefaultMode);
		CFRelease(session);
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot create a DiskArbitration disk for image device %@", BSDName);
		}
		return NO;
	}

	const char *mountBytes = [candidate fileSystemRepresentation];
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,
		(const UInt8 *)mountBytes, (CFIndex)strlen(mountBytes), true);
	if (url == NULL) {
		CFRelease(disk);
		DASessionUnscheduleFromRunLoop(session, loop, kCFRunLoopDefaultMode);
		CFRelease(session);
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot create the mount URL for %@", candidate);
		}
		return NO;
	}

	DIOperationContext operation;
	memset(&operation, 0, sizeof(operation));
	DADiskMount(disk, url, kDADiskMountOptionDefault, DIOperationCallback, &operation);
	DIWaitForOperation(&operation, 120.0);

	BOOL mounted = NO;
	NSString *failure = nil;
	if (!operation.done) {
		failure = [NSString stringWithFormat:
			@"Mounting image device %@ did not complete within 120 seconds", BSDName];
	} else if (operation.status != kDAReturnSuccess) {
		if (operation.message != NULL) {
			failure = [NSString stringWithFormat:@"Mounting image device %@ failed: %@",
				BSDName, (NSString *)operation.message];
		} else {
			failure = [NSString stringWithFormat:@"Mounting image device %@ failed (status 0x%x)",
				BSDName, (unsigned)operation.status];
		}
	} else if (!DIMountPointIsReal(candidate)) {
		failure = [NSString stringWithFormat:
			@"Mounting image device %@ reported success but %@ is not a mounted volume",
			BSDName, candidate];
	} else {
		mounted = YES;
	}

	if (operation.message != NULL) {
		CFRelease(operation.message);
	}
	CFRelease(url);
	CFRelease(disk);
	DASessionUnscheduleFromRunLoop(session, loop, kCFRunLoopDefaultMode);
	CFRelease(session);

	if (!mounted) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"%@", failure);
		}
		return NO;
	}
	if (mountPoint != NULL) {
		*mountPoint = candidate;
	}
	return YES;
}

static NSString *DISuggestedMountPoint(DIDeviceHandle *handle)
{
	DIProducerState *state = nil;
	NSString *identity = [handle di_producerIdentity];
	if (identity != nil) {
		state = DIProducerStateByIdentity(identity);
	}
	if (state == nil) {
		state = DIProducerStateByBSDName([handle BSDName]);
	}
	NSString *name = [state volumeName];
	if (name == nil || [name length] == 0) {
		NSURL *imageURL = [handle di_imageURL];
		if (imageURL != nil && [[imageURL path] length] > 0) {
			name = [[imageURL path] lastPathComponent];
			NSString *extension = [name pathExtension];
			if ([extension length] != 0) {
				name = [name stringByDeletingPathExtension];
			}
		}
	}
	return name;
}

BOOL DIMountDeviceHandle(DIDeviceHandle *handle,
	NSString *suggestedMountPoint,
	NSError **error)
{
	if (handle == nil) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"No device handle was supplied");
		}
		return NO;
	}
	NSString *BSDName = [handle BSDName];
	if (BSDName == nil || [BSDName length] == 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeMissingBSDName, @"The attached image device has no BSD name");
		}
		return NO;
	}
	if (suggestedMountPoint == nil) {
		suggestedMountPoint = DISuggestedMountPoint(handle);
	}
	NSString *mountPoint = nil;
	NSError *mountError = nil;
	if (!DIMountBSDName(BSDName, suggestedMountPoint, &mountPoint, &mountError)) {
		if (error != NULL) {
			*error = mountError;
		}
		return NO;
	}

	/* The producer's own state must report the same live mount. */
	DIProducerState *state = nil;
	NSString *identity = [handle di_producerIdentity];
	if (identity != nil) {
		state = DIProducerStateByIdentity(identity);
	}
	if (state == nil) {
		state = DIProducerStateByBSDName(BSDName);
	}
	if (state == nil || ![state isMounted] ||
		![[state mountPath] isEqualToString:mountPoint]) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Image device %@ is mounted at %@ but its producer does not report that mount", BSDName, mountPoint);
		}
		return NO;
	}
	return YES;
}

static void DIBestEffortDetachIdentity(NSString *identity)
{
	if (identity == nil) {
		return;
	}
	DIHdiutilRun([NSArray arrayWithObjects:@"detach", identity, nil], 30.0, NULL, NULL);
}

DIDeviceHandle *DIAttachImage(DIAttachParams *params, NSError **error)
{
	if (params == nil) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"No attach parameters were supplied");
		}
		return nil;
	}
	NSURL *url = [params URL];
	if (url == nil || ![url isFileURL] || [[url path] length] == 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"The attach parameters have no image file URL");
		}
		return nil;
	}
	if ([[params shadowURLs] count] != 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Shadow images are not supported by this read-only image provider");
		}
		return nil;
	}
	if ([params fileMode] < 0 || [params fileMode] > 2) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Unsupported image attach file mode %lld", [params fileMode]);
		}
		return nil;
	}
	if ([params onDiskCache]) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"On-disk caching is not supported by this image provider");
		}
		return nil;
	}

	struct stat imageStatus;
	if (stat([[url path] fileSystemRepresentation], &imageStatus) != 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot access image %@: %s", [url path], strerror(errno));
		}
		return nil;
	}
	if (!S_ISREG(imageStatus.st_mode)) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Image %@ is not a regular file", [url path]);
		}
		return nil;
	}
	int probe = open([[url path] fileSystemRepresentation], O_RDONLY | O_CLOEXEC);
	if (probe < 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot open image %@: %s", [url path], strerror(errno));
		}
		return nil;
	}
	close(probe);

	NSError *attachError = nil;
	NSString *identity = DIHdiutilAttachNoMount([url path], &attachError);
	if (identity == nil) {
		if (error != NULL) {
			*error = attachError;
		}
		return nil;
	}

	DIProducerState *state = nil;
	for (int attempt = 0; attempt < 250 && state == nil; attempt++) {
		state = DIProducerStateByIdentity(identity);
		if (state == nil) {
			struct timespec pause = { 0, 20 * 1000 * 1000 };
			nanosleep(&pause, NULL);
		}
	}
	if (state == nil) {
		DIBestEffortDetachIdentity(identity);
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"The image producer for %@ did not become available", [url path]);
		}
		return nil;
	}

	NSString *BSDName = [state bsdName];
	NSString *dataPath = [state dataPath];
	if (BSDName == nil || dataPath == nil) {
		DIProducerEject(state, NULL);
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeMissingBSDName, @"The image producer did not publish a BSD name and data node for %@", [url path]);
		}
		return nil;
	}

	NSError *probeError = nil;
	if (!DIDeviceDataPathProbe(dataPath, [state mediaSize], [state blockSize], &probeError)) {
		DIProducerEject(state, NULL);
		if (error != NULL) {
			*error = probeError;
		}
		return nil;
	}

	DIDeviceHandle *handle = [[DIDeviceHandle alloc] initWithRegEntryID:[state registryEntryID]];
	[handle setBSDName:BSDName];
	[handle di_setProducerIdentity:identity imageURL:url];

	DIAttachedDeviceInfo *info = [[DIAttachedDeviceInfo alloc] di_initWithIdentity:identity
		BSDName:BSDName
		imageURL:url];
	if (info != nil) {
		DIRegisterAttachedDevice(info);
		[info release];
	}
	return handle;
}
