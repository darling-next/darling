#import "SKInternal.h"

#include <errno.h>
#include <string.h>

#define SK_HOST_PROC_MOUNTS @"/Volumes/SystemRoot/proc/self/mounts"
#define SK_SYSTEM_ROOT @"/Volumes/SystemRoot"

static NSArray *SKHostMountLines(void)
{
	NSError *error = nil;
	NSString *contents = [NSString stringWithContentsOfFile:SK_HOST_PROC_MOUNTS
		encoding:NSUTF8StringEncoding
		error:&error];
	if (contents == nil) {
		SKLog("host mount table %s unavailable: %s", [SK_HOST_PROC_MOUNTS UTF8String],
			error != nil ? [[error description] UTF8String] : "unknown error");
		return nil;
	}
	return [contents componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]];
}

static BOOL SKPathIsWithin(NSString *path, NSString *mountPoint)
{
	if (path == nil || mountPoint == nil) {
		return NO;
	}
	if ([path isEqualToString:mountPoint]) {
		return YES;
	}
	if ([mountPoint isEqualToString:@"/"]) {
		return [path hasPrefix:@"/"];
	}
	return [path hasPrefix:mountPoint] && [path length] > [mountPoint length] &&
		[path characterAtIndex:[mountPoint length]] == '/';
}

@implementation SKBaseManager

+ (instancetype)sharedManager
{
	static SKBaseManager *shared = nil;
	@synchronized(self) {
		if (shared == nil) {
			shared = [[self alloc] init];
		}
	}
	return shared;
}

@end

@implementation SKManager

+ (instancetype)syncSharedManager
{
	SKManager *manager = (SKManager *)[self sharedManager];
	[manager syncAllDisks];
	return manager;
}

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_allDisks = [[NSMutableSet alloc] init];
	_hostDisks = [[NSMutableSet alloc] init];
	_attachedDisks = [[NSMutableSet alloc] init];
	[self syncAllDisks];
	return self;
}

- (void)dealloc
{
	[_allDisks release];
	[_hostDisks release];
	[_attachedDisks release];
	[super dealloc];
}

