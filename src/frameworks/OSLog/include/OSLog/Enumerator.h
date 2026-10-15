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

#ifndef _OSLOG_ENUMERATOR_H_
#define _OSLOG_ENUMERATOR_H_

#import <Foundation/Foundation.h>

@class OSLogPosition;

NS_ASSUME_NONNULL_BEGIN

typedef NS_OPTIONS(NSUInteger, OSLogEnumeratorOptions) {
    OSLogEnumeratorReverse = 0x01,
};

/**
 * The sequence of entries a store yields.
 *
 * Darling note: Apple's enumerator streams entries lazily out of the store as the logarchive is
 * read.  This framework's store is backed by the ASL database (see Store.h), whose search API
 * returns the whole matching set in one call, so an enumerator here walks the snapshot that the
 * search produced.  -nextObject, -allObjects and fast enumeration are all real; the only
 * difference is when the records are read out of the store, not which records come back.
 */
@interface OSLogEnumerator : NSEnumerator

@end

NS_ASSUME_NONNULL_END

#endif /* _OSLOG_ENUMERATOR_H_ */
