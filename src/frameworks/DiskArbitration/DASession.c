#include "DASession.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <CoreFoundation/CFRuntime.h>
#include <CoreFoundation/CFBase.h>
#include <DiskArbitration/DiskArbitration.h>
#include <DiskArbitration/DiskArbitrationPrivate.h>

/*
 * Registered callbacks.  Darling has no Disk Arbitration server, so no event
 * source invokes them yet; keeping the registrations is still required
 * behaviour (the public API returns void and the unregister calls must remove
 * exactly what was registered).  The real framework delivers these from the
 * disk arbitration daemon; the delivery side is intentionally absent rather
 * than fabricated.
 */
enum {
  DA_CALLBACK_DISK_APPEARED,
  DA_CALLBACK_DESCRIPTION_CHANGED,
  DA_CALLBACK_DISK_DISAPPEARED,
  DA_CALLBACK_UNMOUNT_APPROVAL,
  DA_CALLBACK_IDLE,
};

typedef struct _DACallback {
  int kind;
  void *callback;
  void *context;
  CFTypeRef match;
  CFTypeRef watch;
  struct _DACallback *next;
} DACallback;

struct __DASession
{
  CFRuntimeBase _parent;
  const CFArrayCallBacks *_callBacks;
  DACallback *callbacks;
};

static CFTypeID _kDASessionTypeID = 0;
static void DASessionFinalize(CFTypeRef cf);
static Boolean DASessionEqual(CFTypeRef cf1, CFTypeRef cf2);
static CFHashCode DASessionHash(CFTypeRef cf);
static DASessionRef DASessionCreateCopy (CFAllocatorRef allocator, DASessionRef disk);

static CFRuntimeClass DASessionClass = {
  0,
  "DASession",
  NULL,
  (CFTypeRef (*)(CFAllocatorRef, CFTypeRef)) DASessionCreateCopy,
  DASessionFinalize,
  DASessionEqual,
  DASessionHash,
  NULL,
  NULL
};

__attribute__((constructor))
void
DASessionInitialize (void)
{
	_kDASessionTypeID = _CFRuntimeRegisterClass (&DASessionClass);
}

#define DASESSION_SIZE sizeof(struct __DASession) - sizeof(CFRuntimeBase)

static void
DASessionFinalize (CFTypeRef cf)
{
	DASessionRef session = (DASessionRef) cf;
	DACallback *callback = session->callbacks;
	while (callback) {
		DACallback *next = callback->next;
		if (callback->match) CFRelease(callback->match);
		if (callback->watch) CFRelease(callback->watch);
		free(callback);
		callback = next;
	}
	session->callbacks = NULL;
}

static Boolean
DASessionEqual (CFTypeRef cf1, CFTypeRef cf2)
{
	return 0;
}

static CFHashCode
DASessionHash (CFTypeRef cf)
{
	return 0;
}

static DASessionRef
DASessionCreateCopy (CFAllocatorRef allocator, DASessionRef disk)
{
	return (DASessionRef) CFRetain(disk);
}

CFTypeID DASessionGetTypeID(void)
{
	return _kDASessionTypeID;
}

DASessionRef DASessionCreate(CFAllocatorRef allocator)
{
	DASessionRef new;
	
	new = (DASessionRef) _CFRuntimeCreateInstance (allocator, _kDASessionTypeID,
												DASESSION_SIZE, 0);
	new->callbacks = NULL;
	
	return new;
}

static void
DAAddCallback(DASessionRef session, int kind, CFDictionaryRef match,
		CFArrayRef watch, void *callback, void *context)
{
	if (!session || !callback) return;

	DACallback *entry = calloc(1, sizeof(*entry));
	if (!entry) return;

	entry->kind = kind;
	entry->callback = callback;
	entry->context = context;
	entry->match = match ? (CFTypeRef) CFRetain(match) : NULL;
	entry->watch = watch ? (CFTypeRef) CFRetain(watch) : NULL;
	entry->next = session->callbacks;
	session->callbacks = entry;
}

static void
DARemoveCallback(DASessionRef session, void *callback, void *context,
		Boolean approvalOnly)
{
	if (!session || !callback) return;

	DACallback **link = &session->callbacks;
	while (*link) {
		DACallback *entry = *link;
		Boolean approval = entry->kind == DA_CALLBACK_UNMOUNT_APPROVAL;
		if (entry->callback == callback && entry->context == context &&
				(!approvalOnly || approval)) {
			*link = entry->next;
			if (entry->match) CFRelease(entry->match);
			if (entry->watch) CFRelease(entry->watch);
			free(entry);
		} else {
			link = &entry->next;
		}
	}
}

