/* Consumer-side private declarations: the guest CLT SDK does not publish
 * DiskImages2 or DiskManagement headers. Exercise the same Objective-C ABI as
 * DVTDownloads, not a model or an in-process replacement for either framework.
 * Arguments: image expected-payload relative-payload filesystem-type.
 */
#import <Foundation/Foundation.h>
#import <DiskArbitration/DiskArbitration.h>
#import <IOKit/IOKitLib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

@class DIDeviceHandle;
@interface DIAttachParams : NSObject
- (instancetype)initWithURL:(NSURL *)url error:(NSError **)error;
- (void)setAutoMount:(BOOL)value;
- (void)setFileMode:(long long)value;
@end
@interface DIDeviceHandle : NSObject
- (NSString *)BSDName;
@end
@interface DiskImages2 : NSObject
+ (BOOL)attachWithParams:(DIAttachParams *)params handle:(DIDeviceHandle **)handle error:(NSError **)error;
+ (BOOL)managedAttachWithParams:(DIAttachParams *)params handle:(DIDeviceHandle **)handle error:(NSError **)error;
@end
@interface DIAttachedDeviceInfo : NSObject
- (instancetype)initWithBSDName:(NSString *)name error:(NSError **)error;
- (NSString *)BSDName;
- (NSArray *)copyEntitiesList;
+ (NSArray *)newDevicesArrayWithError:(NSError **)error;
@end
@interface DMManager : NSObject
+ (instancetype)sharedManagerForThread;
- (BOOL)mountDisk:(DADiskRef)disk entireDisk:(BOOL)whole atPath:(NSString *)path readOnly:(BOOL)readOnly otherArguments:(NSArray *)arguments timeout:(double)timeout errorDict:(NSDictionary **)error;
- (BOOL)unmountVolumePath:(NSString *)path force:(BOOL)force errorDict:(NSDictionary **)error;
- (BOOL)ejectDisk:(DADiskRef)disk errorDict:(NSDictionary **)error;
@end

struct Watch {
    NSMutableSet *identities;
    unsigned errors;
};

static void notice(void *context, io_iterator_t iterator)
{
    struct Watch *watch = context;
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        uint64_t identity = 0;
        if (IORegistryEntryGetRegistryEntryID(service, &identity) != KERN_SUCCESS)
            ++watch->errors;
        else
            [watch->identities addObject:[NSNumber numberWithUnsignedLongLong:identity]];
        IOObjectRelease(service);
    }
}

static BOOL waitForNotice(struct Watch *watch, uint64_t identity)
{
    CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 10;
    NSNumber *number = [NSNumber numberWithUnsignedLongLong:identity];
    while (![watch->identities containsObject:number] && CFAbsoluteTimeGetCurrent() < deadline)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    return watch->errors == 0 && [watch->identities containsObject:number];
}

static NSDictionary *entityForBSD(NSArray *entities, NSString *name)
{
    for (NSDictionary *entity in entities)
        if ([[entity objectForKey:@"BSD Name"] isEqual:name])
            return entity;
    return nil;
}

static BOOL listContainsBSD(NSArray *devices, NSString *name)
{
    for (DIAttachedDeviceInfo *device in devices)
        if ([[device BSDName] isEqual:name])
            return YES;
    return NO;
}

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "DISKIMAGES2_CHECK=FAIL line=%d %s\n", __LINE__, #condition); \
        if (error) fprintf(stderr, "DISKIMAGES2_ERROR=%s\n", [[error description] UTF8String]); \
        if (operationError) fprintf(stderr, "DISKIMAGES2_OPERATION_ERROR=%s\n", [[operationError description] UTF8String]); \
        ++failures; goto done; \
    } \
} while (0)

