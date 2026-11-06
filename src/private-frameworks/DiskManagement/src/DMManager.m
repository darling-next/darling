/*
This file is part of Darling.

Copyright (C) 2017 Lubos Dolezel

Darling is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Darling is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Darling.  If not, see <http://www.gnu.org/licenses/>.
*/

#import <DiskManagement/DiskManagement.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* A synchronous DMManager call must not block the DiskArbitration delivery
 * context: approval callbacks are delivered on the caller's own session while
 * the operation worker waits for them. Operations therefore run through this
 * manager's DASession on a private dispatch queue, and the caller waits on a
 * semaphore. Completion state lives in a heap context with an explicit
 * reference count, so a timeout can return while a late callback still owns
 * its context (never a pointer to a freed stack frame). */

enum {
	DM_OPERATION_PENDING = 0,
	DM_OPERATION_COMPLETED,
	DM_OPERATION_ABANDONED
};

typedef struct DMPendingRegistry DMPendingRegistry;

typedef struct DMOperationContext {
	_Atomic int state;
	_Atomic int references;
	DMPendingRegistry *registry;
	dispatch_semaphore_t semaphore;
	DAReturn status;
	CFStringRef message;
	struct DMOperationContext *next;
} DMOperationContext;

struct DMPendingRegistry {
	_Atomic int references;
	pthread_mutex_t lock;
	DMOperationContext *head;
};

static DMPendingRegistry *DMPendingCreate(void)
{
	DMPendingRegistry *registry = calloc(1, sizeof(*registry));
	if (!registry) return NULL;
	atomic_init(&registry->references, 1);
	pthread_mutex_init(&registry->lock, NULL);
	return registry;
}

static void DMPendingRetain(DMPendingRegistry *registry)
{
	atomic_fetch_add(&registry->references, 1);
}

static void DMPendingRelease(DMPendingRegistry *registry)
{
	if (atomic_fetch_sub(&registry->references, 1) != 1) return;
	pthread_mutex_destroy(&registry->lock);
	free(registry);
}

static void DMContextRelease(DMOperationContext *context);

static DMOperationContext *DMContextCreate(DMPendingRegistry *registry)
{
	DMOperationContext *context = calloc(1, sizeof(*context));
	if (!context) return NULL;
	atomic_init(&context->state, DM_OPERATION_PENDING);
	/* One reference belongs to the waiting caller, one to the queued
	 * operation (the callback, or the manager while it is still pending). */
	atomic_init(&context->references, 2);
	context->semaphore = dispatch_semaphore_create(0);
	if (!context->semaphore) { free(context); return NULL; }
	context->registry = registry;
	DMPendingRetain(registry);
	pthread_mutex_lock(&registry->lock);
	context->next = registry->head;
	registry->head = context;
	pthread_mutex_unlock(&registry->lock);
	return context;
}

static void DMContextRetain(DMOperationContext *context)
{
	atomic_fetch_add(&context->references, 1);
}

static void DMContextRelease(DMOperationContext *context)
{
	if (atomic_fetch_sub(&context->references, 1) != 1) return;
	if (context->message) CFRelease(context->message);
	dispatch_release(context->semaphore);
	DMPendingRelease(context->registry);
	free(context);
}

/* The callback takes its own reference before touching the pending list, so a
 * concurrent manager deallocation cannot free the context mid-callback. */
static void DMOperationFinished(DADiskRef disk, DADissenterRef dissenter, void *opaque)
{
	DMOperationContext *context = opaque;
	DMContextRetain(context);
	DAReturn status = dissenter ? DADissenterGetStatus(dissenter) : kDAReturnSuccess;
	CFStringRef message = dissenter ? DADissenterGetStatusString(dissenter) : NULL;

	DMPendingRegistry *registry = context->registry;
	Boolean removed = false;
	pthread_mutex_lock(&registry->lock);
	DMOperationContext **link = &registry->head;
	while (*link) {
		if (*link == context) { *link = context->next; context->next = NULL; removed = true; break; }
		link = &(*link)->next;
	}
	pthread_mutex_unlock(&registry->lock);
	if (removed) DMContextRelease(context);

	int expected = DM_OPERATION_PENDING;
	/* Publish the result before the state transition: a timed-out caller whose
	 * CAS fails because completion won must never observe a partial result. */
	context->status = status;
	if (message) context->message = (CFStringRef)CFRetain(message);
	if (atomic_compare_exchange_strong(&context->state, &expected, DM_OPERATION_COMPLETED))
		dispatch_semaphore_signal(context->semaphore);
	DMContextRelease(context);
}

static NSDictionary *DMCreateErrorDictionary(BOOL timedOut, CFStringRef message)
{
	NSMutableDictionary *dictionary = [NSMutableDictionary dictionary];
	[dictionary setObject:[NSNumber numberWithBool:timedOut] forKey:@"TimedOut"];
	if (message) [dictionary setObject:(NSString *)message forKey:NSLocalizedDescriptionKey];
	return [[dictionary copy] autorelease];
}

