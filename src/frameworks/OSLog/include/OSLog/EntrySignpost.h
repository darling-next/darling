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

#ifndef _OSLOG_ENTRY_SIGNPOST_H_
#define _OSLOG_ENTRY_SIGNPOST_H_

#import <OSLog/Entry.h>
#import <os/signpost.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, OSLogEntrySignpostType) {
    OSLogEntrySignpostTypeUndefined = 0,
    OSLogEntrySignpostTypeIntervalBegin,
    OSLogEntrySignpostTypeIntervalEnd,
    OSLogEntrySignpostTypeEvent,
};

@interface OSLogEntrySignpost : OSLogEntry <OSLogEntryFromProcess, OSLogEntryWithPayload>

@property (nonatomic, readonly) os_signpost_id_t signpostIdentifier;
@property (nonatomic, readonly) NSString *signpostName;
@property (nonatomic, readonly) OSLogEntrySignpostType signpostType;

@end

NS_ASSUME_NONNULL_END

#endif /* _OSLOG_ENTRY_SIGNPOST_H_ */
