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

#include <sys/sysctl.h>
#include <sys/time.h>

@implementation OSLogPosition

- (instancetype)_initWithKind:(OSLogPositionKind)kind
                     interval:(NSTimeInterval)interval
                         date:(NSDate *)date
{
	self = [super init];
	if (self) {
		self.kind = kind;
		self.interval = interval;
		self.absoluteDate = date;
	}
	return self;
}

- (NSDate *)_resolvedDateWithRangeEnd:(NSDate *)rangeEnd
{
	switch (self.kind) {
	case OSLogPositionKindDate:
		return self.absoluteDate;

	case OSLogPositionKindIntervalSinceEnd:
		/* The end of the range of entries is the store's own latest entry, so this position needs
		 * the snapshot: it is resolved by the store when the enumerator is built. */
		return rangeEnd != nil ? [rangeEnd dateByAddingTimeInterval:self.interval] : nil;

	case OSLogPositionKindIntervalSinceLatestBoot: {
		struct timeval bootTime;
		size_t size = sizeof(bootTime);

		/* The real boot time, from the kernel, not a stand-in for it. */
		if (sysctl((int[]){ CTL_KERN, KERN_BOOTTIME }, 2, &bootTime, &size, NULL, 0) != 0) {
			return nil;
		}

		NSDate *bootDate = [NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)bootTime.tv_sec
			+ (NSTimeInterval)bootTime.tv_usec / 1000000.0];
		return [bootDate dateByAddingTimeInterval:self.interval];
	}
	}

	return nil;
}

@end
