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

/* The sequence of entries a store yields.  See Enumerator.h for why this walks a snapshot rather
 * than streaming out of the store: the ASL search behind the store hands back the whole matching
 * set in one call, so there is nothing left to stream.  -nextObject, -allObjects and fast
 * enumeration all walk that array. */
@implementation OSLogEnumerator

- (instancetype)_initWithEntries:(NSArray<OSLogEntry *> *)entries
{
	self = [super init];
	if (self) {
		self.entries = entries;
		self.index = 0;
	}
	return self;
}

- (id)nextObject
{
	if (self.index >= self.entries.count) {
		return nil;
	}

	return self.entries[self.index++];
}

- (NSArray<OSLogEntry *> *)allObjects
{
	NSArray<OSLogEntry *> *rest = [self.entries subarrayWithRange:
		NSMakeRange(self.index, self.entries.count - self.index)];
	self.index = self.entries.count;
	return rest;
}

@end
