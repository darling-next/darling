/*
 * DiskImages2 private framework umbrella header.
 *
 * Implemented against the captured native provider metadata and the exact
 * unchanged DVTDownloads consumer: real image attach through the in-guest
 * hdiutil/producer authority, a version 3 producer query, and a direct
 * open/read probe of the selected device data node before a handle is
 * returned. Mount state is verified against the real kernel mount; nothing
 * in this framework fabricates a BSD name, IOMedia, or mount.
 */

#ifndef DISKIMAGES2_DISKIMAGES2_H
#define DISKIMAGES2_DISKIMAGES2_H

#import <Foundation/Foundation.h>

#import <DiskImages2/DIError.h>
#import <DiskImages2/DIAttachParams.h>
#import <DiskImages2/DIDeviceHandle.h>
#import <DiskImages2/DIAttachedDeviceInfo.h>

@interface DiskImages2 : NSObject

/* Attaches the image described by params. On success *handle is an
 * autoreleased handle for a live device; when params.autoMount is set, the
 * image is mounted and verified before success. On failure *handle is nil,
 * *error describes the failure, and no producer, mount or data daemon is
 * left behind. */
+ (BOOL)attachWithParams:(DIAttachParams *)params
	handle:(DIDeviceHandle **)handle
	error:(NSError **)error;

/* Attach plus mount plus registration verification: succeeds only when the
 * handle's BSD name matches a real mounted entity in the live producer
 * state. */
+ (BOOL)managedAttachWithParams:(DIAttachParams *)params
	handle:(DIDeviceHandle **)handle
	error:(NSError **)error;

@end

#endif /* DISKIMAGES2_DISKIMAGES2_H */