- (void)addHostMountLine:(NSString *)line toSet:(NSMutableSet *)set
{
	NSArray *fields = [line componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
	NSMutableArray *nonEmpty = [NSMutableArray arrayWithCapacity:6];
	for (NSString *field in fields) {
		if ([field length] > 0) {
			[nonEmpty addObject:field];
		}
	}
	if ([nonEmpty count] < 4) {
		return;
	}

	NSString *device = SKUnescapeMountField([nonEmpty objectAtIndex:0]);
	NSString *mountPoint = SKUnescapeMountField([nonEmpty objectAtIndex:1]);
	NSString *filesystemType = SKUnescapeMountField([nonEmpty objectAtIndex:2]);
	NSString *options = [nonEmpty objectAtIndex:3];

	SKDisk *disk = [[SKDisk alloc] initWithDevice:device
		mountPoint:mountPoint
		filesystemType:filesystemType
		readOnly:SKMountOptionsAreReadOnly(options)];
	if (disk == nil) {
		return;
	}

	NSString *guestPath = [SK_SYSTEM_ROOT stringByAppendingString:mountPoint];
	struct statfs fs;
	if (statfs([guestPath fileSystemRepresentation], &fs) == 0) {
		unsigned long long blockSize = (unsigned long long)fs.f_bsize;
		[disk setTotalSpace:(unsigned long long)fs.f_blocks * blockSize
			freeSpace:(unsigned long long)fs.f_bfree * blockSize
			availableSpace:(unsigned long long)fs.f_bavail * blockSize];
	}

	[set addObject:disk];
	[disk release];
}

- (void)addAttachedDisk:(SKDisk *)disk
{
	if (disk == nil) {
		return;
	}
	[_attachedDisks addObject:disk];
	[_allDisks addObject:disk];
	SKLog("attached disk registered: mount=%s image=%d",
		[[disk mountPoint] UTF8String], (int)[disk isDiskImage]);
}

- (void)removeAttachedDisk:(SKDisk *)disk
{
	if (disk == nil) {
		return;
	}
	[_attachedDisks removeObject:disk];
	[_allDisks removeObject:disk];
	SKLog("attached disk removed: mount=%s", [[disk mountPoint] UTF8String]);
}

/*
 * Reconcile registered image disks against the exact kernel mount instances they
 * recorded at attach. A registration is dropped only when the kernel mount table
 * was actually read and either no mount is present at that host path or the
 * present mount has a different mount identity (external detach + replacement).
 * An unreadable table leaves every registration intact: unknown state is not
 * evidence that a live mount vanished.
 */
- (void)reconcileAttachedDisks
{
	NSMutableArray *stale = [NSMutableArray array];
	for (SKDisk *attached in _attachedDisks) {
		SKMountRecord *expected = [attached kernelMountRecord];
		if (expected == nil) {
			/* No exact identity was recorded; do not guess one. */
			continue;
		}
		SKMountLookupStatus status = SKMountLookupUnknown;
		SKMountRecord *current = SKLookupMountRecord([expected hostMountPoint], &status);
		if (status == SKMountLookupUnknown) {
			SKLog("cannot read kernel mount table; keeping registration for %s",
				[[attached mountPoint] UTF8String]);
			continue;
		}
		if (status == SKMountLookupPresent && SKMountRecordIsSameMount(expected, current)) {
			continue;
		}
		[stale addObject:attached];
	}
	for (SKDisk *disk in stale) {
		[self removeAttachedDisk:disk];
	}
	if ([stale count] > 0) {
		SKLog("reconciled %lu stale attached image disk(s)", (unsigned long)[stale count]);
	}
}

- (void)syncAllDisks
{
	NSMutableSet *hostSet = [[NSMutableSet alloc] init];
	NSArray *lines = SKHostMountLines();
	for (NSString *line in lines) {
		[self addHostMountLine:line toSet:hostSet];
	}

	if ([hostSet count] == 0) {
		struct statfs *mounts = NULL;
		errno = 0;
		int count = getmntinfo(&mounts, MNT_NOWAIT);
		if (count > 0) {
			for (int i = 0; i < count; i++) {
				SKDisk *disk = [[SKDisk alloc] initWithDevice:SKStringFromCString(mounts[i].f_mntfromname)
					mountPoint:SKStringFromCString(mounts[i].f_mntonname)
					filesystemType:SKStringFromCString(mounts[i].f_fstypename)
					readOnly:(mounts[i].f_flags & MNT_RDONLY) != 0];
				if (disk != nil) {
					unsigned long long blockSize = (unsigned long long)mounts[i].f_bsize;
					[disk setTotalSpace:(unsigned long long)mounts[i].f_blocks * blockSize
						freeSpace:(unsigned long long)mounts[i].f_bfree * blockSize
						availableSpace:(unsigned long long)mounts[i].f_bavail * blockSize];
					[hostSet addObject:disk];
				}
				[disk release];
			}
		} else {
			SKLog("getmntinfo returned %d (errno=%d: %s)", count, errno, strerror(errno));
		}
	}

	[_hostDisks release];
	_hostDisks = hostSet;

	[self reconcileAttachedDisks];

	NSMutableSet *all = [[NSMutableSet alloc] initWithSet:_hostDisks];
	[all unionSet:_attachedDisks];
	[_allDisks release];
	_allDisks = all;

	SKLog("syncAllDisks: %lu host mount(s), %lu attached image disk(s)",
		(unsigned long)[_hostDisks count], (unsigned long)[_attachedDisks count]);
}

- (NSSet *)allDisksSet
{
	[self syncAllDisks];
	return _allDisks;
}

- (NSSet *)allDisks
{
	[self syncAllDisks];
	return [[_allDisks copy] autorelease];
}

- (SKDisk *)_attachedDiskForPath:(NSString *)normalized
{
	SKDisk *best = nil;
	NSUInteger bestLength = 0;
	for (SKDisk *disk in _attachedDisks) {
		NSString *mountPoint = [disk mountPoint];
		if (!SKPathIsWithin(normalized, mountPoint)) {
			continue;
		}
		if ([mountPoint length] > bestLength) {
			best = disk;
			bestLength = [mountPoint length];
		}
	}
	return best;
}

- (SKDisk *)_hostDiskForPath:(NSString *)hostPath
{
	SKDisk *best = nil;
	NSUInteger bestLength = 0;
	for (SKDisk *disk in _hostDisks) {
		NSString *mountPoint = [disk mountPoint];
		if (!SKPathIsWithin(hostPath, mountPoint)) {
			continue;
		}
		if ([mountPoint length] > bestLength) {
			best = disk;
			bestLength = [mountPoint length];
		}
	}
	return best;
}

- (SKDisk *)_diskForPath:(NSString *)path isNetworkMount:(char *)isNetworkMount
{
	if (isNetworkMount != NULL) {
		*isNetworkMount = 0;
	}
	if (path == nil || [path length] == 0) {
		return nil;
	}

	NSString *normalized = [path stringByStandardizingPath];
	if (![normalized isAbsolutePath]) {
		normalized = [[[NSFileManager defaultManager] currentDirectoryPath]
			stringByAppendingPathComponent:normalized];
		normalized = [normalized stringByStandardizingPath];
	}

	SKDisk *attached = [self _attachedDiskForPath:normalized];
	if (attached != nil) {
		if (isNetworkMount != NULL) {
			*isNetworkMount = [attached isNetwork] ? 1 : 0;
		}
		return attached;
	}

	/*
	 * A guest /Volumes path that is not an attached image must not fall back to the
	 * host root. That would report a disk for an unmounted guest volume.
	 */
	if ([normalized hasPrefix:@"/Volumes/"] && ![normalized hasPrefix:@"/Volumes/SystemRoot"]) {
		return nil;
	}

	NSString *hostPath = nil;
	if ([normalized isEqualToString:SK_SYSTEM_ROOT] ||
		[normalized hasPrefix:[SK_SYSTEM_ROOT stringByAppendingString:@"/"]]) {
		hostPath = [normalized substringFromIndex:[SK_SYSTEM_ROOT length]];
		if ([hostPath length] == 0) {
			hostPath = @"/";
		}
	} else {
		hostPath = @"/";
	}

	SKDisk *hostDisk = [self _hostDiskForPath:hostPath];
	if (hostDisk != nil && isNetworkMount != NULL) {
		*isNetworkMount = [hostDisk isNetwork] ? 1 : 0;
	}
	return hostDisk;
}

- (SKDisk *)diskForPath:(NSString *)path
{
	/*
	 * Refresh before answering: an image detached externally must not be
	 * returned from a registration that the kernel no longer backs.
	 */
	[self syncAllDisks];
	SKDisk *disk = [self _diskForPath:path isNetworkMount:NULL];
	if (disk == nil) {
		SKLog("diskForPath:%s -> nil (no attached image or matching mount)", [path UTF8String]);
	} else {
		SKLog("diskForPath:%s -> mount=%s image=%d", [path UTF8String],
			[[disk mountPoint] UTF8String], (int)[disk isDiskImage]);
	}
	return disk;
}

- (SKDisk *)diskForBSDname:(NSString *)name
{
	if (name == nil || [name length] == 0) {
		return nil;
	}
	[self syncAllDisks];
	NSString *device = [name hasPrefix:@"/dev/"] ? name : [@"/dev" stringByAppendingPathComponent:name];
	for (SKDisk *disk in _allDisks) {
		if ([[disk diskIdentifier] isEqualToString:device]) {
			return disk;
		}
	}
	return nil;
}

@end
