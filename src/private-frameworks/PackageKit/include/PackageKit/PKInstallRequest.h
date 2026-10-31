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

#import <Foundation/Foundation.h>

/*
 * Install requests are NOT implemented.  Creating one requires the real
 * PackageKit trust evaluation (archive signatures, Apple software-signing
 * anchors, trust levels 500/501), authorization rights
 * (system.install.apple-software), SystemPolicy preflight and the install
 * service.  None of those services exist here, so the factory raises instead
 * of returning a request that would report trust or install success.
 */
@interface PKInstallRequest : NSObject

+ (instancetype) requestWithPackages: (NSArray *) packages
                         destination: (NSString *) destination;

@end
