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
#import <PackageKit/PKPackageSpecifier.h>
#import <PackageKit/PKPackageReference.h>

@implementation PKPackageSpecifier
{
    PKPackageReference *_packageReference;
    NSString *_prefixPath;
    NSData *_archiveDigestData;
    PKPackage *_package;
}

- (instancetype) initWithPackage: (PKPackage *) package
{
    PKPackageReference *reference;
    NSURL *url = [package fileURL];
    NSString *identifier = [package packageIdentifier];
    NSString *version = [package packageVersion] ?: @"";

    reference = [[PKPackageReference alloc] initWithURL: url
                                             identifier: identifier
                                                version: version
                                               fileSize: 0
                                             fileDigest: @""
                                         fileDigestType: 0];
    if (reference == nil)
        return nil;

    self = [self initWithPackageReference: reference];
    if (self == nil)
        return nil;

    _package = package;
    _archiveDigestData = [[package archive] archiveDigestData];

    return self;
}

- (instancetype) initWithPackageReference: (PKPackageReference *) packageReference
{
    self = [super init];
    if (self == nil)
        return nil;

    [self setPackageReference: packageReference];

    return self;
}

- (PKPackageReference *) packageReference
{
    @synchronized (self) {
        return _packageReference;
    }
}

- (void) setPackageReference: (PKPackageReference *) packageReference
{
    if (packageReference == _packageReference)
        return;

    @synchronized (self) {
        if (_packageReference != nil &&
            ![[_packageReference identifier] isEqualToString: [packageReference identifier]]) {
            NSLog(@"PackageKit: replacing package reference %@ with %@",
                  [_packageReference identifier], [packageReference identifier]);
        }

        _packageReference = packageReference;
        _package = nil;
    }
}

- (PKPackage *) package
{
    @autoreleasepool {
        if (_package != nil)
            return _package;

        NSURL *url = [_packageReference URL];
        if (url != nil && [url isFileURL]) {
            NSDictionary *options = nil;

            if (_archiveDigestData != nil) {
                options = @{ PKPackageOptionValidateDigest: _archiveDigestData };
            }

            _package = [PKPackage packageWithURL: url options: options];
        }

        if (_package != nil)
            _archiveDigestData = [[_package archive] archiveDigestData];

        return _package;
    }
}

- (NSString *) prefixPath
{
    @synchronized (self) {
        return _prefixPath;
    }
}

- (void) setPrefixPath: (NSString *) prefixPath
{
    @synchronized (self) {
        _prefixPath = prefixPath;
    }
}

@end
