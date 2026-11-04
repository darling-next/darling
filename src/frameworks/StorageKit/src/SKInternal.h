/*
 * StorageKit: the measured class surface the CoreDevice consumer binds, backed by
 * a real in-guest hdiutil attach.
 *
 * The class and selector shapes are the ones the consumer imports and sends:
 *   SKDiskImage initWithURL:error:
 *   SKDiskImageAttachParams init / setIsManagedAttach: / mountParams
 *   SKMountParams setReadOnly: / setNoBrowse: / setAllParams: / setMountPoint:
 *   SKDiskImage attachWithParams:error:
 *   SKManager syncSharedManager / diskForPath:
 *   SKDisk isDiskImage
 *
 * Attach runs /usr/bin/hdiutil in the guest, proves the resulting mount through
 * the exact kernel mount instance (resolved from the guest path through the
 * vchroot prefix, then matched against the host kernel mount table), and records
 * a real SKDisk whose writability comes from that kernel record. Detach runs
 * hdiutil detach and drops the record only after the kernel no longer reports
 * that same mount instance.
 */

#import <Foundation/Foundation.h>
#import <stdbool.h>
#import <sys/types.h>
#import <sys/mount.h>

extern void SKLog(const char *format, ...) __attribute__((format(printf, 1, 2)));
extern NSError *SKPOSIXError(int code);
extern NSError *SKErrorWithMessage(int code, NSString *message);
extern NSString *SKStringFromCString(const char *value);

/*
 * The authoritative guest-to-host path resolver also used by hdiutil. A nil
 * result means the resolver itself failed, not that the path is unmounted.
 */
extern NSString *SKHostPathForGuestMount(NSString *guestMount);

extern bool SKMountOptionsAreReadOnly(NSString *options);
extern NSString *SKUnescapeMountField(NSString *field);

typedef NS_ENUM(int, SKMountLookupStatus) {
	/* The kernel mount table could not be read; mount state is unknown. */
	SKMountLookupUnknown = -1,
	/* The table was read and no mount with this identity is present. */
	SKMountLookupAbsent = 0,
	/* The table was read and an exact mount record is present. */
	SKMountLookupPresent = 1,
};

/*
 * One exact kernel mount instance from /proc/self/mountinfo: the mount ID is
 * unique for a mount instance, so an external detach followed by a replacement
 * mount at the same path is not mistaken for the original mount.
 */
@interface SKMountRecord : NSObject
{
	unsigned long long _mountID;
	NSString *_device;
	NSString *_filesystemType;
	NSString *_hostMountPoint;
	bool _readOnly;
}
- (unsigned long long)mountID;
- (NSString *)device;
- (NSString *)filesystemType;
- (NSString *)hostMountPoint;
- (bool)readOnly;
@end

extern SKMountRecord *SKLookupMountRecord(NSString *hostMountPoint,
	SKMountLookupStatus *status);
extern SKMountRecord *SKLookupGuestMountRecord(NSString *guestMountPoint,
	SKMountLookupStatus *status);
extern bool SKMountRecordIsSameMount(SKMountRecord *expected, SKMountRecord *current);

@interface SKMountParams : NSObject
{
	bool _readOnly;
	bool _noBrowse;
	bool _force;
	bool _recursive;
	bool _raidTraverse;
	NSURL *_mountPoint;
	NSSet *_allParams;
}
- (instancetype)initWithParams:(SKMountParams *)params;
- (bool)readOnly;
- (void)setReadOnly:(bool)value;
- (bool)noBrowse;
- (void)setNoBrowse:(bool)value;
- (bool)force;
- (void)setForce:(bool)value;
- (bool)recursive;
- (void)setRecursive:(bool)value;
- (bool)raidTraverse;
- (void)setRaidTraverse:(bool)value;
- (NSURL *)mountPoint;
- (void)setMountPoint:(NSURL *)mountPoint;
- (NSSet *)allParams;
- (void)setAllParams:(NSSet *)allParams;
@end

@interface SKDiskImageAttachParams : NSObject
{
	bool _readOnly;
	bool _stdinPassPhrase;
	bool _isManagedAttach;
	bool _is4KBlockSize;
	bool _emulateExternalDisk;
	long long _policy;
	SKMountParams *_mountParams;
}
- (instancetype)initWithParams:(SKMountParams *)params;
- (SKMountParams *)mountParams;
- (void)setMountParams:(SKMountParams *)mountParams;
- (bool)readOnly;
- (void)setReadOnly:(bool)value;
- (bool)stdinPassPhrase;
- (void)setStdinPassPhrase:(bool)value;
- (bool)isManagedAttach;
- (void)setIsManagedAttach:(bool)value;
- (bool)is4KBlockSize;
- (void)setIs4KBlockSize:(bool)value;
- (bool)emulateExternalDisk;
- (void)setEmulateExternalDisk:(bool)value;
- (long long)policy;
- (void)setPolicy:(long long)policy;
@end