/* Wait for the operation or the caller's deadline. Returns YES only for a
 * backend success; timedOut distinguishes a late completion from failure. */
static BOOL DMWaitForCompletion(DMOperationContext *context, double timeout,
	BOOL *timedOut, DAReturn *status, CFStringRef *message)
{
	if (timeout <= 0) timeout = 60.0;
	dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW,
		(int64_t)(timeout * (double)NSEC_PER_SEC));
	dispatch_semaphore_wait(context->semaphore, deadline);

	int expected = DM_OPERATION_PENDING;
	if (!atomic_compare_exchange_strong(&context->state, &expected, DM_OPERATION_ABANDONED)) {
		/* The callback won the race; its result is authoritative. */
		*status = context->status;
		*message = context->message ? (CFStringRef)CFRetain(context->message) : NULL;
		return *status == kDAReturnSuccess;
	}
	*timedOut = YES;
	*status = kDAReturnNotReady;
	return NO;
}

@interface DMManagerPrivateVars : NSObject {
@public
	DASessionRef session;
	dispatch_queue_t queue;
	DMPendingRegistry *pending;
	NSLock *operationLock;
}
@end

@implementation DMManagerPrivateVars

- (instancetype)init
{
	self = [super init];
	if (!self) return nil;
	operationLock = [[NSLock alloc] init];
	pending = DMPendingCreate();
	session = DASessionCreate(NULL);
	queue = dispatch_queue_create("com.apple.DiskManagement.DMManager", NULL);
	if (!operationLock || !pending || !session || !queue) {
		[self release];
		return nil;
	}
	DASessionSetDispatchQueue(session, queue);
	return self;
}

- (void)dealloc
{
	if (session) {
		/* Stop delivery before releasing the session; the dispatch source
		 * cancellation handler releases its own reference asynchronously. */
		DASessionSetDispatchQueue(session, NULL);
		CFRelease(session);
		session = NULL;
	}
	if (queue) {
		dispatch_release(queue);
		queue = NULL;
	}
	if (pending) {
		DMOperationContext *contexts = NULL;
		pthread_mutex_lock(&pending->lock);
		contexts = pending->head;
		pending->head = NULL;
		pthread_mutex_unlock(&pending->lock);
		while (contexts) {
			DMOperationContext *next = contexts->next;
			contexts->next = NULL;
			DMContextRelease(contexts);
			contexts = next;
		}
		DMPendingRelease(pending);
		pending = NULL;
	}
	[operationLock release];
	[super dealloc];
}

@end

@implementation DMManager

+ (instancetype)sharedManagerForThread
{
	NSMutableDictionary *thread = [[NSThread currentThread] threadDictionary];
	NSString *key = @"com.apple.DiskManagement.DMManager.thread";
	DMManager *manager = [thread objectForKey:key];
	if (!manager) {
		manager = [[[DMManager alloc] init] autorelease];
		if (manager) [thread setObject:manager forKey:key];
	}
	return manager;
}

- (instancetype)init
{
	self = [super init];
	if (!self) return nil;
	_instPriv = [[DMManagerPrivateVars alloc] init];
	if (!_instPriv) { [self release]; return nil; }
	return self;
}

- (void)dealloc
{
	[_instPriv release];
	[super dealloc];
}

- (BOOL)finishOperation:(DMOperationContext *)context timeout:(double)timeout
	errorDict:(NSDictionary **)errorDict
{
	BOOL timedOut = NO;
	DAReturn status = kDAReturnError;
	CFStringRef message = NULL;
	BOOL success = DMWaitForCompletion(context, timeout, &timedOut, &status, &message);
	DMContextRelease(context);
	if (errorDict) *errorDict = nil;
	if (!success && errorDict)
		*errorDict = DMCreateErrorDictionary(timedOut, message);
	if (message) CFRelease(message);
	return success;
}

