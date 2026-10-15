/*
 This file is part of Darling.

 Copyright (C) 2026 Darling Team

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

#import "OSLogPrivate.h"

#include <stdio.h>
#include <unistd.h>

/* The error domain and codes this framework reports through.  Apple's own store reports a
 * store-unavailable failure the same way -- nil plus an NSError -- and its description is the part
 * that carries the reason. */
static NSString *const OSLogErrorDomain = @"OSLogErrorDomain";

enum {
	OSLogErrorClientUnavailable = 1,
	OSLogErrorLocalStoreUnavailable = 2,
	OSLogErrorStoreNotSearchable = 3,
	OSLogErrorUnsupportedStore = 4,
	OSLogErrorPositionUnresolved = 5,
	OSLogErrorInvalidPredicate = 6,
	OSLogErrorInvalidArgument = 7,
};

static NSError *OSLogError(NSInteger code, NSString *description)
{
	return [NSError errorWithDomain:OSLogErrorDomain
	                           code:code
	                       userInfo:@{ NSLocalizedDescriptionKey: description }];
}

/* Whether the store behind an ASL object can be searched at all.  This runs the same query the
 * enumerator would: asl_search returns NULL when the store cannot be opened, which is the case the
 * public API documents as "this process does not have access to local logs" -- measured in this
 * environment, where there is no ASL server and no ASL database directory.  A store that opens but
 * holds no os_log record answers with an empty list, and that is a usable store. */
static BOOL OSLogASLSourceIsSearchable(asl_object_t source)
{
	asl_object_t query = asl_new(ASL_TYPE_LIST);
	asl_set_query(query, OSLOG_ASL_MARKER_KEY, "TRUE", ASL_QUERY_OP_EQUAL);

	asl_object_t response = asl_search(source, query);
	asl_release(query);

	if (response == NULL) {
		return NO;
	}

	asl_release(response);
	return YES;
}

@implementation OSLogStore

- (void)dealloc
{
	for (NSValue *source in self.sources) {
		asl_release((asl_object_t)source.pointerValue);
	}
}

- (instancetype)_initWithSources:(NSArray<NSValue *> *)sources scope:(OSLogStoreScope)scope
{
	self = [super init];
	if (self) {
		self.sources = sources;
		self.scope = scope;
	}
	return self;
}

#pragma mark - factories

- (instancetype)init
{
	// The deprecated entry point.  It has no error out-parameter, so an unavailable local store is
	// reported as nil.
	asl_object_t client = asl_open(NULL, OSLOG_ASL_FACILITY, 0);
	if (client == NULL) {
		return nil;
	}

	if (!OSLogASLSourceIsSearchable(client)) {
		asl_release(client);
		return nil;
	}

	return [self _initWithSources:@[ [NSValue valueWithPointer:client] ] scope:OSLogStoreSystem];
}

+ (instancetype)localStoreAndReturnError:(NSError **)error
{
	return [self storeWithScope:OSLogStoreSystem error:error];
}

+ (instancetype)storeWithScope:(OSLogStoreScope)scope error:(NSError **)error
{
	asl_object_t client = asl_open(NULL, OSLOG_ASL_FACILITY, 0);
	if (client == NULL) {
		if (error != NULL) {
			*error = OSLogError(OSLogErrorClientUnavailable,
				@"The local unified log client could not be opened (asl_open failed).");
		}
		return nil;
	}

	if (!OSLogASLSourceIsSearchable(client)) {
		asl_release(client);
		if (error != NULL) {
			*error = OSLogError(OSLogErrorLocalStoreUnavailable,
				@"The local unified log is not available: libsystem_asl found no database directory "
				 "/var/log/asl and no ASL server is running in this environment.");
		}
		return nil;
	}

	return [[self alloc] _initWithSources:@[ [NSValue valueWithPointer:client] ] scope:scope];
}

