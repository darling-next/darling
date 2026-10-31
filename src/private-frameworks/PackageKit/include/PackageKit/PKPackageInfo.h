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
 * The pkg-info document of a flat package.
 *
 * Only the root metadata used by package identity and the bundle graph is
 * modelled: identifier, version, format-version and the bundle/directive
 * elements.  Payload/script file containers are installation material and
 * are not parsed here.
 */
@interface PKPackageInfo : NSObject
{
@private
    NSXMLDocument *_xmlDocument;
}

- (instancetype) initWithData: (NSData *) data;

- (NSString *) identifier;
- (NSString *) version;

/* Root format-version as a number; a missing attribute defaults to 2. */
- (NSNumber *) formatVersion;

@end
