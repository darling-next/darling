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

#ifndef _OSLOG_MESSAGE_COMPONENT_H_
#define _OSLOG_MESSAGE_COMPONENT_H_

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, OSLogMessageComponentArgumentCategory) {
    OSLogMessageComponentArgumentCategoryUndefined = 0,
    OSLogMessageComponentArgumentCategoryData,
    OSLogMessageComponentArgumentCategoryDouble,
    OSLogMessageComponentArgumentCategoryInt64,
    OSLogMessageComponentArgumentCategoryString,
    OSLogMessageComponentArgumentCategoryUInt64,
};

/**
 * One piece of a decomposed log message: the literal text between two placeholders, or a
 * placeholder together with the value the caller passed for it.
 */
@interface OSLogMessageComponent : NSObject <NSSecureCoding>

@property (nonatomic, readonly) NSString *formatSubstring;
@property (nonatomic, readonly) NSString *placeholder;
@property (nonatomic, readonly) OSLogMessageComponentArgumentCategory argumentCategory;
@property (nonatomic, readonly, nullable) NSData *argumentDataValue;
@property (nonatomic, readonly) double argumentDoubleValue;
@property (nonatomic, readonly) int64_t argumentInt64Value;
@property (nonatomic, readonly, nullable) NSNumber *argumentNumberValue;
@property (nonatomic, readonly, nullable) NSString *argumentStringValue;
@property (nonatomic, readonly) uint64_t argumentUInt64Value;

@end

NS_ASSUME_NONNULL_END

#endif /* _OSLOG_MESSAGE_COMPONENT_H_ */
