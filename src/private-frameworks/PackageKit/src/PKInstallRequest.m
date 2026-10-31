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
#import <PackageKit/PKInstallRequest.h>

@implementation PKInstallRequest

/* There is no trust evaluation, authorization, SystemPolicy preflight or
 * install service in this tree.  Reporting a request here would let a caller
 * treat an unverified package as installable, so this boundary raises. */
+ (instancetype) requestWithPackages: (NSArray *) packages
                         destination: (NSString *) destination
{
    [NSException raise: NSInvalidArgumentException
                format: @"+[PKInstallRequest requestWithPackages:destination:] is unimplemented: "
                        @"PackageKit trust evaluation, authorization and the install service "
                        @"are not available"];

    return nil;
}

@end
