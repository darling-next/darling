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

#ifndef _OSLOG_STORE_H_
#define _OSLOG_STORE_H_

#import <Foundation/Foundation.h>

@class OSLogEnumerator;
@class OSLogPosition;

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, OSLogStoreScope) {
    OSLogStoreSystem = 0,
    OSLogStoreCurrentProcessIdentifier = 1,
};

/**
 * A set of entries from the unified logging system.
 *
 * WHAT BACKS THIS STORE IN DARLING.  Apple's store reads the tracev3 unified log: a directory of
 * persist/timesync files that `libtrace` writes through the kernel's firehose, read back through
 * LoggingSupport's index and chunk readers.  This tree has neither half of that: libsystem_trace's
 * `_os_log_impl` (src/external/libtrace/libsystem_trace/os_log.c) writes each message into ASL
 * (asl_log to the "org.puredarwin.os_log" client, with the "os_log(3)" marker, the Subsystem and
 * Category of the log, the hex of the argument buffer, and the decoded message), and
 * LoggingSupport's readers are forwarding stubs -- MEASURED 2026-10-15: every file of
 * src/private-frameworks/LoggingSupport/src/_OSLog*.m, OSLogEventStore.m, OSLogEventStream.m and
 * OSLogEventProxy.m is a 32-line methodSignatureForSelector:/forwardInvocation: stub, in this tree
 * and in the VibeDarling reference checkout alike.
 *
 * So the store reads the log store Darling actually has: the ASL database, through the same
 * libsystem_asl the writer uses (libsystem_asl is Apple's real syslog implementation, 14.5k lines
 * in src/external/syslog).  That is what makes the values below real rather than invented --
 * Time/TimeNanoSec, Sender, PID, Level and Message come out of the record libsystem_trace wrote.
 *
 * WHAT IT CANNOT ANSWER, and why: the ASL record carries the composed message and the hex of the
 * argument buffer, not the format string, so `formatString` cannot be recovered and `components`
 * is empty for every entry this store yields; the ASL record has no thread id; and the ASL
 * database has no store tiers, so `storeCategory` is OSLogEntryStoreCategoryUndefined.  Each of
 * those is stated where the value is produced rather than filled with a plausible constant.
 */
@interface OSLogStore : NSObject

/**
 * Create a store over the local unified log.
 *
 * Returns nil and sets `error` when the local store cannot be opened, which is Apple's own
 * documented failure for a process without access to local logs.  In Darling the local store is
 * the ASL database: when no ASL database directory exists and no ASL server is running, there is
 * no local log to read and this returns nil with an error saying so.
 */
+ (nullable instancetype)localStoreAndReturnError:(NSError **)error;

/** Create a store over a subset of the local store: everything, or this process's entries. */
+ (nullable instancetype)storeWithScope:(OSLogStoreScope)scope error:(NSError **)error;

/**
 * Create a store based on a logarchive.
 *
 * Darling note: the ASL database is the only record format this tree can read end to end, so a
 * URL naming an ASL database file (.asl) or a directory of them opens; a URL naming a tracev3
 * logarchive returns nil with an error that says the format has no reader here.
 */
+ (nullable instancetype)storeWithURL:(NSURL *)url error:(NSError **)error;

- (instancetype)init API_DEPRECATED("Use one of the factory methods", macos(10.15, 12.0));

- (nullable OSLogEnumerator *)entriesEnumeratorWithOptions:(OSLogEnumeratorOptions)options
                                                  position:(nullable OSLogPosition *)position
                                                 predicate:(nullable NSPredicate *)predicate
                                                     error:(NSError **)error;

- (nullable OSLogEnumerator *)entriesEnumeratorAndReturnError:(NSError **)error;

- (OSLogPosition *)positionWithDate:(NSDate *)date;
- (OSLogPosition *)positionWithTimeIntervalSinceEnd:(NSTimeInterval)seconds;
- (OSLogPosition *)positionWithTimeIntervalSinceLatestBoot:(NSTimeInterval)seconds;

@end

NS_ASSUME_NONNULL_END

#endif /* _OSLOG_STORE_H_ */