@interface SKDisk : NSObject
{
	NSString *_diskIdentifier;
	NSString *_mountPoint;
	NSString *_volumeName;
	NSString *_filesystemType;
	NSString *_volumeUUID;
	bool _isDiskImage;
	bool _isNetwork;
	bool _isInternal;
	bool _isRemovable;
	bool _isWritable;
	unsigned long long _totalSpace;
	unsigned long long _freeSpace;
	unsigned long long _availableSpace;
	SKMountRecord *_kernelMountRecord;
}
- (instancetype)initWithMountPoint:(NSString *)mountPoint
	filesystemType:(NSString *)filesystemType
	readOnly:(bool)readOnly;
- (instancetype)initWithDevice:(NSString *)device
	mountPoint:(NSString *)mountPoint
	filesystemType:(NSString *)filesystemType
	readOnly:(bool)readOnly;
- (void)setTotalSpace:(unsigned long long)totalSpace
	freeSpace:(unsigned long long)freeSpace
	availableSpace:(unsigned long long)availableSpace;
/* Exact kernel mount instance backing an attached image disk, if known. */
- (void)setKernelMountRecord:(SKMountRecord *)record;
- (SKMountRecord *)kernelMountRecord;
- (char)isDiskImage;
- (NSString *)diskIdentifier;
- (NSString *)mountPoint;
- (NSString *)volumeName;
- (NSString *)filesystemType;
- (NSString *)volumeUUID;
- (bool)isNetwork;
- (bool)isInternal;
- (bool)isRemovable;
- (bool)isWritable;
- (unsigned long long)totalSpace;
- (unsigned long long)freeSpace;
- (unsigned long long)availableSpace;
@end

@interface SKBaseManager : NSObject
+ (instancetype)sharedManager;
@end

@interface SKManager : SKBaseManager
{
	NSMutableSet *_allDisks;
	NSMutableSet *_hostDisks;
	NSMutableSet *_attachedDisks;
}
+ (instancetype)syncSharedManager;
- (void)syncAllDisks;
- (void)addAttachedDisk:(SKDisk *)disk;
- (void)removeAttachedDisk:(SKDisk *)disk;
- (NSSet *)allDisksSet;
- (NSSet *)allDisks;
- (SKDisk *)diskForPath:(NSString *)path;
- (SKDisk *)_diskForPath:(NSString *)path isNetworkMount:(char *)isNetworkMount;
- (SKDisk *)diskForBSDname:(NSString *)name;
@end

@interface SKDiskImage : NSObject
{
	NSURL *_imageURL;
	NSArray *_shadowURLs;
	id _deviceHandle;
	id _diParams;
	NSString *_mountPoint;
	SKDisk *_mountedDisk;
	SKMountRecord *_mountRecord;
}
- (instancetype)initWithURL:(NSURL *)url error:(NSError **)error;
- (instancetype)initWithURL:(NSURL *)url diParams:(id)diParams shadowURLs:(NSArray *)shadowURLs error:(NSError **)error;
- (instancetype)initWithURL:(NSURL *)url diParams:(id)diParams error:(NSError **)error;
- (instancetype)initWithURL:(NSURL *)url shadowURLs:(NSArray *)shadowURLs error:(NSError **)error;
- (NSURL *)imageURL;
- (NSArray *)shadowURLs;
- (id)diParams;
- (void)setDiParams:(id)diParams;
- (id)deviceHandle;
- (void)setDeviceHandle:(id)deviceHandle;
- (char)isAttached;
- (NSString *)mountPoint;
- (id)attachWithParams:(SKDiskImageAttachParams *)params error:(NSError **)error;
- (id)attachWithError:(NSError **)error;
- (id)attachNoMountWithError:(NSError **)error;
- (id)diAttachWithParams:(SKDiskImageAttachParams *)params error:(NSError **)error;
- (char)mount:(NSURL *)mountPoint params:(SKMountParams *)params outError:(NSError **)error;
- (char)detachWithError:(NSError **)error;
- (char)ejectWithError:(NSError **)error;
@end
