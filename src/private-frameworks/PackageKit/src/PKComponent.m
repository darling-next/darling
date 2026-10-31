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
#import <PackageKit/PKComponent.h>

@implementation PKComponent

- (instancetype) initWithIdentifier: (NSString *) identifier
                            version: (NSString *) version
{
    if (identifier == nil) {
        [NSException raise: NSInvalidArgumentException
                    format: @"-[%@ %@]: an identifier is required",
                            [self class], NSStringFromSelector(_cmd)];
    }

    if (version == nil) {
        [NSException raise: NSInvalidArgumentException
                    format: @"-[%@ %@]: a version is required",
                            [self class], NSStringFromSelector(_cmd)];
    }

    self = [super init];
    if (self == nil)
        return nil;

    _identifier = [identifier copy];
    _version = [version copy];

    return self;
}

- (NSString *) identifier
{
    return _identifier;
}

- (NSString *) version
{
    return _version;
}

- (PKPackage *) package
{
    return _package;
}

- (void) setPackage: (PKPackage *) package
{
    _package = package;
}

- (BOOL) relocatable
{
    return _relocatable;
}

- (void) setRelocatable: (BOOL) flag
{
    _relocatable = flag;
}

- (BOOL) versionChecked
{
    return _versionChecked;
}

- (void) setVersionChecked: (BOOL) flag
{
    _versionChecked = flag;
}

- (BOOL) strictIdentifier
{
    return _strictIdentifier;
}

- (void) setStrictIdentifier: (BOOL) flag
{
    _strictIdentifier = flag;
}

@end