+ (instancetype)storeWithURL:(NSURL *)url error:(NSError **)error
{
	if (url == nil || !url.isFileURL || url.path == nil) {
		if (error != NULL) {
			*error = OSLogError(OSLogErrorUnsupportedStore,
				@"The store URL must name a file: an ASL database file (.asl) or a directory of them.");
		}
		return nil;
	}

	NSString *path = url.path;
	NSFileManager *fileManager = [NSFileManager defaultManager];
	BOOL isDirectory = NO;
	if (![fileManager fileExistsAtPath:path isDirectory:&isDirectory]) {
		if (error != NULL) {
			*error = OSLogError(OSLogErrorUnsupportedStore,
				[NSString stringWithFormat:@"No store at %@.", path]);
		}
		return nil;
	}

	NSMutableArray<NSValue *> *sources = [NSMutableArray array];

	if (!isDirectory) {
		asl_object_t source = asl_open_path(path.fileSystemRepresentation, 0);
		if (source == NULL) {
			if (error != NULL) {
				*error = OSLogError(OSLogErrorUnsupportedStore,
					[NSString stringWithFormat:@"%@ is not a readable ASL database file.", path]);
			}
			return nil;
		}
		[sources addObject:[NSValue valueWithPointer:source]];
		return [[self alloc] _initWithSources:sources scope:OSLogStoreSystem];
	}

	NSArray<NSString *> *contents = [fileManager contentsOfDirectoryAtPath:path error:NULL] ?: @[];
	for (NSString *name in [contents sortedArrayUsingSelector:@selector(compare:)]) {
		if (![name.pathExtension isEqualToString:@"asl"]) {
			continue;
		}

		NSString *file = [path stringByAppendingPathComponent:name];
		asl_object_t source = asl_open_path(file.fileSystemRepresentation, 0);
		if (source == NULL) {
			for (NSValue *opened in sources) {
				asl_release((asl_object_t)opened.pointerValue);
			}
			if (error != NULL) {
				*error = OSLogError(OSLogErrorUnsupportedStore,
					[NSString stringWithFormat:@"%@ is not a readable ASL database file.", file]);
			}
			return nil;
		}
		[sources addObject:[NSValue valueWithPointer:source]];
	}

	if (sources.count == 0) {
		// The measured shape of a macOS logarchive: a directory of tracev3 persist/timesync files,
		// for which this tree has no reader at all (see Store.h).
		if (error != NULL) {
			*error = OSLogError(OSLogErrorUnsupportedStore,
				[NSString stringWithFormat:@"No ASL record files (.asl) under %@. A tracev3 logarchive "
				 "cannot be read: this tree has no reader for it (LoggingSupport's index and chunk "
				 "readers are forwarding stubs).", path]);
		}
		return nil;
	}

	return [[self alloc] _initWithSources:sources scope:OSLogStoreSystem];
}

#pragma mark - entries

- (NSArray<OSLogEntry *> *)_entriesWithError:(NSError **)error
{
	NSMutableArray<OSLogEntry *> *entries = [NSMutableArray array];
	char pid[24];

	for (NSValue *sourceValue in self.sources) {
		asl_object_t source = (asl_object_t)sourceValue.pointerValue;
		asl_object_t query = asl_new(ASL_TYPE_LIST);

		// Every record in a store is an os_log record, and the scope narrows the set: Apple's
		// CurrentProcessIdentifier scope is "the entries matching the calling process".
		asl_set_query(query, OSLOG_ASL_MARKER_KEY, "TRUE", ASL_QUERY_OP_EQUAL);
		if (self.scope == OSLogStoreCurrentProcessIdentifier) {
			snprintf(pid, sizeof(pid), "%d", (int)getpid());
			asl_set_query(query, ASL_KEY_PID, pid, ASL_QUERY_OP_EQUAL | ASL_QUERY_OP_NUMERIC);
		}

		asl_object_t response = asl_search(source, query);
		asl_release(query);

		if (response == NULL) {
			if (error != NULL) {
				*error = OSLogError(OSLogErrorStoreNotSearchable,
					@"The store could not be searched: its database is not openable.");
			}
			return nil;
		}

		for (asl_object_t record = asl_next(response); record != NULL; record = asl_next(response)) {
			OSLogEntry *entry = OSLogEntryFromASLMessage(record);
			if (entry != nil) {
				[entries addObject:entry];
			}
		}

		asl_release(response);
	}

	[entries sortUsingComparator:^NSComparisonResult(OSLogEntry *left, OSLogEntry *right) {
		return [left.date compare:right.date];
	}];

	return entries;
}

