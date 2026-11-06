/*
 * DiskImages2 provider-internal interfaces.
 *
 * The live attachment authority is the opened image producer: each producer
 * publishes an authenticated AF_UNIX socket under DAImageDirectory and
 * answers DA_IMAGE_QUERY with the version 3 state (BSD names, registry IDs,
 * data node path, size, block size, content, mount instance). Device
 * readiness must be proven by opening and reading that data node.
 *
 * hdiutil is the supported attach entry point (it opens the image and starts
 * the producer). Its -plist output identifies the producer; the query reply
 * remains the metadata authority.
 */

#ifndef DISKIMAGES2_DIINTERNAL_H
#define DISKIMAGES2_DIINTERNAL_H

#import <Foundation/Foundation.h>

#include <DiskImages2/DIError.h>
#include <DiskImages2/DIAttachParams.h>
#include <DiskImages2/DIDeviceHandle.h>
#include <DiskImages2/DIAttachedDeviceInfo.h>

/* Wire contract shared with darling-dmg; its directory is on this target's
 * include path (see CMakeLists.txt). */
#include "DAImageProtocol.h"

@class DIProducerState;

@interface DIDeviceHandle (DIInternal)
- (void)di_setProducerIdentity:(NSString *)identity imageURL:(NSURL *)imageURL;
- (NSString *)di_producerIdentity;
- (NSURL *)di_imageURL;
@end

@interface DIAttachedDeviceInfo (DIInternal)
- (instancetype)di_initWithIdentity:(NSString *)identity
	BSDName:(NSString *)BSDName
	imageURL:(NSURL *)imageURL;
- (NSString *)di_identity;
@end

/* Provider-internal registry of attachments made by this process. It only
 * supplies information the producer does not publish (the image URL);
 * liveness and mount state always come from a fresh producer query. */
void DIRegisterAttachedDevice(DIAttachedDeviceInfo *info);
DIAttachedDeviceInfo *DIAttachedDeviceForIdentity(NSString *identity);
DIAttachedDeviceInfo *DIAttachedDeviceForBSDName(NSString *BSDName);
void DIForgetAttachedDevice(NSString *identity);

NSError *DIErrorMake(NSInteger code, NSString *format, ...)
	NS_FORMAT_FUNCTION(2, 3);

/* One live producer's queried state. */
@interface DIProducerState : NSObject {
@public
	NSString *socketPath;
	struct DAImageReply reply;
}
- (NSString *)identity;
- (NSString *)bsdName;
- (NSString *)wholeBSDName;
- (NSString *)dataPath;
- (NSString *)mountPath;
- (BOOL)isMounted;
- (unsigned long long)mediaSize;
- (unsigned int)blockSize;
- (NSString *)content;
- (NSString *)filesystem;
- (NSString *)volumeName;
- (NSString *)volumeUUID;
- (unsigned long long)registryEntryID;
@end

/* Queries every live producer. Returns the live states; an absent image
 * directory is an empty list, not an error. */
NSArray *DIProducerCopyStates(void);

DIProducerState *DIProducerStateByIdentity(NSString *identity);
DIProducerState *DIProducerStateByBSDName(NSString *BSDName);

/* Producer authority teardown: unmounts a live mount and removes the
 * attachment. Idempotent; a producer that is already gone is not an error. */
BOOL DIProducerEject(DIProducerState *state, NSError **error);

/* Opens the selected data node read-only and reads real bytes from both ends
 * of the device. Fails when the node is absent, the size disagrees with the
 * producer's reported selected size, or a read fails. */
BOOL DIDeviceDataPathProbe(NSString *dataPath,
	unsigned long long expectedSize,
	unsigned int blockSize,
	NSError **error);

/* hdiutil runner: real spawn, captured output, finite timeout, no orphaned
 * child. */
BOOL DIHdiutilRun(NSArray *arguments,
	NSTimeInterval timeout,
	NSString **output,
	NSError **error);

/* Attach through hdiutil without mounting; returns the producer identity
 * reported by the successful attach. */
NSString *DIHdiutilAttachNoMount(NSString *imagePath, NSError **error);

/* Parsed `hdiutil info -plist` state, or nil. Root key "images"; each image
 * dictionary has image-path, darling-image-id and system-entities. */
NSDictionary *DIHdiutilInfoPlist(NSError **error);

/* Image path for a producer identity or BSD name, from the live info state. */
NSString *DIProducerImagePath(NSString *identity, NSString *BSDName);

/* True when path is a distinct mounted file system (device differs from its
 * parent) and its root directory is readable. */
BOOL DIMountPointIsReal(NSString *path);

/* Attach implementation shared by DIAttachParams and DiskImages2. Returns a
 * new (owned) handle whose device readiness was proven. */
DIDeviceHandle *DIAttachImage(DIAttachParams *params, NSError **error);

/* Mounts and verifies a handle created by DIAttachImage. */
BOOL DIMountDeviceHandle(DIDeviceHandle *handle,
	NSString *suggestedMountPoint,
	NSError **error);

#endif /* DISKIMAGES2_DIINTERNAL_H */
