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

#import <PackageKit/PackageKit.h>
#import <PackageKit/PKBundleComponent.h>
#import <PackageKit/PKBundleComponentVersion.h>

NSString * const PKBundleStorageTypeCFBundle = @"cfbundle";

@implementation PKBundleComponent
{
    NSString *_storageType;
    NSString *_bundlePath;
    PKBundleComponentVersion *_bundleVersion;
    NSInteger _overwriteAction;
}

- (instancetype) initWithIdentifier: (NSString *) identifier
                   versionAttributes: (NSDictionary *) versionAttributes
                          bundlePath: (NSString *) bundlePath
                        storageType: (NSString *) storageType
{
    PKBundleComponentVersion *bundleVersion;

    if (bundlePath == nil) {
        [NSException raise: NSInvalidArgumentException
                    format: @"-[%@ %@]: a bundle path is required",
                            [self class], NSStringFromSelector(_cmd)];
    }

    bundleVersion = [PKBundleComponentVersion bundleComponentVersionWithPackageAttributes: versionAttributes];

    self = [super initWithIdentifier: identifier
                             version: [bundleVersion combinedVersionString]];
    if (self == nil)
        return nil;

    _storageType = [(storageType ?: PKBundleStorageTypeCFBundle) copy];
    _bundlePath = [bundlePath copy];
    _bundleVersion = bundleVersion;

    return self;
}

- (NSString *) bundlePath
{
    return _bundlePath;
}

- (PKBundleComponentVersion *) bundleVersion
{
    return _bundleVersion;
}

- (NSString *) prefixPath
{
    NSString *prefixPath = [_bundlePath stringByDeletingLastPathComponent];

    return [prefixPath length] != 0 ? prefixPath : nil;
}

- (NSString *) storageType
{
    return _storageType;
}

- (NSInteger) overwriteAction
{
    return _overwriteAction;
}

- (void) setOverwriteAction: (NSInteger) action
{
    _overwriteAction = action;
}

@end
