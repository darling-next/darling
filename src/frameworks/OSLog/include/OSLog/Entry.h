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

#ifndef _OSLOG_ENTRY_H_
#define _OSLOG_ENTRY_H_

#import <Foundation/Foundation.h>
#import <os/activity.h>

@class OSLogMessageComponent;

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, OSLogEntryStoreCategory) {
    OSLogEntryStoreCategoryUndefined = 0,
    OSLogEntryStoreCategoryMetadata,
    OSLogEntryStoreCategoryShortTerm,
    OSLogEntryStoreCategoryLongTermAuto,
    OSLogEntryStoreCategoryLongTerm1,
    OSLogEntryStoreCategoryLongTerm3,
    OSLogEntryStoreCategoryLongTerm7,
    OSLogEntryStoreCategoryLongTerm14,
    OSLogEntryStoreCategoryLongTerm30,
};

@interface OSLogEntry : NSObject <NSSecureCoding>

@property (nonatomic, readonly) NSString *composedMessage;
@property (nonatomic, readonly) NSDate *date;
@property (nonatomic, readonly) OSLogEntryStoreCategory storeCategory;

@end

@protocol OSLogEntryFromProcess <NSObject>

@property (nonatomic, readonly) os_activity_id_t activityIdentifier;
@property (nonatomic, readonly) NSString *process;
@property (nonatomic, readonly) pid_t processIdentifier;
@property (nonatomic, readonly) NSString *sender;
@property (nonatomic, readonly) uint64_t threadIdentifier;

@end

@protocol OSLogEntryWithPayload <NSObject>

@property (nonatomic, readonly) NSString *category;
@property (nonatomic, readonly) NSArray<OSLogMessageComponent *> *components;
@property (nonatomic, readonly) NSString *formatString;
@property (nonatomic, readonly) NSString *subsystem;

@end

NS_ASSUME_NONNULL_END

#endif /* _OSLOG_ENTRY_H_ */
