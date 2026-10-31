/*
This file is part of Darling.

Copyright (C) 2017 Lubos Dolezel

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

#import <Foundation/Foundation.h>

@class PKXARArchive;

/*
 * Reading-side archive abstraction used by PKPackage.
 *
 * Only the flat XAR provider (PKXARArchive) is implemented, so
 * +_allArchiveClasses returns only that class; PKFolderArchive (a
 * directory-backed archive) is not implemented and is deliberately not
 * offered as a reader that cannot open anything.
 *
 * "Validated digest" below is the XAR table-of-contents checksum and
 * "verification" is the XAR member checksum.  Neither authenticates a
 * package: no method here evaluates signatures, certificates, Apple trust
 * anchors or an installation policy.  Trust, authorization, preflight and
 * installation are separate, unimplemented services.
 */

/* NSDictionary key: NSData holding the expected XAR table-of-contents digest. */
extern NSString * const PKArchiveOpenOptionValidatedDigest;

@interface PKArchive : NSObject

+ (instancetype) archiveWithPath: (NSString *) path;
+ (instancetype) archiveWithPath: (NSString *) path options: (NSDictionary *) options;

/* The concrete archive readers, in the order the factory tries them. */
+ (NSArray *) _allArchiveClasses;

/* Abstract in this base class; concrete subclasses implement them. */
- (NSString *) archivePath;
- (BOOL) closeArchive;
- (NSData *) dataForPath: (NSString *) path;

/* XAR table-of-contents checksum, or nil when the archive has none. */
- (NSData *) archiveDigestData;

@end