int main(int argc, char **argv)
{
    if (argc != 5) return 2;
    NSAutoreleasePool *pool = [NSAutoreleasePool new];
    unsigned failures = 0;
    NSError *error = nil;
    NSDictionary *operationError = nil;
    DIAttachParams *params = nil;
    DIDeviceHandle *handle = nil;
    DIAttachedDeviceInfo *info = nil;
    NSArray *entities = nil;
    NSArray *devices = nil;
    DASessionRef session = NULL;
    DADiskRef disk = NULL;
    CFDictionaryRef description = NULL;
    io_service_t media = IO_OBJECT_NULL;
    io_iterator_t matched = IO_OBJECT_NULL, terminated = IO_OBJECT_NULL;
    IONotificationPortRef notifications = NULL;
    CFRunLoopSourceRef source = NULL;
    struct Watch appeared = { [NSMutableSet new], 0 };
    struct Watch disappeared = { [NSMutableSet new], 0 };
    DMManager *manager = [DMManager sharedManagerForThread];
    NSString *mountPath = nil;
    NSString *bsdName = nil;
    BOOL attached = NO, mounted = NO;
    NSData *expected = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[2]]];
    NSString *relative = [NSString stringWithUTF8String:argv[3]];
    NSString *filesystem = [NSString stringWithUTF8String:argv[4]];
    uint64_t registryID = 0;
    int fd = -1;

    REQUIRE(expected != nil);
    params = [[DIAttachParams alloc] initWithURL:nil error:&error];
    REQUIRE(params == nil && [error isKindOfClass:[NSError class]]);
    REQUIRE([[error domain] isEqual:@"com.apple.DiskImages2.ErrorDomain"]);
    error = nil;
    notifications = IONotificationPortCreate(kIOMasterPortDefault);
    REQUIRE(notifications != NULL);
    source = IONotificationPortGetRunLoopSource(notifications);
    REQUIRE(source != NULL);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
    REQUIRE(IOServiceAddMatchingNotification(notifications, kIOFirstMatchNotification,
        IOServiceMatching("IOMedia"), notice, &appeared, &matched) == KERN_SUCCESS);
    notice(&appeared, matched);
    REQUIRE(IOServiceAddMatchingNotification(notifications, kIOTerminatedNotification,
        IOServiceMatching("IOMedia"), notice, &disappeared, &terminated) == KERN_SUCCESS);
    notice(&disappeared, terminated);

    params = [[DIAttachParams alloc] initWithURL:
        [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]] error:&error];
    [params setAutoMount:NO];
    [params setFileMode:2];
    REQUIRE([DiskImages2 attachWithParams:params handle:&handle error:&error]);
    attached = YES;
    bsdName = [[handle BSDName] copy];
    NSString *devicePath = [@"/dev" stringByAppendingPathComponent:bsdName];
    fd = open([devicePath fileSystemRepresentation], O_RDONLY);
    struct stat status;
    unsigned char byte;
    REQUIRE(fd >= 0 && fstat(fd, &status) == 0 && S_ISBLK(status.st_mode));
    REQUIRE(status.st_size > 0 && pread(fd, &byte, 1, status.st_size - 1) == 1);
    close(fd); fd = -1;
    media = IOServiceGetMatchingService(kIOMasterPortDefault,
        IOBSDNameMatching(kIOMasterPortDefault, 0, [bsdName UTF8String]));
    REQUIRE(media != IO_OBJECT_NULL && IOObjectConformsTo(media, "IOMedia"));
    REQUIRE(IORegistryEntryGetRegistryEntryID(media, &registryID) == KERN_SUCCESS);
    REQUIRE(waitForNotice(&appeared, registryID));

    session = DASessionCreate(NULL);
    disk = DADiskCreateFromBSDName(NULL, session, [bsdName UTF8String]);
    REQUIRE(disk != NULL);
    io_service_t sameMedia = DADiskCopyIOMedia(disk);
    uint64_t sameID = 0;
    BOOL sameBacking = sameMedia != IO_OBJECT_NULL &&
        IORegistryEntryGetRegistryEntryID(sameMedia, &sameID) == KERN_SUCCESS && sameID == registryID;
    if (sameMedia) IOObjectRelease(sameMedia);
    REQUIRE(sameBacking);
    description = DADiskCopyDescription(disk);
    CFTypeRef volumeType = description ?
        CFDictionaryGetValue(description, kDADiskDescriptionVolumeTypeKey) : NULL;
    REQUIRE(volumeType && CFEqual(volumeType, (CFStringRef)filesystem));
    REQUIRE(!CFDictionaryContainsKey(description, kDADiskDescriptionVolumePathKey));
    CFRelease(description); description = NULL;
    info = [[DIAttachedDeviceInfo alloc] initWithBSDName:bsdName error:&error];
    entities = [info copyEntitiesList];
    REQUIRE([[entityForBSD(entities, bsdName) objectForKey:@"Dev Entry"] isEqual:devicePath]);
    REQUIRE([entityForBSD(entities, bsdName) objectForKey:@"Mount Point"] == nil);
    [entities release]; entities = nil;
    mountPath = [@"/Volumes/diskimages2-consumer-proof" copy];
    REQUIRE([manager mountDisk:disk entireDisk:YES atPath:mountPath readOnly:YES
        otherArguments:nil timeout:10 errorDict:&operationError]);
    mounted = YES;
    NSString *payload = [mountPath stringByAppendingPathComponent:relative];
    REQUIRE([[NSData dataWithContentsOfFile:payload] isEqualToData:expected]);
    entities = [info copyEntitiesList];
    REQUIRE([[entityForBSD(entities, bsdName) objectForKey:@"Mount Point"] isEqual:mountPath]);
    [entities release]; entities = nil;
    REQUIRE([manager unmountVolumePath:mountPath force:NO errorDict:&operationError]);
    mounted = NO;
    REQUIRE(access([payload fileSystemRepresentation], F_OK) != 0 && errno == ENOENT);
    entities = [info copyEntitiesList];
    REQUIRE([entityForBSD(entities, bsdName) objectForKey:@"Mount Point"] == nil);
    [entities release]; entities = nil;
    REQUIRE([manager mountDisk:disk entireDisk:YES atPath:mountPath readOnly:YES
        otherArguments:nil timeout:10 errorDict:&operationError]);
    mounted = YES;
    REQUIRE([[NSData dataWithContentsOfFile:payload] isEqualToData:expected]);
    REQUIRE([manager ejectDisk:disk errorDict:&operationError]);
    attached = NO; mounted = NO;
    REQUIRE(waitForNotice(&disappeared, registryID));
    REQUIRE(access([devicePath fileSystemRepresentation], F_OK) != 0 && errno == ENOENT);
    io_service_t gone = IOServiceGetMatchingService(kIOMasterPortDefault,
        IOBSDNameMatching(kIOMasterPortDefault, 0, [bsdName UTF8String]));
    if (gone) IOObjectRelease(gone);
    REQUIRE(gone == IO_OBJECT_NULL);
    devices = [DIAttachedDeviceInfo newDevicesArrayWithError:&error];
    REQUIRE(devices != nil && !listContainsBSD(devices, bsdName));
    [devices release]; devices = nil;
    puts("DISKIMAGES2_MANUAL_LIFECYCLE=PASS");

    CFRelease(disk); disk = NULL;
    [info release]; info = nil;
    [bsdName release]; bsdName = nil;
    [mountPath release]; mountPath = nil;
    handle = nil;
    REQUIRE([DiskImages2 managedAttachWithParams:params handle:&handle error:&error]);
    attached = YES; mounted = YES;
    bsdName = [[handle BSDName] copy];
    disk = DADiskCreateFromBSDName(NULL, session, [bsdName UTF8String]);
    description = DADiskCopyDescription(disk);
    REQUIRE(description != NULL);
    CFTypeRef mountedURL = CFDictionaryGetValue(description, kDADiskDescriptionVolumePathKey);
    REQUIRE(mountedURL && CFGetTypeID(mountedURL) == CFURLGetTypeID());
    mountPath = [[(NSURL *)mountedURL path] copy];
    REQUIRE(mountPath != nil && [[NSData dataWithContentsOfFile:
        [mountPath stringByAppendingPathComponent:relative]] isEqualToData:expected]);
    CFRelease(description); description = NULL;
    REQUIRE(![manager unmountVolumePath:mountPath force:YES errorDict:&operationError]);
    REQUIRE([[NSData dataWithContentsOfFile:
        [mountPath stringByAppendingPathComponent:relative]] isEqualToData:expected]);
    NSString *managedDevice = [@"/dev" stringByAppendingPathComponent:bsdName];
    fd = open([managedDevice fileSystemRepresentation], O_RDONLY);
    REQUIRE(fd >= 0 && ioctl(fd, DKIOCEJECT, 0) == 0);
    close(fd); fd = -1;
    attached = NO; mounted = NO;
    REQUIRE(access([[mountPath stringByAppendingPathComponent:relative] fileSystemRepresentation], F_OK) != 0 && errno == ENOENT);
    puts("DISKIMAGES2_MANAGED_LIFECYCLE=PASS");

