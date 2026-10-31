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

#import <PackageKit/PKComponent.h>

@class PKBundleComponentVersion;

/* Storage type used when a bundle element names none. */
extern NSString * const PKBundleStorageTypeCFBundle;

/*
 * A bundle inside a package.  bundlePath is relative to the package payload
 * root; prefixPath is its parent directory (nil for a top-level bundle).
 */
@interface PKBundleComponent : PKComponent
{
@private
    NSString *_storageType;
    NSString *_bundlePath;
    PKBundleComponentVersion *_bundleVersion;
    NSInteger _overwriteAction;
}

- (instancetype) initWithIdentifier: (NSString *) identifier
                   versionAttributes: (NSDictionary *) versionAttributes
                          bundlePath: (NSString *) bundlePath
                        storageType: (NSString *) storageType;

- (NSString *) bundlePath;
- (PKBundleComponentVersion *) bundleVersion;
- (NSString *) prefixPath;
- (NSString *) storageType;

/* Bundle directive action: 0 none, 1 upgrade, 2 update, 3 atomic update. */
- (NSInteger) overwriteAction;
- (void) setOverwriteAction: (NSInteger) action;

@end
