/*
 * DiskImages2 private framework: DIAttachedDeviceInfo.
 *
 * The consumer constructs this object from the handle's BSD name, reads
 * -imageURL / -BSDName, and consumes -copyEntitiesList. Entity dictionaries
 * are derived from a fresh query of the live image producer, so they carry
 * the real BSD name, device entry, content and volume identity, and a Mount
 * Point only while the producer reports a real kernel mount.
 */

#ifndef DISKIMAGES2_DIATTACHEDDEVICEINFO_H
#define DISKIMAGES2_DIATTACHEDDEVICEINFO_H

#import <Foundation/Foundation.h>

@interface DIAttachedDeviceInfo : NSObject {
@private
	NSString *_BSDName;
	NSURL *_imageURL;
	/* Provider-internal state. */
	NSString *_di_identity;
}

- (instancetype)initWithBSDName:(NSString *)BSDName error:(NSError **)error;
- (NSString *)BSDName;
- (NSURL *)imageURL;

/* DI1-style entity dictionaries for this device, or an empty array once the
 * device has been ejected. */
- (NSArray *)copyEntitiesList;

/* Live attached devices. Returns an empty array when no producer is
 * attached. */
+ (NSArray *)newDevicesArrayWithError:(NSError **)error;

@end

#endif /* DISKIMAGES2_DIATTACHEDDEVICEINFO_H */
