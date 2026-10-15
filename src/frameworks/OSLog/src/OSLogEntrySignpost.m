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

/* A signpost entry.
 *
 * MEASURED 2026-10-15: no record this tree can produce is a signpost record -- libsystem_trace's
 * signpost entry points (_os_signpost_emit_impl and the rest, src/external/libtrace/libsystem_trace/
 * stubs.c) assert on call, and the ASL record the os_log bridge writes carries no signpost keys --
 * so an OSLogStore in this tree yields no OSLogEntrySignpost.  The class is still the real one: its
 * values are the ones its initializer was given, which is what a store reading signpost records
 * would set. */
@implementation OSLogEntrySignpost

- (os_signpost_id_t)signpostIdentifier
{
	return (os_signpost_id_t)self.fields.signpostIdentifier;
}

- (NSString *)signpostName
{
	return self.fields.signpostName;
}

- (OSLogEntrySignpostType)signpostType
{
	return self.fields.signpostType;
}

#pragma mark - OSLogEntryFromProcess

- (os_activity_id_t)activityIdentifier
{
	return self.fields.activityIdentifier;
}

- (NSString *)process
{
	return self.fields.process;
}

- (pid_t)processIdentifier
{
	return self.fields.processIdentifier;
}

- (NSString *)sender
{
	return self.fields.sender;
}

- (uint64_t)threadIdentifier
{
	return self.fields.threadIdentifier;
}

#pragma mark - OSLogEntryWithPayload

- (NSString *)category
{
	return self.fields.category;
}

- (NSArray<OSLogMessageComponent *> *)components
{
	return self.fields.components;
}

- (NSString *)formatString
{
	return self.fields.formatString;
}

- (NSString *)subsystem
{
	return self.fields.subsystem;
}

@end
