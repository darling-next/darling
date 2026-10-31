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
#import <CoreFoundation/CFBundle.h>

/*
 * Version tuple of a bundle, either as packaged (attributes from the
 * PackageInfo bundle element) or as found on disk (CFBundle Info.plist
 * merged with version.plist).
 *
 * shortVersionString and bundleVersion are the standardized tuples
 * ("<major>.<minor>.<patch>"); sourceVersion and buildVersion are raw.
 */
@interface PKBundleComponentVersion : NSObject

+ (instancetype) bundleComponentVersionWithPackageAttributes: (NSDictionary *) attributes;
+ (instancetype) bundleComponentVersionWithOnDiskCFBundle: (CFBundleRef) bundle;

- (instancetype) initWithAttributes: (NSDictionary *) attributes
                forOnDiskComponent: (BOOL) onDiskComponent;

/* Field-by-field NSNumericSearch comparison; first difference wins. */
- (NSInteger) compare: (PKBundleComponentVersion *) other;
- (NSInteger) properCompare: (PKBundleComponentVersion *) other;

- (NSString *) combinedVersionString;

- (NSString *) shortVersionString;
- (NSString *) bundleVersion;
- (NSString *) sourceVersion;
- (NSString *) buildVersion;

@end
