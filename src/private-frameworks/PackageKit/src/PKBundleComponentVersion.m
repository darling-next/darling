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
#import <PackageKit/PKBundleComponentVersion.h>
#import <CoreFoundation/CFBundlePriv.h>

/* PackageInfo bundle attribute names. */
static NSString * const PKShortVersionAttributeName = @"CFBundleShortVersionString";
static NSString * const PKBundleVersionAttributeName = @"CFBundleVersion";
static NSString * const PKSourceVersionAttributeName = @"SourceVersion";
static NSString * const PKBuildVersionAttributeName = @"BuildVersion";

/* Comparison keys, in native order.  -compare: uses the ordered keys and
 * -properCompare: swaps the first two. */
static NSString * const PKShortVersionStringKey = @"shortVersionString";
static NSString * const PKBundleVersionKey = @"bundleVersion";
static NSString * const PKSourceVersionKey = @"sourceVersion";

static NSString * const PKVersionTupleSeparator = @"-";
static NSString * const PKVersionComponentSeparator = @" ";
static NSString * const PKVersionTupleComponentSeparator = @".";
static NSString * const PKVersionTupleFormat = @"%d.%d.%d";
static NSString * const PKVersionResourceName = @"version";
static NSString * const PKVersionResourceType = @"plist";
static NSString * const PKVersionPlistName = @"version.plist";

@interface PKBundleComponentVersion ()

+ (NSArray *) _orderedVersionKeys;
+ (NSArray *) _properlyOrderedVersionKeys;
+ (NSString *) _standardizedTupleVersionString: (NSString *) string;
+ (NSDictionary *) _combinedVersionAndInfoDictionaryForCFBundle: (CFBundleRef) bundle;

- (instancetype) initWithShortVersionString: (id) shortVersionString
                              bundleVersion: (id) bundleVersion
                              sourceVersion: (id) sourceVersion
                               buildVersion: (id) buildVersion
                        forOnDiskComponent: (BOOL) onDiskComponent;

- (BOOL) _isOnDiskComponent;
- (NSInteger) compare: (PKBundleComponentVersion *) other
    withProperVersionOrdering: (BOOL) proper;

@end

static NSString *PKStringFromVersionValue (id value)
{
    if ([value isKindOfClass: [NSString class]])
        return value;

    return [value description];
}

@implementation PKBundleComponentVersion
{
    NSString *_shortVersionString;
    NSString *_bundleVersion;
    NSString *_sourceVersion;
    NSString *_buildVersion;
    BOOL _onDiskComponent;
}

+ (instancetype) bundleComponentVersionWithPackageAttributes: (NSDictionary *) attributes
{
    return [[self alloc] initWithAttributes: attributes forOnDiskComponent: NO];
}

+ (instancetype) bundleComponentVersionWithOnDiskCFBundle: (CFBundleRef) bundle
{
    NSDictionary *attributes = [self _combinedVersionAndInfoDictionaryForCFBundle: bundle];

    return [[self alloc] initWithAttributes: attributes forOnDiskComponent: YES];
}

- (instancetype) initWithAttributes: (NSDictionary *) attributes
                forOnDiskComponent: (BOOL) onDiskComponent
{
    return [self initWithShortVersionString: [attributes objectForKey: PKShortVersionAttributeName]
                              bundleVersion: [attributes objectForKey: PKBundleVersionAttributeName]
                              sourceVersion: [attributes objectForKey: PKSourceVersionAttributeName]
                               buildVersion: [attributes objectForKey: PKBuildVersionAttributeName]
                        forOnDiskComponent: onDiskComponent];
}

- (instancetype) initWithShortVersionString: (id) shortVersionString
                              bundleVersion: (id) bundleVersion
                              sourceVersion: (id) sourceVersion
                               buildVersion: (id) buildVersion
                        forOnDiskComponent: (BOOL) onDiskComponent
{
    NSString *shortString = PKStringFromVersionValue(shortVersionString);
    NSString *bundleString = PKStringFromVersionValue(bundleVersion);
    NSString *sourceString = PKStringFromVersionValue(sourceVersion);
    NSString *buildString = PKStringFromVersionValue(buildVersion);

    self = [super init];
    if (self == nil)
        return nil;

    _shortVersionString = [[PKBundleComponentVersion _standardizedTupleVersionString: shortString] copy];
    _bundleVersion = [[PKBundleComponentVersion _standardizedTupleVersionString: bundleString] copy];
    _sourceVersion = [sourceString copy];
    _buildVersion = [buildString copy];
    _onDiskComponent = onDiskComponent;

    return self;
}

