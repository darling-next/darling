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

/* Implemented by PKPackage.m. */
@interface PKPackage (PKLeopardPackagePrivate)

- (void) _setPackageInfo: (PKPackageInfo *) packageInfo;

@end

@implementation PKLeopardPackage

- (instancetype) initWithURL: (NSURL *) url
{
    return [self initWithURL: url options: nil];
}

/* A URL may carry a fragment naming the product payload the package lives
 * in; the percent-decoded fragment is the archive subpath prefix. */
- (NSString *) _archiveSubpathWithFileName: (NSString *) fileName
{
    NSString *fragment = [[[self fileURL] fragment] stringByRemovingPercentEncoding];

    if (fragment == nil)
        return fileName;

    return [fragment stringByAppendingPathComponent: fileName];
}

- (instancetype) initWithURL: (NSURL *) url options: (NSDictionary *) options
{
    NSString *packageInfoPath;
    NSData *packageInfoData;
    PKPackageInfo *packageInfo;
    NSNumber *formatVersion;

    self = [super initWithURL: url options: options];
    if (self == nil)
        return nil;

    packageInfoPath = [self _archiveSubpathWithFileName: @"PackageInfo"];
    packageInfoData = [[self archive] dataForPath: packageInfoPath];
    if (packageInfoData == nil) {
        NSLog(@"PackageKit: %@ has no readable %@ member", [url path], packageInfoPath);
        return nil;
    }

    packageInfo = [[PKPackageInfo alloc] initWithData: packageInfoData];
    if (packageInfo == nil) {
        NSLog(@"PackageKit: %@ has a malformed %@ member", [url path], packageInfoPath);
        return nil;
    }

    if ([packageInfo identifier] == nil) {
        NSLog(@"PackageKit: %@ names no package identifier", [url path]);
        return nil;
    }

    formatVersion = [packageInfo formatVersion];
    if ([formatVersion compare: @2] == NSOrderedDescending) {
        NSLog(@"PackageKit: %@ uses unsupported format-version %@", [url path], formatVersion);
        return nil;
    }

    [self _setPackageInfo: packageInfo];

    return self;
}

@end
