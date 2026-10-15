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

@implementation OSLogEntry

- (instancetype)init
{
	return [self _initWithFields:[[_OSLogEntryFields alloc] init]];
}

- (instancetype)_initWithFields:(_OSLogEntryFields *)fields
{
	self = [super init];
	if (self) {
		self.fields = fields;
	}
	return self;
}

- (NSString *)composedMessage
{
	return self.fields.composedMessage;
}

- (NSDate *)date
{
	return self.fields.date;
}

- (OSLogEntryStoreCategory)storeCategory
{
	return self.fields.storeCategory;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p %@ %@>",
		NSStringFromClass([self class]), self, self.fields.date, self.fields.composedMessage];
}

#pragma mark - NSCoding

/* The whole of an entry is its fields object, so the base class implements NSCoding for every
 * subclass: a subclass has no state of its own to encode (see OSLogPrivate.h). */
+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:self.fields forKey:@"fields"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	_OSLogEntryFields *fields = [coder decodeObjectOfClass:[_OSLogEntryFields class] forKey:@"fields"];
	if (fields == nil) {
		return nil;
	}

	return [self _initWithFields:fields];
}

@end
