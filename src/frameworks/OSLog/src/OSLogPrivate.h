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

/* Internal to OSLog.framework: the entry storage every entry class shares, the store's private
 * state, and the one conversion from an ASL record to an OSLogEntry.  Nothing here is exported. */

#ifndef _OSLOG_PRIVATE_H_
#define _OSLOG_PRIVATE_H_

#import <Foundation/Foundation.h>

#import <OSLog/OSLog.h>

#include <asl.h>

NS_ASSUME_NONNULL_BEGIN

/* The ASL key libsystem_trace's _os_log_impl marks its records with (os_log.c, "os_log(3)"):
 * the store's query selects on it, and every record in a store is one. */
#define OSLOG_ASL_MARKER_KEY "os_log(3)"

/* The ASL facility the os_log bridge writes through, which is the facility an OSLogStore's local
 * client opens (os_log.c's get_client()). */
#define OSLOG_ASL_FACILITY "org.puredarwin.os_log"

/* Every value an entry can carry.  The entry classes hold one of these instead of one ivar per
 * field, which is also what makes NSSecureCoding a single implementation (see OSLogEntry.m). */
@interface _OSLogEntryFields : NSObject <NSSecureCoding>
@end

@interface _OSLogEntryFields ()

@property (nonatomic, copy) NSString *composedMessage;
@property (nonatomic, strong) NSDate *date;
@property (nonatomic) OSLogEntryStoreCategory storeCategory;

@property (nonatomic, copy) NSString *subsystem;
@property (nonatomic, copy) NSString *category;
@property (nonatomic, copy) NSString *formatString;
@property (nonatomic, copy) NSArray<OSLogMessageComponent *> *components;
@property (nonatomic) OSLogEntryLogLevel level;

@property (nonatomic, copy) NSString *signpostName;
@property (nonatomic) uint64_t signpostIdentifier;
@property (nonatomic) OSLogEntrySignpostType signpostType;

@property (nonatomic) os_activity_id_t activityIdentifier;
@property (nonatomic) os_activity_id_t parentActivityIdentifier;

@property (nonatomic, copy) NSString *process;
@property (nonatomic) pid_t processIdentifier;
@property (nonatomic, copy) NSString *sender;
@property (nonatomic) uint64_t threadIdentifier;

@end

/* The three position kinds Apple's store supports, and the value each one resolves against. */
typedef NS_ENUM(NSInteger, OSLogPositionKind) {
	OSLogPositionKindDate = 0,
	OSLogPositionKindIntervalSinceEnd,
	OSLogPositionKindIntervalSinceLatestBoot,
};

/* The private interfaces below declare PROPERTIES rather than instance variables on purpose.  This
 * tree's Objective-C compiles with an ivar layout that rejects an ivar block in an @implementation
 * ("inconsistent number of instance variables specified") and rejects ivars in a class extension
 * ("instance variables may not be placed in class extension"), both measured 2026-10-15 with the
 * framework's own compile flags; a property in a class extension is the one shape that compiles
 * and still stores per-object state (its ivar is synthesized into the class's own layout). */
@interface OSLogPosition ()

@property (nonatomic) OSLogPositionKind kind;
@property (nonatomic) NSTimeInterval interval;
@property (nonatomic, nullable) NSDate *absoluteDate;

- (instancetype)_initWithKind:(OSLogPositionKind)kind
                     interval:(NSTimeInterval)interval
                         date:(nullable NSDate *)date;

/* The instant this position names, given the end of the range of entries it is being used with
 * (which is what an interval-since-end resolves against).  Nil when it cannot be resolved -- an
 * unreadable boot time, or an interval-since-end used without a range. */
- (nullable NSDate *)_resolvedDateWithRangeEnd:(nullable NSDate *)rangeEnd;

@end

@interface OSLogEnumerator ()

/* The enumerator over `entries`, in the order given (the store has already applied the options). */
@property (nonatomic, copy) NSArray<OSLogEntry *> *entries;
@property (nonatomic) NSUInteger index;

- (instancetype)_initWithEntries:(NSArray<OSLogEntry *> *)entries;

@end

@interface OSLogEntry ()

/* The values the entry carries.  One storage object rather than one property per field, which is
 * what makes NSSecureCoding a single implementation (see OSLogEntry.m). */
@property (nonatomic, strong) _OSLogEntryFields *fields;

- (instancetype)_initWithFields:(_OSLogEntryFields *)fields;

@end

@interface OSLogStore ()

/* `sources` holds asl_object_t values (NSValue valueWithPointer:), each one retained by the store
 * and released in -dealloc. */
@property (nonatomic, copy) NSArray<NSValue *> *sources;
@property (nonatomic) OSLogStoreScope scope;

- (instancetype)_initWithSources:(NSArray<NSValue *> *)sources
                           scope:(OSLogStoreScope)scope;

/* The store's whole matching set, as entries, in ascending date order.  This is what both the
 * enumerator and the range end for an interval-since-end position are built from. */
- (nullable NSArray<OSLogEntry *> *)_entriesWithError:(NSError **)error;

@end

/* The conversion of one ASL record.  The class of the result is the one the record's own shape
 * names: OSLogEntryLog for the records libsystem_trace's os_log bridge writes. */
OSLogEntry *OSLogEntryFromASLMessage(asl_object_t message);

/* The record's value for `key` as an NSString, or nil when the record has no such key. */
NSString * _Nullable OSLogStringFromASLMessage(asl_object_t message, const char *key);

NS_ASSUME_NONNULL_END

#endif /* _OSLOG_PRIVATE_H_ */
