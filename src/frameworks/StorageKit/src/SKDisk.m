#import "SKInternal.h"

#include <string.h>

static bool SKIsNetworkFilesystem(const char *fstype)
{
	static const char *networkTypes[] = {
		"nfs", "nfs4", "cifs", "smbfs", "afpfs", "webdav", "ftp", "9p", NULL
	};
	if (fstype == NULL) {
		return false;
	}
	for (size_t i = 0; networkTypes[i] != NULL; i++) {
		if (strcmp(fstype, networkTypes[i]) == 0) {
			return true;
		}
	}
	return false;
}

static bool SKIsInternalDevice(const char *device)
{
	static const char *prefixes[] = {
		"/dev/sd", "/dev/nvme", "/dev/vd", "/dev/dm-", "/dev/md", "/dev/mmcblk", "/dev/xvd", NULL
	};
	if (device == NULL) {
		return false;
	}
	for (size_t i = 0; prefixes[i] != NULL; i++) {
		if (strncmp(device, prefixes[i], strlen(prefixes[i])) == 0) {
			return true;
		}
	}
	return false;
}

static bool SKIsRemovableDevice(const char *device)
{
	if (device == NULL) {
		return false;
	}
	return strncmp(device, "/dev/sr", 7) == 0 || strncmp(device, "/dev/loop", 9) == 0;
}

@implementation SKDisk

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_diskIdentifier = nil;
		_mountPoint = nil;
		_volumeName = nil;
		_filesystemType = nil;
		_volumeUUID = nil;
		_isDiskImage = false;
		_isNetwork = false;
		_isInternal = false;
		_isRemovable = false;
		_isWritable = true;
		_totalSpace = 0;
		_freeSpace = 0;
		_availableSpace = 0;
		_kernelMountRecord = nil;
	}
	return self;
}

- (instancetype)initWithMountPoint:(NSString *)mountPoint
	filesystemType:(NSString *)filesystemType
	readOnly:(bool)readOnly
{
	self = [self init];
	if (self == nil) {
		return nil;
	}

	_mountPoint = [mountPoint copy];
	_volumeName = [[_mountPoint lastPathComponent] copy];
	_diskIdentifier = [mountPoint copy];
	_isDiskImage = true;
	_isRemovable = true;
	_isWritable = !readOnly;

	struct statfs fs;
	if (mountPoint != nil && statfs([mountPoint fileSystemRepresentation], &fs) == 0) {
		_filesystemType = [SKStringFromCString(fs.f_fstypename) copy];
		unsigned long long blockSize = (unsigned long long)fs.f_bsize;
		_totalSpace = (unsigned long long)fs.f_blocks * blockSize;
		_freeSpace = (unsigned long long)fs.f_bfree * blockSize;
		_availableSpace = (unsigned long long)fs.f_bavail * blockSize;
	}
	if (_filesystemType == nil) {
		_filesystemType = [filesystemType copy];
	}
	return self;
}

- (instancetype)initWithDevice:(NSString *)device
	mountPoint:(NSString *)mountPoint
	filesystemType:(NSString *)filesystemType
	readOnly:(bool)readOnly
{
	self = [self init];
	if (self == nil) {
		return nil;
	}
	_diskIdentifier = [device copy];
	_mountPoint = [mountPoint copy];
	_filesystemType = [filesystemType copy];
	_volumeName = [[_mountPoint lastPathComponent] copy];
	/*
	 * A host mount device name does not prove that the mount is backed by an
	 * image. Only the provider's genuine attached-image record sets this flag.
	 */
	_isDiskImage = false;
	_isNetwork = filesystemType != nil && SKIsNetworkFilesystem([filesystemType UTF8String]);
	_isInternal = device != nil && SKIsInternalDevice([device UTF8String]);
	_isRemovable = device != nil && SKIsRemovableDevice([device UTF8String]);
	_isWritable = !readOnly;
	return self;
}

- (void)setTotalSpace:(unsigned long long)totalSpace
	freeSpace:(unsigned long long)freeSpace
	availableSpace:(unsigned long long)availableSpace
{
	_totalSpace = totalSpace;
	_freeSpace = freeSpace;
	_availableSpace = availableSpace;
}

- (void)setKernelMountRecord:(SKMountRecord *)record
{
	if (_kernelMountRecord == record) {
		return;
	}
	[record retain];
	[_kernelMountRecord release];
	_kernelMountRecord = record;
}

- (SKMountRecord *)kernelMountRecord
{
	return _kernelMountRecord;
}

- (void)dealloc
{
	[_diskIdentifier release];
	[_mountPoint release];
	[_volumeName release];
	[_filesystemType release];
	[_volumeUUID release];
	[_kernelMountRecord release];
	[super dealloc];
}

- (char)isDiskImage
{
	return _isDiskImage ? 1 : 0;
}

- (NSString *)diskIdentifier
{
	return _diskIdentifier;
}

- (NSString *)mountPoint
{
	return _mountPoint;
}

- (NSString *)volumeName
{
	return _volumeName;
}

- (NSString *)filesystemType
{
	return _filesystemType;
}

- (NSString *)volumeUUID
{
	return _volumeUUID;
}

- (bool)isNetwork
{
	return _isNetwork;
}

- (bool)isInternal
{
	return _isInternal;
}

- (bool)isRemovable
{
	return _isRemovable;
}

- (bool)isWritable
{
	/*
	 * For an attached image the exact kernel mount record is the authority: an
	 * image that was requested writable is still reported read-only when the
	 * kernel mounted it read-only (hdiutil does that for every image here).
	 */
	if (_kernelMountRecord != nil) {
		return [_kernelMountRecord readOnly] ? false : true;
	}
	return _isWritable;
}

- (unsigned long long)totalSpace
{
	return _totalSpace;
}

- (unsigned long long)freeSpace
{
	return _freeSpace;
}

- (unsigned long long)availableSpace
{
	return _availableSpace;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<SKDisk id=%@ mount=%@ fs=%@ image=%d network=%d>",
		_diskIdentifier, _mountPoint, _filesystemType, _isDiskImage ? 1 : 0, _isNetwork ? 1 : 0];
}

@end
