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

/*
 * The downloadable identity of a package: URL, identifier, version, size and
 * an optional digest.  This is data only; it neither authenticates the file
 * nor records that any digest was verified.
 */
@interface PKPackageReference : NSObject

- (instancetype) initWithURL: (NSURL *) url
                  identifier: (NSString *) identifier
                     version: (NSString *) version
                    fileSize: (unsigned long long) fileSize
                  fileDigest: (NSString *) fileDigest
              fileDigestType: (NSInteger) fileDigestType;

- (instancetype) initWithURL: (NSURL *) url
                  identifier: (NSString *) identifier
                     version: (NSString *) version
                    fileSize: (unsigned long long) fileSize
                  fileDigest: (NSString *) fileDigest
              fileDigestType: (NSInteger) fileDigestType
              additionalInfo: (NSDictionary *) additionalInfo;

- (instancetype) initWithDictionaryRepresentation: (NSDictionary *) representation
                                          baseURL: (NSURL *) baseURL;

- (NSURL *) URL;
- (NSString *) identifier;
- (NSString *) version;

- (NSDictionary *) dictionaryRepresentation;

@end
