#import "SKInternal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* The host kernel's mount table, as seen through the guest SystemRoot view. */
#define SK_HOST_MOUNTINFO @"/Volumes/SystemRoot/proc/self/mountinfo"

/* Authoritative Darling guest-to-host path resolver, also used by hdiutil. */
extern int __darling_vchroot_expand(const char *path, char *out);

@interface SKMountRecord ()
- (instancetype)initWithMountID:(unsigned long long)mountID
	device:(NSString *)device
	filesystemType:(NSString *)filesystemType
	hostMountPoint:(NSString *)hostMountPoint
	readOnly:(bool)readOnly;
@end

void SKLog(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	fputs("StorageKitHdiutil: ", stderr);
	vfprintf(stderr, format, ap);
	fputc('\n', stderr);
	va_end(ap);
}

NSError *SKPOSIXError(int code)
{
	return [NSError errorWithDomain:NSPOSIXErrorDomain code:code userInfo:nil];
}

NSError *SKErrorWithMessage(int code, NSString *message)
{
	NSDictionary *userInfo = nil;
	if (message != nil) {
		userInfo = [NSDictionary dictionaryWithObject:message forKey:NSLocalizedDescriptionKey];
	}
	return [NSError errorWithDomain:NSPOSIXErrorDomain code:code userInfo:userInfo];
}

NSString *SKStringFromCString(const char *value)
{
	if (value == NULL || value[0] == '\0') {
		return nil;
	}
	return [NSString stringWithUTF8String:value];
}

NSString *SKHostPathForGuestMount(NSString *guestMount)
{
	if (guestMount == nil || [guestMount length] == 0) {
		return nil;
	}
	char hostPath[4096];
	memset(hostPath, 0, sizeof(hostPath));
	int result = __darling_vchroot_expand([guestMount fileSystemRepresentation], hostPath);
	if (result != 0 || hostPath[0] == '\0') {
		SKLog("vchroot expand failed for %s: rv=%d", [guestMount UTF8String], result);
		return nil;
	}
	return [NSString stringWithUTF8String:hostPath];
}

NSString *SKUnescapeMountField(NSString *field)
{
	if (field == nil || [field rangeOfString:@"\\"].location == NSNotFound) {
		return field;
	}
	NSMutableString *result = [NSMutableString stringWithCapacity:[field length]];
	NSUInteger length = [field length];
	for (NSUInteger i = 0; i < length; i++) {
		unichar c = [field characterAtIndex:i];
		if (c == '\\' && i + 3 < length) {
			unichar d0 = [field characterAtIndex:i + 1];
			unichar d1 = [field characterAtIndex:i + 2];
			unichar d2 = [field characterAtIndex:i + 3];
			if (d0 >= '0' && d0 <= '7' && d1 >= '0' && d1 <= '7' && d2 >= '0' && d2 <= '7') {
				unichar value = (unichar)((d0 - '0') * 64 + (d1 - '0') * 8 + (d2 - '0'));
				[result appendFormat:@"%C", value];
				i += 3;
				continue;
			}
		}
		[result appendFormat:@"%C", c];
	}
	return result;
}

bool SKMountOptionsAreReadOnly(NSString *options)
{
	for (NSString *option in [options componentsSeparatedByString:@","]) {
		if ([option isEqualToString:@"ro"]) {
			return true;
		}
	}
	return false;
}

@implementation SKMountRecord

- (instancetype)initWithMountID:(unsigned long long)mountID
	device:(NSString *)device
	filesystemType:(NSString *)filesystemType
	hostMountPoint:(NSString *)hostMountPoint
	readOnly:(bool)readOnly
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_mountID = mountID;
	_device = [device copy];
	_filesystemType = [filesystemType copy];
	_hostMountPoint = [hostMountPoint copy];
	_readOnly = readOnly;
	return self;
}

- (void)dealloc
{
	[_device release];
	[_filesystemType release];
	[_hostMountPoint release];
	[super dealloc];
}

- (unsigned long long)mountID
{
	return _mountID;
}

- (NSString *)device
{
	return _device;
}

- (NSString *)filesystemType
{
	return _filesystemType;
}

