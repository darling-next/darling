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
#import <PackageKit/PKLeopardPackage.h>
#import <PackageKit/PKPackageInfo.h>

NSString * const PKPackageOptionValidateDigest = @"ValidateDigest";

/* Implemented by PKPackageInfo.m. */
@interface PKPackageInfo (PKComponentMap)

- (NSDictionary *) _identifierToComponentMapWithPackage: (PKPackage *) package;

@end

@implementation PKPackage

+ (instancetype) packageWithPath: (NSString *) path
{
    return [self packageWithPath: path options: nil];
}

+ (instancetype) packageWithPath: (NSString *) path options: (NSDictionary *) options
{
    NSURL *url = [NSURL fileURLWithPath: [path stringByStandardizingPath]];

    return [self packageWithURL: url options: options];
}

+ (instancetype) packageWithURL: (NSURL *) url
{
    return [PKPackage packageWithURL: url options: nil];
}

+ (instancetype) packageWithURL: (NSURL *) url options: (NSDictionary *) options
{
    for (id packageClass in [self _allPackageClasses]) {
        if (![packageClass canInitWithURL: url])
            continue;

        PKPackage *package = [[packageClass alloc] initWithURL: url options: options];
        if (package == nil)
            continue;

        /* The metadata is read during init; the factory hands back a package
         * whose archive is closed, and readers reopen it lazily. */
        [[package archive] closeArchive];
        return package;
    }

    return nil;
}

+ (NSArray *) _allPackageClasses
{
    /* The only implemented flat-XAR reader; there is no fallback reader. */
    return @[ [PKLeopardPackage class] ];
}

+ (BOOL) canInitWithURL: (NSURL *) url
{
    return YES;
}

- (instancetype) initWithURL: (NSURL *) url options: (NSDictionary *) options
{
    NSMutableDictionary *archiveOptions;
    PKArchive *archive;

    if (![url isFileURL])
        return nil;
    if ([url path] == nil)
        return nil;

    archiveOptions = [NSMutableDictionary dictionaryWithCapacity: 1];

    id expectedDigest = [options objectForKey: PKPackageOptionValidateDigest];
    if (expectedDigest != nil)
        [archiveOptions setObject: expectedDigest forKey: PKArchiveOpenOptionValidatedDigest];

    archive = [PKArchive archiveWithPath: [url path] options: archiveOptions];
    if (archive == nil)
        return nil;

    self = [super init];
    if (self == nil)
        return nil;

    _fileURL = [url copy];
    _options = options;
    _archive = archive;

    return self;
}

- (void) _setPackageInfo: (PKPackageInfo *) packageInfo
{
    _packageInfo = packageInfo;
}

- (NSURL *) fileURL
{
    return _fileURL;
}

- (PKArchive *) archive
{
    return _archive;
}

- (PKPackageInfo *) packageInfo
{
    return _packageInfo;
}

- (NSString *) packageIdentifier
{
    return [[self packageInfo] identifier];
}

- (NSString *) packageVersion
{
    return [[self packageInfo] version];
}

- (NSDictionary *) _componentMap
{
    if (_componentMap == nil) {
        _componentMap = [[self packageInfo] _identifierToComponentMapWithPackage: self];
    }

    return _componentMap;
}

- (NSArray *) components
{
    return [[self _componentMap] allValues];
}

- (id) componentForIdentifier: (NSString *) identifier
{
    return [[self _componentMap] objectForKey: identifier];
}

- (void) enumerateBundleComponentsUsingBlock: (PKBundleComponentEnumerator) block
{
    @autoreleasepool {
        NSMutableArray *bundles = [NSMutableArray array];
        NSMutableDictionary *ancestors = [NSMutableDictionary dictionary];
        NSProgress *progress;
        NSArray *sorted;

        for (id component in [self components]) {
            if ([component isKindOfClass: [PKBundleComponent class]])
                [bundles addObject: component];
        }

        [bundles sortUsingDescriptors:
            @[ [NSSortDescriptor sortDescriptorWithKey: @"bundlePath" ascending: YES] ]];
        sorted = bundles;

        progress = [NSProgress progressWithTotalUnitCount: [sorted count]];

        for (PKBundleComponent *component in sorted) {
            NSString *prefixPath;
            PKBundleComponent *parent = nil;
            BOOL stop = NO;

            [progress becomeCurrentWithPendingUnitCount: 1];

            prefixPath = [component prefixPath];
            while ([prefixPath length] != 0 && ![prefixPath isEqualToString: @"/"]) {
                parent = [ancestors objectForKey: prefixPath];
                if (parent != nil)
                    break;

                prefixPath = [prefixPath stringByDeletingLastPathComponent];
            }

            @autoreleasepool {
                block(component, parent, &stop);
            }

            [progress resignCurrent];

            if (stop)
                break;

            /* A component is an ancestor only for later (deeper) paths. */
            [ancestors setObject: component forKey: [component bundlePath]];
        }
    }
}

@end
