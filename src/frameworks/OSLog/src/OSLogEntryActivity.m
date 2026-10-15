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

/* An activity entry: the boundary of an os_activity scope, with the activity it belongs to and the
 * one that contains it.  Like OSLogEntrySignpost, no record this tree produces is an activity
 * record (the ASL record the os_log bridge writes carries no activity key), so an OSLogStore here
 * yields none; the values below are the ones the initializer was given. */
@implementation OSLogEntryActivity

- (os_activity_id_t)parentActivityIdentifier
{
	return self.fields.parentActivityIdentifier;
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

@end