- (NSString *)hostMountPoint
{
	return _hostMountPoint;
}

- (bool)readOnly
{
	return _readOnly;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<SKMountRecord id=%llu device=%@ fs=%@ ro=%d mount=%@>",
		_mountID, _device, _filesystemType, _readOnly ? 1 : 0, _hostMountPoint];
}

@end

static bool SKStringsEqual(NSString *left, NSString *right)
{
	return left == right || (left != nil && right != nil && [left isEqualToString:right]);
}

bool SKMountRecordIsSameMount(SKMountRecord *expected, SKMountRecord *current)
{
	if (expected == nil || current == nil) {
		return false;
	}
	if ([expected mountID] != [current mountID]) {
		return false;
	}
	if (!SKStringsEqual([expected device], [current device])) {
		return false;
	}
	return SKStringsEqual([expected filesystemType], [current filesystemType]);
}

/*
 * /proc/self/mountinfo line shape:
 *   mount-id parent-id major:minor root mount-point mount-options [optional...] -
 *   filesystem-type mount-source super-options
 * The mount point and root fields escape space, tab, newline and backslash, so
 * splitting on whitespace is safe and the mount point is compared exactly after
 * unescaping. The mount ID is the kernel's per-instance identity.
 */
SKMountRecord *SKLookupMountRecord(NSString *hostMountPoint, SKMountLookupStatus *status)
{
	if (status != NULL) {
		*status = SKMountLookupUnknown;
	}
	if (hostMountPoint == nil || [hostMountPoint length] == 0) {
		return nil;
	}

	NSError *error = nil;
	NSString *contents = [NSString stringWithContentsOfFile:SK_HOST_MOUNTINFO
		encoding:NSUTF8StringEncoding
		error:&error];
	if (contents == nil) {
		SKLog("kernel mount table %s unavailable: %s", [SK_HOST_MOUNTINFO UTF8String],
			error != nil ? [[error description] UTF8String] : "unknown error");
		return nil;
	}

	if (status != NULL) {
		*status = SKMountLookupAbsent;
	}
	for (NSString *line in [contents componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]]) {
		NSArray *rawFields = [line componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
		NSMutableArray *fields = [NSMutableArray arrayWithCapacity:[rawFields count]];
		for (NSString *field in rawFields) {
			if ([field length] > 0) {
				[fields addObject:field];
			}
		}
		if ([fields count] < 10) {
			continue;
		}

		NSUInteger separator = NSNotFound;
		for (NSUInteger i = 6; i < [fields count]; i++) {
			if ([[fields objectAtIndex:i] isEqualToString:@"-"]) {
				separator = i;
				break;
			}
		}
		if (separator == NSNotFound || separator + 3 >= [fields count]) {
			continue;
		}

		NSString *mountPoint = SKUnescapeMountField([fields objectAtIndex:4]);
		if (![mountPoint isEqualToString:hostMountPoint]) {
			continue;
		}

		NSString *mountOptions = [fields objectAtIndex:5];
		NSString *filesystemType = [fields objectAtIndex:separator + 1];
		NSString *device = [fields objectAtIndex:separator + 2];
		NSString *superOptions = [fields objectAtIndex:separator + 3];
		unsigned long long mountID = strtoull([[fields objectAtIndex:0] UTF8String], NULL, 10);
		bool readOnly = SKMountOptionsAreReadOnly(mountOptions) ||
			SKMountOptionsAreReadOnly(superOptions);

		SKMountRecord *record = [[SKMountRecord alloc]
			initWithMountID:mountID
			device:device
			filesystemType:filesystemType
			hostMountPoint:mountPoint
			readOnly:readOnly];
		if (status != NULL) {
			*status = SKMountLookupPresent;
		}
		return [record autorelease];
	}

	return nil;
}

SKMountRecord *SKLookupGuestMountRecord(NSString *guestMountPoint, SKMountLookupStatus *status)
{
	if (status != NULL) {
		*status = SKMountLookupUnknown;
	}
	NSString *hostMountPoint = SKHostPathForGuestMount(guestMountPoint);
	if (hostMountPoint == nil) {
		return nil;
	}
	return SKLookupMountRecord(hostMountPoint, status);
}
