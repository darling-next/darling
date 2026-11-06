/*
 * DiskImages2 private framework: DIDeviceHandle.
 *
 * The consumer reads only -BSDName from the handle returned by attach. The
 * handle also carries the live producer identity and the image URL for the
 * mount/cleanup paths and for device-info construction.
 */

#ifndef DISKIMAGES2_DIDEVICEHANDLE_H
#define DISKIMAGES2_DIDEVICEHANDLE_H

#import <Foundation/Foundation.h>

@interface DIDeviceHandle : NSObject {
@private
	NSString *_BSDName;
	unsigned long long _regEntryID;
	/* Provider-internal state. */
	NSURL *_di_imageURL;
	NSString *_di_producerIdentity;
}

- (instancetype)initWithRegEntryID:(unsigned long long)regEntryID;
- (NSString *)BSDName;
- (void)setBSDName:(NSString *)BSDName;
- (unsigned long long)regEntryID;

@end

#endif /* DISKIMAGES2_DIDEVICEHANDLE_H */
