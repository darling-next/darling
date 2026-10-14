/*
 This file is part of Darling.

 Copyright (C) 2023 Darling Team

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

#include <Foundation/Foundation.h>

/* The API surface is the one measured from Apple's own CoreSimulator (extracted from Xcode 26.3's
 * XcodeSystemResources.pkg, sha256 recorded in the workspace's provisioning route) and from the one
 * caller in this guest, Xcode's IDESimulatorFoundation: the class object, the class factory
 * +sharedTrackerWithIdentifier:, the properties identifier/key and the two members the caller reads
 * and writes, -lastUsage and -use. See src/SimUsageTracker.m for the measurement and for why each
 * member answers the way it does. */
@interface SimUsageTracker : NSObject {
	NSString *_identifier;
	NSString *_key;
}

@property (nonatomic, copy) NSString *identifier;
@property (nonatomic, copy) NSString *key;
@property (nonatomic, readonly) NSDate *lastUsage;

+ (instancetype)sharedTrackerWithIdentifier:(NSString *)identifier;
- (instancetype)initWithIdentifier:(NSString *)identifier;

- (void)use;

@end
