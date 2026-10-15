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

/* A log entry: the composed message plus the process and payload fields of the log the message
 * was sent to.  Every value comes from the record OSLogStore read (see OSLogEntryFromRecord.m),
 * including the ones that are empty because the record does not carry them. */
@implementation OSLogEntryLog

- (OSLogEntryLogLevel)level
{
	return self.fields.level;
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