void DARegisterDiskAppearedCallback(DASessionRef session,
		CFDictionaryRef match, DADiskAppearedCallback callback, void *context)
{
	DAAddCallback(session, DA_CALLBACK_DISK_APPEARED, match, NULL, (void *) callback, context);
}

void DARegisterDiskDescriptionChangedCallback(DASessionRef session,
		CFDictionaryRef match, CFArrayRef watch,
		DADiskDescriptionChangedCallback callback, void *context)
{
	DAAddCallback(session, DA_CALLBACK_DESCRIPTION_CHANGED, match, watch, (void *) callback, context);
}

void DARegisterDiskDisappearedCallback(DASessionRef session,
		CFDictionaryRef match, DADiskDisappearedCallback callback, void *context)
{
	DAAddCallback(session, DA_CALLBACK_DISK_DISAPPEARED, match, NULL, (void *) callback, context);
}

void DARegisterDiskUnmountApprovalCallback(DASessionRef session,
		CFDictionaryRef match, DADiskUnmountApprovalCallback callback, void *context)
{
	DAAddCallback(session, DA_CALLBACK_UNMOUNT_APPROVAL, match, NULL, (void *) callback, context);
}

void DARegisterIdleCallback(DASessionRef session, DAIdleCallback callback, void *context)
{
	DAAddCallback(session, DA_CALLBACK_IDLE, NULL, NULL, (void *) callback, context);
}

void DAUnregisterCallback(DASessionRef session, void *callback, void *context)
{
	DARemoveCallback(session, callback, context, false);
}

void DAUnregisterApprovalCallback(DASessionRef session, void *callback, void *context)
{
	DARemoveCallback(session, callback, context, true);
}

void DASessionScheduleWithRunLoop(DASessionRef session,
		CFRunLoopRef runLoop, CFStringRef runLoopMode)
{
	
}

void DASessionUnscheduleFromRunLoop(DASessionRef session,
		CFRunLoopRef runLoop, CFStringRef runLoopMode)
{
	
}

void DASessionSetDispatchQueue(DASessionRef session, dispatch_queue_t queue)
{
	
}

struct _DAApprovalSession
{
  CFRuntimeBase _parent;
  const CFArrayCallBacks *_callBacks;
};

static CFTypeID _kDAApprovalSessionTypeID = 0;
static void DAApprovalSessionFinalize(CFTypeRef cf);
static Boolean DAApprovalSessionEqual(CFTypeRef cf1, CFTypeRef cf2);
static CFHashCode DAApprovalSessionHash(CFTypeRef cf);
static DAApprovalSessionRef DAApprovalSessionCreateCopy (CFAllocatorRef allocator, DAApprovalSessionRef disk);

static CFRuntimeClass DAApprovalSessionClass = {
  0,
  "DAApprovalSession",
  NULL,
  (CFTypeRef (*)(CFAllocatorRef, CFTypeRef)) DAApprovalSessionCreateCopy,
  DAApprovalSessionFinalize,
  DAApprovalSessionEqual,
  DAApprovalSessionHash,
  NULL,
  NULL
};

__attribute__((constructor))
void
DAApprovalSessionInitialize (void)
{
	_kDAApprovalSessionTypeID = _CFRuntimeRegisterClass (&DAApprovalSessionClass);
}

#define DAAPPROVALSESSION_SIZE sizeof(struct _DAApprovalSession) - sizeof(CFRuntimeBase)

static void
DAApprovalSessionFinalize (CFTypeRef cf)
{
}

static Boolean
DAApprovalSessionEqual (CFTypeRef cf1, CFTypeRef cf2)
{
	return 0;
}

static CFHashCode
DAApprovalSessionHash (CFTypeRef cf)
{
	return 0;
}

static DAApprovalSessionRef
DAApprovalSessionCreateCopy (CFAllocatorRef allocator, DAApprovalSessionRef disk)
{
	return (DAApprovalSessionRef) CFRetain(disk);
}

CFTypeID DAApprovalSessionGetTypeID(void)
{
	return _kDAApprovalSessionTypeID;
}

DAApprovalSessionRef DAApprovalSessionCreate(CFAllocatorRef allocator)
{
	DAApprovalSessionRef new;
	
	new = (DAApprovalSessionRef) _CFRuntimeCreateInstance (allocator,
												_kDAApprovalSessionTypeID,
												DAAPPROVALSESSION_SIZE, 0);
	
	return new;
}

void DAApprovalSessionScheduleWithRunLoop(DAApprovalSessionRef session,
		CFRunLoopRef runLoop, CFStringRef runLoopMode)
{
	
}

void DAApprovalSessionUnscheduleFromRunLoop(DAApprovalSessionRef session,
		CFRunLoopRef runLoop, CFStringRef runLoopMode)
{
	
}
