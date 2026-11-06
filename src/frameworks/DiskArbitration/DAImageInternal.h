#ifndef DARLING_DA_IMAGE_INTERNAL_H
#define DARLING_DA_IMAGE_INTERNAL_H

#include <DiskArbitration/DiskArbitration.h>

struct DAImageReply;
DADiskRef _DAImageCreateApprovalDisk(DASessionRef session, const struct DAImageReply *state);
/* The image producer is the authority; a BSD-name token alone proves nothing. */
CFArrayRef _DAImageCopyDisks(DASessionRef session);
DASessionRef _DADiskGetSession(DADiskRef disk);
const char *_DADiskGetID(DADiskRef disk);
DADiskRef _DADiskSetCallbackDisk(DADiskRef disk);
DADiskRef _DADiskCreateImageSnapshot(DASessionRef session, CFStringRef identity,
    CFDictionaryRef description);
void _DASessionQueueOperation(DASessionRef session, DADiskRef disk,
    unsigned operation, CFURLRef path, unsigned options,
    DADiskMountCallback callback, void *context, CFArrayRef arguments);
DAReturn _DAImagePerformOperation(DADiskRef disk, unsigned operation,
    CFURLRef path, unsigned options, CFArrayRef arguments, CFStringRef *message);

#endif