done:
    if (fd >= 0) close(fd);
    if (attached && !disk && bsdName) {
        if (!session) session = DASessionCreate(NULL);
        if (session) disk = DADiskCreateFromBSDName(NULL, session, [bsdName UTF8String]);
    }
    if (mounted && ![manager unmountVolumePath:mountPath force:NO errorDict:&operationError]) ++failures;
    if (attached && disk && ![manager ejectDisk:disk errorDict:&operationError]) ++failures;
    if (failures) {
        if (error) NSLog(@"DiskImages2 error: %@", error);
        if (operationError) NSLog(@"DiskManagement error: %@", operationError);
    }
    if (description) CFRelease(description);
    if (disk) CFRelease(disk);
    if (session) CFRelease(session);
    if (media) IOObjectRelease(media);
    if (source) CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
    if (matched) IOObjectRelease(matched);
    if (terminated) IOObjectRelease(terminated);
    if (notifications) IONotificationPortDestroy(notifications);
    [appeared.identities release]; [disappeared.identities release];
    [entities release]; [devices release]; [info release];
    [bsdName release]; [mountPath release]; [params release];
    printf("DISKIMAGES2_FAILURES=%u\n", failures);
    if (!failures) puts("DISKIMAGES2_IMAGE_SMOKE=PASS");
    [pool release];
    return failures ? 1 : 0;
}
