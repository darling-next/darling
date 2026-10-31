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
#import <PackageKit/PKPackageReference.h>

/* Dictionary representation keys, as used by the native reader. */
static NSString * const PKReferenceURLKey = @"URL";
static NSString * const PKReferenceIdentifierKey = @"Identifier";
static NSString * const PKReferenceVersionKey = @"Version";
static NSString * const PKReferenceSizeKey = @"Size";
static NSString * const PKReferenceHashAlgorithmKey = @"HashAlgorithm";
static NSString * const PKReferenceHashStringKey = @"HashString";
static NSString * const PKReferenceAdditionalInfoKey = @"AdditionalInfo";
static NSString * const PKReferenceLegacyMD5Key = @"MD5";

/* Legacy MD5 representations are migrated to this hash algorithm number. */
static const NSInteger PKReferenceMD5HashAlgorithm = 100;

@implementation PKPackageReference
{
    NSDictionary *_dictionaryRepresentation;
    NSURL *_baseURL;
}

- (instancetype) initWithURL: (NSURL *) url
                  identifier: (NSString *) identifier
                     version: (NSString *) version
                    fileSize: (unsigned long long) fileSize
                  fileDigest: (NSString *) fileDigest
              fileDigestType: (NSInteger) fileDigestType
{
    return [self initWithURL: url
                  identifier: identifier
                     version: version
                    fileSize: fileSize
                  fileDigest: fileDigest
              fileDigestType: fileDigestType
              additionalInfo: nil];
}

- (instancetype) initWithURL: (NSURL *) url
                  identifier: (NSString *) identifier
                     version: (NSString *) version
                    fileSize: (unsigned long long) fileSize
                  fileDigest: (NSString *) fileDigest
              fileDigestType: (NSInteger) fileDigestType
              additionalInfo: (NSDictionary *) additionalInfo
{
    NSMutableDictionary *representation;

    if (url == nil || identifier == nil || version == nil)
        return nil;

    representation = [NSMutableDictionary dictionaryWithCapacity: 8];
    [representation setObject: [url relativeString] forKey: PKReferenceURLKey];
    [representation setObject: identifier forKey: PKReferenceIdentifierKey];
    [representation setObject: version forKey: PKReferenceVersionKey];
    [representation setObject: [NSNumber numberWithUnsignedLongLong: fileSize]
                       forKey: PKReferenceSizeKey];
    [representation setObject: [NSNumber numberWithInteger: fileDigestType]
                       forKey: PKReferenceHashAlgorithmKey];

    if ([fileDigest isKindOfClass: [NSString class]])
        [representation setObject: fileDigest forKey: PKReferenceHashStringKey];
    if ([additionalInfo isKindOfClass: [NSDictionary class]])
        [representation setObject: additionalInfo forKey: PKReferenceAdditionalInfoKey];

    return [self initWithDictionaryRepresentation: representation
                                          baseURL: [url baseURL]];
}

- (instancetype) initWithDictionaryRepresentation: (NSDictionary *) representation
                                          baseURL: (NSURL *) baseURL
{
    if ([representation objectForKey: PKReferenceURLKey] == nil)
        return nil;
    if ([representation objectForKey: PKReferenceIdentifierKey] == nil)
        return nil;
    if ([representation objectForKey: PKReferenceVersionKey] == nil)
        return nil;

    self = [super init];
    if (self == nil)
        return nil;

    id legacyDigest = [representation objectForKey: PKReferenceLegacyMD5Key];
    id hashAlgorithm = [representation objectForKey: PKReferenceHashAlgorithmKey];

    if (legacyDigest != nil && hashAlgorithm == nil) {
        NSMutableDictionary *migrated = [representation mutableCopy];

        [migrated removeObjectForKey: PKReferenceLegacyMD5Key];
        [migrated setObject: [NSNumber numberWithInteger: PKReferenceMD5HashAlgorithm]
                     forKey: PKReferenceHashAlgorithmKey];
        [migrated setObject: legacyDigest forKey: PKReferenceHashStringKey];
        _dictionaryRepresentation = [migrated copy];
    } else {
        _dictionaryRepresentation = [representation copy];
    }

    _baseURL = baseURL;

    return self;
}

- (NSURL *) URL
{
    return [NSURL URLWithString: [_dictionaryRepresentation objectForKey: PKReferenceURLKey]
                  relativeToURL: _baseURL];
}

- (NSString *) identifier
{
    return [_dictionaryRepresentation objectForKey: PKReferenceIdentifierKey];
}

- (NSString *) version
{
    return [_dictionaryRepresentation objectForKey: PKReferenceVersionKey];
}

- (NSDictionary *) dictionaryRepresentation
{
    return [_dictionaryRepresentation copy];
}

- (NSString *) description
{
    return [_dictionaryRepresentation description];
}

@end