+ (NSArray *) _orderedVersionKeys
{
    return @[ PKShortVersionStringKey, PKBundleVersionKey, PKSourceVersionKey ];
}

+ (NSArray *) _properlyOrderedVersionKeys
{
    return @[ PKBundleVersionKey, PKShortVersionStringKey, PKSourceVersionKey ];
}

/* The native normalization keeps the first space-separated token, splits it
 * on dots and renders up to three integer components. */
+ (NSString *) _standardizedTupleVersionString: (NSString *) string
{
    NSArray *components;
    NSString *head;
    int values[3] = { 0, 0, 0 };
    NSUInteger index = 0;

    if ([string length] == 0)
        return nil;

    components = [string componentsSeparatedByString: PKVersionComponentSeparator];
    head = string;
    if ([components count] >= 2)
        head = [components objectAtIndex: 0];

    components = [head componentsSeparatedByString: PKVersionTupleComponentSeparator];
    for (NSString *component in components) {
        if (index == 3)
            break;

        values[index] = [component intValue];
        index++;
    }

    return [NSString stringWithFormat: PKVersionTupleFormat, values[0], values[1], values[2]];
}

- (NSString *) combinedVersionString
{
    NSMutableArray *values = [NSMutableArray array];

    for (NSString *key in [PKBundleComponentVersion _orderedVersionKeys]) {
        id value = [self valueForKey: key];

        [values addObject: value ?: @""];
    }

    return [values componentsJoinedByString: PKVersionTupleSeparator];
}

- (NSInteger) compare: (PKBundleComponentVersion *) other
{
    return [self compare: other withProperVersionOrdering: NO];
}

- (NSInteger) properCompare: (PKBundleComponentVersion *) other
{
    return [self compare: other withProperVersionOrdering: YES];
}

- (NSInteger) compare: (PKBundleComponentVersion *) other
    withProperVersionOrdering: (BOOL) proper
{
    NSArray *keys = proper ? [PKBundleComponentVersion _properlyOrderedVersionKeys]
                           : [PKBundleComponentVersion _orderedVersionKeys];

    for (NSString *key in keys) {
        NSString *left = [self valueForKey: key];
        NSString *right;

        if (left == nil) {
            /* A missing packaged value is skipped; a missing on-disk value
             * compares as the empty string. */
            if (![self _isOnDiskComponent])
                continue;

            left = @"";
        }

        right = [other valueForKey: key];
        if (right == nil) {
            if (![other _isOnDiskComponent])
                continue;

            right = @"";
        }

        NSInteger result = [left compare: right options: NSNumericSearch];
        if (result != 0)
            return result;
    }

    return 0;
}

- (BOOL) _isOnDiskComponent
{
    return _onDiskComponent;
}

- (NSString *) shortVersionString
{
    return _shortVersionString;
}

- (NSString *) bundleVersion
{
    return _bundleVersion;
}

- (NSString *) sourceVersion
{
    return _sourceVersion;
}

- (NSString *) buildVersion
{
    return _buildVersion;
}

+ (NSDictionary *) _combinedVersionAndInfoDictionaryForCFBundle: (CFBundleRef) bundle
{
    NSURL *infoPlistURL = (__bridge_transfer NSURL *) _CFBundleCopyInfoPlistURL(bundle);
    NSDictionary *infoDictionary = (__bridge NSDictionary *) CFBundleGetInfoDictionary(bundle);
    NSURL *versionURL;
    NSDictionary *versionDictionary;
    CFURLRef resourceURL;

    resourceURL = CFBundleCopyResourceURL(bundle,
                                          (__bridge CFStringRef) PKVersionResourceName,
                                          (__bridge CFStringRef) PKVersionResourceType,
                                          NULL);
    if (resourceURL != NULL) {
        versionURL = (__bridge_transfer NSURL *) resourceURL;
    } else {
        NSString *path = [[[infoPlistURL path] stringByDeletingLastPathComponent]
                             stringByAppendingPathComponent: PKVersionPlistName];

        if (path == nil)
            return infoDictionary;

        versionURL = [[NSURL alloc] initFileURLWithPath: path];
        if (versionURL == nil)
            return infoDictionary;
    }

    versionDictionary = [NSDictionary dictionaryWithContentsOfURL: versionURL];
    if (versionDictionary == nil)
        return infoDictionary;
    if (infoDictionary == nil)
        return versionDictionary;

    NSMutableDictionary *combined = [infoDictionary mutableCopy];
    [combined addEntriesFromDictionary: versionDictionary];

    return combined;
}

@end
