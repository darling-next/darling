/*
 This file is part of Darling.

 Copyright (C) 2019-2020 Lubos Dolezel

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

#include <FSEvents/FSEvents.h>
#include <stdio.h>
#include <stdlib.h>
#import "FSEventsImpl.h"

FSEventStreamRef FSEventStreamCreate(
		CFAllocatorRef allocator,
		FSEventStreamCallback callback,
		FSEventStreamContext *context,
		CFArrayRef pathsToWatch,
		FSEventStreamEventId sinceWhen,
		CFTimeInterval latency,
		FSEventStreamCreateFlags flags)
{
	return (FSEventStreamRef) [[FSEventsImpl alloc] initWithPaths: (NSArray*)pathsToWatch
									flags: flags
									context: context
									callback: callback];
}

extern FSEventStreamRef FSEventStreamCreateRelativeToDevice(
		CFAllocatorRef allocator,
		FSEventStreamCallback callback,
		FSEventStreamContext *context,
		dev_t deviceToWatch,
		CFArrayRef pathsToWatchRelativeToDevice,
		FSEventStreamEventId sinceWhen,
		CFTimeInterval latency,
		FSEventStreamCreateFlags flags)
{
	printf("STUB %s\n", __PRETTY_FUNCTION__);
    return nil;
}

CFArrayRef FSEventStreamCopyPathsBeingWatched(ConstFSEventStreamRef streamRef)
{
	FSEventsImpl* impl = (FSEventsImpl*) streamRef;
	return (CFArrayRef) [impl copyPathsToWatch];
}

FSEventStreamEventId FSEventStreamGetLatestEventId(ConstFSEventStreamRef streamRef)
{
	return [((FSEventsImpl*) streamRef) lastEventID];
}

/*
 * The two flush entry points. Both are called by DevToolsCore's DTFileSystemEventStream (flush ->
 * FSEventStreamFlushAsync at 0x16376e, flushSync -> FSEventStreamFlushSync at 0x16377c, each a tail
 * call passing the stream).
 *
 * The stream is the inotify descriptor below, not fseventsd: this implementation reads the kernel's
 * inotify queue on its own dispatch source and delivers a callback per batch, and the `latency`
 * FSEventStreamCreate is given is not used to delay anything. So there is no daemon-side buffer to
 * ask for -- the answer each client needs is derivable here, and it is:
 *
 *   FlushAsync  returns the largest event id of any event ever queued for this stream, which is
 *               exactly what this implementation's monotonically increasing counter holds
 *               (_lastEventID, one past the last event appended to _idArray). Asynchronous by
 *               contract, it must not wait, and there is nothing to wait for: the source handler
 *               delivers the events it has read without further prompting.
 *   FlushSync   must have invoked the client callback for every event that had already occurred by
 *               the time it returns, so it reads and delivers on the caller's thread
 *               (FSEventsImpl -flushSynchronously).
 *
 * The stream reference is released by FSEventStreamRelease and invalidated by FSEventStreamInvalidate,
 * as for every other entry point here.
 */
FSEventStreamEventId FSEventStreamFlushAsync(FSEventStreamRef streamRef)
{
	return [((FSEventsImpl*) streamRef) lastEventID];
}

void FSEventStreamFlushSync(FSEventStreamRef streamRef)
{
	[((FSEventsImpl*) streamRef) flushSynchronously];
}

void FSEventStreamInvalidate(FSEventStreamRef streamRef)
{
	[((FSEventsImpl*) streamRef) invalidate];
}

void FSEventStreamRelease(FSEventStreamRef streamRef)
{
	[((FSEventsImpl*) streamRef) release];
}

void FSEventStreamRetain(FSEventStreamRef streamRef)
{
	[((FSEventsImpl*) streamRef) retain];
}

void FSEventStreamScheduleWithRunLoop(FSEventStreamRef streamRef, CFRunLoopRef runLoop, CFStringRef runLoopMode)
{
	[((FSEventsImpl*) streamRef) scheduleWithRunLoop: runLoop
												mode: runLoopMode];
}

void FSEventStreamSetDispatchQueue(FSEventStreamRef streamRef, dispatch_queue_t q)
{
	[((FSEventsImpl*) streamRef) setDispatchQueue: q];
}

Boolean FSEventStreamStart(FSEventStreamRef streamRef)
{
	[((FSEventsImpl*) streamRef) start];
	return TRUE;
}

void FSEventStreamStop(FSEventStreamRef streamRef)
{
	[((FSEventsImpl*) streamRef) stop];
}

void FSEventStreamUnscheduleFromRunLoop(FSEventStreamRef streamRef, CFRunLoopRef runLoop, CFStringRef runLoopMode)
{
	[((FSEventsImpl*) streamRef) unscheduleWithRunLoop: runLoop
												mode: runLoopMode];
}