- (BOOL)mountDisk:(DADiskRef)disk
	entireDisk:(BOOL)entireDisk
	   atPath:(NSString *)path
	 readOnly:(BOOL)readOnly
 otherArguments:(NSArray *)otherArguments
	  timeout:(double)timeout
	errorDict:(NSDictionary **)errorDict
{
	if (errorDict) *errorDict = nil;
	if (!disk || !path) {
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Invalid disk or mount path"));
		return NO;
	}
	/* The opened-image backend presents read-only media and publishes
	 * MediaWritable=false. A writable request cannot be honored truthfully. */
	if (!readOnly) {
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Writable image mounts are not supported"));
		return NO;
	}
	CFURLRef url = CFURLCreateWithFileSystemPath(NULL, (CFStringRef)path,
		kCFURLPOSIXPathStyle, true);
	if (!url) {
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Invalid mount path"));
		return NO;
	}
	CFMutableArrayRef arguments = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	if (!arguments) {
		CFRelease(url);
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Out of memory"));
		return NO;
	}
	CFArrayAppendValue(arguments, CFSTR("rdonly"));
	for (id argument in otherArguments) {
		if ([argument isKindOfClass:[NSString class]]) CFArrayAppendValue(arguments, argument);
		else if (argument) CFArrayAppendValue(arguments, (CFTypeRef)argument);
	}
	CFIndex argumentCount = CFArrayGetCount(arguments);
	CFStringRef *argumentList = calloc((size_t)argumentCount + 1, sizeof(*argumentList));
	if (!argumentList) {
		CFRelease(arguments);
		CFRelease(url);
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Out of memory"));
		return NO;
	}
	for (CFIndex i = 0; i < argumentCount; ++i)
		argumentList[i] = CFArrayGetValueAtIndex(arguments, i);

	DMManagerPrivateVars *priv = (DMManagerPrivateVars *)_instPriv;
	[priv->operationLock lock];
	const char *bsdName = DADiskGetBSDName(disk);
	DADiskRef operationDisk = bsdName ? DADiskCreateFromBSDName(NULL, priv->session, bsdName) : NULL;
	if (!operationDisk) {
		[priv->operationLock unlock];
		free(argumentList);
		CFRelease(arguments);
		CFRelease(url);
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Cannot resolve the disk on the operation session"));
		return NO;
	}
	DMOperationContext *context = DMContextCreate(priv->pending);
	if (!context) {
		CFRelease(operationDisk);
		[priv->operationLock unlock];
		free(argumentList);
		CFRelease(arguments);
		CFRelease(url);
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Out of memory"));
		return NO;
	}
	DADiskMountOptions options = entireDisk ? kDADiskMountOptionWhole : kDADiskMountOptionDefault;
	DADiskMountWithArguments(operationDisk, url, options, DMOperationFinished, context,
		(const CFStringRef *)argumentList);
	CFRelease(operationDisk);
	free(argumentList);
	CFRelease(arguments);
	CFRelease(url);
	BOOL success = [self finishOperation:context timeout:timeout errorDict:errorDict];
	[priv->operationLock unlock];
	return success;
}

- (BOOL)unmountVolumePath:(NSString *)path
		    force:(BOOL)force
		errorDict:(NSDictionary **)errorDict
{
	if (errorDict) *errorDict = nil;
	if (!path) {
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Invalid volume path"));
		return NO;
	}
	CFURLRef url = CFURLCreateWithFileSystemPath(NULL, (CFStringRef)path,
		kCFURLPOSIXPathStyle, true);
	if (!url) {
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Invalid volume path"));
		return NO;
	}
	DMManagerPrivateVars *priv = (DMManagerPrivateVars *)_instPriv;
	[priv->operationLock lock];
	DADiskRef disk = DADiskCreateFromVolumePath(NULL, priv->session, url);
	CFRelease(url);
	if (!disk) {
		[priv->operationLock unlock];
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("No disk is mounted at that path"));
		return NO;
	}
	DMOperationContext *context = DMContextCreate(priv->pending);
	if (!context) {
		CFRelease(disk);
		[priv->operationLock unlock];
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Out of memory"));
		return NO;
	}
	DADiskUnmountOptions options = force ? kDADiskUnmountOptionForce : kDADiskUnmountOptionDefault;
	DADiskUnmount(disk, options, DMOperationFinished, context);
	CFRelease(disk);
	BOOL success = [self finishOperation:context timeout:0 errorDict:errorDict];
	[priv->operationLock unlock];
	return success;
}

- (BOOL)ejectDisk:(DADiskRef)disk errorDict:(NSDictionary **)errorDict
{
	if (errorDict) *errorDict = nil;
	if (!disk) {
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Invalid disk"));
		return NO;
	}
	DMManagerPrivateVars *priv = (DMManagerPrivateVars *)_instPriv;
	[priv->operationLock lock];
	const char *bsdName = DADiskGetBSDName(disk);
	DADiskRef operationDisk = bsdName ? DADiskCreateFromBSDName(NULL, priv->session, bsdName) : NULL;
	if (!operationDisk) {
		[priv->operationLock unlock];
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Cannot resolve the disk on the operation session"));
		return NO;
	}
	DMOperationContext *context = DMContextCreate(priv->pending);
	if (!context) {
		CFRelease(operationDisk);
		[priv->operationLock unlock];
		if (errorDict) *errorDict = DMCreateErrorDictionary(NO, CFSTR("Out of memory"));
		return NO;
	}
	DADiskEject(operationDisk, kDADiskEjectOptionDefault, DMOperationFinished, context);
	CFRelease(operationDisk);
	BOOL success = [self finishOperation:context timeout:0 errorDict:errorDict];
	[priv->operationLock unlock];
	return success;
}

@end
