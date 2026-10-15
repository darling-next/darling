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

/* The ASL record keys this conversion reads.  The first three are set by libsystem_trace's
 * _os_log_impl (src/external/libtrace/libsystem_trace/os_log.c: "os_log(3)", "Subsystem",
 * "Category"); the rest are ASL's own, set by asl_base_msg for every record it writes. */
static const char *const OSLogASLSubsystemKey = "Subsystem";
static const char *const OSLogASLCategoryKey = "Category";

NSString *OSLogStringFromASLMessage(asl_object_t message, const char *key)
{
	const char *value = asl_get(message, key);

	return value != NULL ? [NSString stringWithUTF8String:value] : nil;
}

/* The record's time: ASL's "Time" is whole seconds since the epoch and "TimeNanoSec" the
 * nanoseconds within that second, both written by asl_base_msg.  A record with no time cannot be
 * placed in the store's range, so it is not an entry. */
static NSDate *OSLogDateFromASLMessage(asl_object_t message)
{
	NSString *seconds = OSLogStringFromASLMessage(message, ASL_KEY_TIME);
	if (seconds == nil) {
		return nil;
	}

	NSTimeInterval interval = seconds.doubleValue;
	NSString *nanoseconds = OSLogStringFromASLMessage(message, ASL_KEY_TIME_NSEC);
	if (nanoseconds != nil) {
		interval += nanoseconds.doubleValue / 1000000000.0;
	}

	return [NSDate dateWithTimeIntervalSince1970:interval];
}

/* The log level, from the record's ASL level.
 *
 * MEASURED, and the one place this conversion is lossy: libsystem_trace's bridge writes
 * ASL_LEVEL_INFO for BOTH OS_LOG_TYPE_INFO and OS_LOG_TYPE_DEFAULT (os_log.c's switch), so a
 * message sent at DEFAULT reads back as Info.  Nothing in the record distinguishes them; making it
 * exact needs the os_log type in the record, which is a change to the writer (see the evidence
 * note for this rung, and the bead it files). */
static OSLogEntryLogLevel OSLogLevelFromASLLevel(NSString *level)
{
	switch (level.intValue) {
	case ASL_LEVEL_DEBUG:
		return OSLogEntryLogLevelDebug;

	case ASL_LEVEL_INFO:
		return OSLogEntryLogLevelInfo;

	case ASL_LEVEL_NOTICE:
		return OSLogEntryLogLevelNotice;

	case ASL_LEVEL_WARNING:
	case ASL_LEVEL_ERR:
		return OSLogEntryLogLevelError;

	case ASL_LEVEL_CRIT:
	case ASL_LEVEL_ALERT:
	case ASL_LEVEL_EMERG:
		return OSLogEntryLogLevelFault;

	default:
		return OSLogEntryLogLevelUndefined;
	}
}

OSLogEntry *OSLogEntryFromASLMessage(asl_object_t message)
{
	NSDate *date = OSLogDateFromASLMessage(message);
	if (date == nil) {
		return nil;
	}

	_OSLogEntryFields *fields = [[_OSLogEntryFields alloc] init];
	fields.date = date;
	fields.composedMessage = OSLogStringFromASLMessage(message, ASL_KEY_MSG) ?: @"";

	// ASL's Sender is documented as "the sender's identification string; default is the process
	// name", which is the process that logged; it is also the only process name the record has, so
	// both of the public names for it -- process and sender -- read from it.
	fields.process = OSLogStringFromASLMessage(message, ASL_KEY_SENDER) ?: @"";
	fields.sender = fields.process;

	NSString *pid = OSLogStringFromASLMessage(message, ASL_KEY_PID);
	fields.processIdentifier = pid != nil ? (pid_t)pid.intValue : 0;

	fields.subsystem = OSLogStringFromASLMessage(message, OSLogASLSubsystemKey) ?: @"";
	fields.category = OSLogStringFromASLMessage(message, OSLogASLCategoryKey) ?: @"";
	fields.level = OSLogLevelFromASLLevel(OSLogStringFromASLMessage(message, ASL_KEY_LEVEL));

	// Values the ASL record does not carry, set to the absence the class documents rather than to a
	// plausible constant: the record has no thread id and no activity id (the bridge sets neither),
	// it has the composed message and the hex of the argument buffer but not the format string, so
	// there is nothing to decompose into components, and the ASL database has no store tiers.
	fields.threadIdentifier = 0;
	fields.activityIdentifier = 0;
	fields.parentActivityIdentifier = 0;
	fields.formatString = @"";
	fields.components = @[];
	fields.storeCategory = OSLogEntryStoreCategoryUndefined;

	// A record from the os_log bridge is a log entry.  Signpost, activity and boundary records are
	// the tracev3 shapes, which this tree produces none of (see EntrySignpost.m/EntryActivity.m).
	return [[OSLogEntryLog alloc] _initWithFields:fields];
}
