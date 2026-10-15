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

@implementation _OSLogEntryFields

+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (instancetype)init
{
	self = [super init];
	if (self) {
		self.composedMessage = @"";
		self.date = [NSDate dateWithTimeIntervalSince1970:0];
		self.formatString = @"";
		self.components = @[];
		self.process = @"";
		self.sender = @"";
		self.subsystem = @"";
		self.category = @"";
		self.signpostName = @"";
	}
	return self;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:self.composedMessage forKey:@"composedMessage"];
	[coder encodeObject:self.date forKey:@"date"];
	[coder encodeInteger:self.storeCategory forKey:@"storeCategory"];
	[coder encodeObject:self.subsystem forKey:@"subsystem"];
	[coder encodeObject:self.category forKey:@"category"];
	[coder encodeObject:self.formatString forKey:@"formatString"];
	[coder encodeObject:self.components forKey:@"components"];
	[coder encodeInteger:self.level forKey:@"level"];
	[coder encodeObject:self.signpostName forKey:@"signpostName"];
	[coder encodeInt64:(int64_t)self.signpostIdentifier forKey:@"signpostIdentifier"];
	[coder encodeInteger:self.signpostType forKey:@"signpostType"];
	[coder encodeInt64:(int64_t)self.activityIdentifier forKey:@"activityIdentifier"];
	[coder encodeInt64:(int64_t)self.parentActivityIdentifier forKey:@"parentActivityIdentifier"];
	[coder encodeObject:self.process forKey:@"process"];
	[coder encodeInt32:self.processIdentifier forKey:@"processIdentifier"];
	[coder encodeObject:self.sender forKey:@"sender"];
	[coder encodeInt64:(int64_t)self.threadIdentifier forKey:@"threadIdentifier"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super init];
	if (self) {
		self.composedMessage = [coder decodeObjectOfClass:[NSString class] forKey:@"composedMessage"];
		self.date = [coder decodeObjectOfClass:[NSDate class] forKey:@"date"];
		self.storeCategory = (OSLogEntryStoreCategory)[coder decodeIntegerForKey:@"storeCategory"];
		self.subsystem = [coder decodeObjectOfClass:[NSString class] forKey:@"subsystem"];
		self.category = [coder decodeObjectOfClass:[NSString class] forKey:@"category"];
		self.formatString = [coder decodeObjectOfClass:[NSString class] forKey:@"formatString"];
		self.components = [coder decodeObjectOfClasses:
			[NSSet setWithObjects:[NSArray class], [OSLogMessageComponent class], nil]
			forKey:@"components"];
		self.level = (OSLogEntryLogLevel)[coder decodeIntegerForKey:@"level"];
		self.signpostName = [coder decodeObjectOfClass:[NSString class] forKey:@"signpostName"];
		self.signpostIdentifier = (uint64_t)[coder decodeInt64ForKey:@"signpostIdentifier"];
		self.signpostType = (OSLogEntrySignpostType)[coder decodeIntegerForKey:@"signpostType"];
		self.activityIdentifier = (os_activity_id_t)[coder decodeInt64ForKey:@"activityIdentifier"];
		self.parentActivityIdentifier = (os_activity_id_t)[coder decodeInt64ForKey:@"parentActivityIdentifier"];
		self.process = [coder decodeObjectOfClass:[NSString class] forKey:@"process"];
		self.processIdentifier = (pid_t)[coder decodeInt32ForKey:@"processIdentifier"];
		self.sender = [coder decodeObjectOfClass:[NSString class] forKey:@"sender"];
		self.threadIdentifier = (uint64_t)[coder decodeInt64ForKey:@"threadIdentifier"];

		/* A record that carries nothing is still a record: keep the same non-nil defaults -init
		 * gives, rather than handing a nil back through a nonnull property. */
		if (self.composedMessage == nil) self.composedMessage = @"";
		if (self.subsystem == nil) self.subsystem = @"";
		if (self.category == nil) self.category = @"";
		if (self.formatString == nil) self.formatString = @"";
		if (self.components == nil) self.components = @[];
		if (self.process == nil) self.process = @"";
		if (self.sender == nil) self.sender = @"";
		if (self.signpostName == nil) self.signpostName = @"";
		if (self.date == nil) self.date = [NSDate dateWithTimeIntervalSince1970:0];
	}
	return self;
}

@end
