/*
 * DiskImages2 private framework: provider error domain.
 *
 * The domain value is the one the native framework exports and that the
 * DVTDownloads consumer compares against (it remaps code 152 from this
 * domain). Code 153 is the native DIDeviceHandle failure for a device that
 * has no BSD name. Every other failure from this provider uses the generic
 * code and carries a concrete localized description; this provider never
 * returns a fabricated success or a synthetic device identity.
 */

#ifndef DISKIMAGES2_DIERROR_H
#define DISKIMAGES2_DIERROR_H

#import <Foundation/Foundation.h>

extern NSString *const DIErrorDomain;

typedef NS_ENUM(NSInteger, DIErrorCode) {
	/* Unspecified provider failure; userInfo describes the exact cause. */
	DIErrorCodeGeneric = 1,
	/* Native DiskImages2 error code for a device without a BSD name. */
	DIErrorCodeMissingBSDName = 153
};

#endif /* DISKIMAGES2_DIERROR_H */
