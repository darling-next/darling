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

#ifndef _OSLOG_POSITION_H_
#define _OSLOG_POSITION_H_

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * A point in a store's range of entries, produced by an OSLogStore and handed back to it.
 *
 * The three kinds Apple's store supports are all here and all resolved against real values: an
 * absolute date, an offset from the end of the store's own range of entries, and an offset from
 * the latest boot (read from the kernel's boot time).
 */
@interface OSLogPosition : NSObject

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END

#endif /* _OSLOG_POSITION_H_ */
