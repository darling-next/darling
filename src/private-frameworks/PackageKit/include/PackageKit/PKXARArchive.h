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

#import <PackageKit/PKArchive.h>

struct __xar_t;

/*
 * Flat XAR archive reader backed by the vendored libxar C API.
 *
 * The archive is opened lazily and closed again by -closeArchive, matching
 * PKPackage's factory contract: the table-of-contents digest is captured at
 * first open so it stays available after the handle is closed.
 */
@interface PKXARArchive : PKArchive
{
@private
    NSString *_path;
    NSData *_tocDigest;
    const struct __xar_t *_xar;
    BOOL _skipsVerifyIfUnsigned;
}

- (instancetype) initForReadingFromPath: (NSString *) path;
- (instancetype) initForReadingFromPath: (NSString *) path options: (NSDictionary *) options;

- (NSString *) archivePath;
- (BOOL) closeArchive;
- (NSData *) dataForPath: (NSString *) path;
- (NSData *) archiveDigestData;
- (BOOL) fileExistsAtPath: (NSString *) path;

/* Member checksum verification is skipped for unsigned archives when set. */
- (void) _setSkipsVerifyIfUnsigned: (BOOL) skip;

@end