- (OSLogEnumerator *)entriesEnumeratorWithOptions:(OSLogEnumeratorOptions)options
                                         position:(OSLogPosition *)position
                                        predicate:(NSPredicate *)predicate
                                            error:(NSError **)error
{
	NSArray<OSLogEntry *> *entries = [self _entriesWithError:error];
	if (entries == nil) {
		return nil;
	}

	BOOL reverse = (options & OSLogEnumeratorReverse) != 0;

	// An interval-since-end position is an offset from the end of the range of entries, so the
	// range has to be known before the position can be resolved; the entries came back in ascending
	// date order, so its end is the last one.
	if (position != nil) {
		NSDate *cut = [position _resolvedDateWithRangeEnd:entries.lastObject.date];
		if (cut == nil) {
			if (error != NULL) {
				*error = OSLogError(OSLogErrorPositionUnresolved,
					@"The position could not be resolved: an interval-since-end position needs a "
					 "store with entries, and an interval-since-latest-boot position needs the "
					 "kernel's boot time.");
			}
			return nil;
		}

		NSMutableArray<OSLogEntry *> *fromPosition = [NSMutableArray array];
		for (OSLogEntry *entry in entries) {
			NSComparisonResult order = [entry.date compare:cut];
			if (reverse ? order != NSOrderedDescending : order != NSOrderedAscending) {
				[fromPosition addObject:entry];
			}
		}
		entries = fromPosition;
	}

	if (predicate != nil) {
		NSMutableArray<OSLogEntry *> *matching = [NSMutableArray array];
		@try {
			for (OSLogEntry *entry in entries) {
				if ([predicate evaluateWithObject:entry]) {
					[matching addObject:entry];
				}
			}
		} @catch (NSException *exception) {
			// Apple's contract: a predicate naming something an entry does not have fails the
			// enumerator instead of yielding entries.  The keys a predicate can name here are the
			// entry's own properties (subsystem, category, composedMessage, process, ...).
			if (error != NULL) {
				*error = OSLogError(OSLogErrorInvalidPredicate,
					[NSString stringWithFormat:@"The predicate could not be evaluated against the "
					 "entries: %@", exception.reason]);
			}
			return nil;
		}
		entries = matching;
	}

	if (reverse) {
		entries = [[entries reverseObjectEnumerator] allObjects];
	}

	return [[OSLogEnumerator alloc] _initWithEntries:entries];
}

- (OSLogEnumerator *)entriesEnumeratorAndReturnError:(NSError **)error
{
	return [self entriesEnumeratorWithOptions:0 position:nil predicate:nil error:error];
}

#pragma mark - positions

- (OSLogPosition *)positionWithDate:(NSDate *)date
{
	return [[OSLogPosition alloc] _initWithKind:OSLogPositionKindDate interval:0 date:date];
}

- (OSLogPosition *)positionWithTimeIntervalSinceEnd:(NSTimeInterval)seconds
{
	if (seconds < 0) {
		// Apple's contract for this one: the interval is positive, and a negative one would name an
		// ambiguous-or-earlier position.  Nil says so instead of carrying the ambiguity.
		return nil;
	}

	return [[OSLogPosition alloc] _initWithKind:OSLogPositionKindIntervalSinceEnd
	                                   interval:seconds
	                                       date:nil];
}

- (OSLogPosition *)positionWithTimeIntervalSinceLatestBoot:(NSTimeInterval)seconds
{
	if (seconds < 0) {
		return nil;
	}

	return [[OSLogPosition alloc] _initWithKind:OSLogPositionKindIntervalSinceLatestBoot
	                                   interval:seconds
	                                       date:nil];
}

@end
