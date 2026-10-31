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
#import <PackageKit/PKXARArchive.h>

NSString * const PKArchiveOpenOptionValidatedDigest = @"ValidatedHash";

@interface PKArchive (PKConcreteArchiveInitializer)

- (instancetype) initForReadingFromPath: (NSString *) path options: (NSDictionary *) options;

@end

@implementation PKArchive

+ (instancetype) archiveWithPath: (NSString *) path
{
    return [self archiveWithPath: path options: nil];
}

+ (instancetype) archiveWithPath: (NSString *) path options: (NSDictionary *) options
{
    NSString *standardizedPath = [path stringByStandardizingPath];

    for (id archiveClass in [self _allArchiveClasses]) {
        PKArchive *archive = [[archiveClass alloc] initForReadingFromPath: standardizedPath
                                                                  options: options];

        if (archive != nil)
            return archive;
    }

    return nil;
}

+ (NSArray *) _allArchiveClasses
{
    /* PKFolderArchive is not implemented, so it is not advertised here. */
    return @[ [PKXARArchive class] ];
}

- (NSString *) archivePath
{
    [NSException raise: NSInvalidArgumentException
                format: @"-[%@ %@]: abstract method, implement it in a concrete archive reader",
                        [self class], NSStringFromSelector(_cmd)];
    return nil;
}

- (BOOL) closeArchive
{
    [NSException raise: NSInvalidArgumentException
                format: @"-[%@ %@]: abstract method, implement it in a concrete archive reader",
                        [self class], NSStringFromSelector(_cmd)];
    return NO;
}

- (NSData *) dataForPath: (NSString *) path
{
    [NSException raise: NSInvalidArgumentException
                format: @"-[%@ %@]: abstract method, implement it in a concrete archive reader",
                        [self class], NSStringFromSelector(_cmd)];
    return nil;
}

- (NSData *) archiveDigestData
{
    return nil;
}

@end
