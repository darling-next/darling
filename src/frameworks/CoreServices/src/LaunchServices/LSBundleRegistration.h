/*
This file is part of Darling.

Copyright (C) 2026 Darling Team

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

#ifndef LS_BUNDLE_REGISTRATION_H
#define LS_BUNDLE_REGISTRATION_H

#include <CoreFoundation/CFBundle.h>
#import <Foundation/NSObject.h>

@class FMDatabase;

/*
 * THE LaunchServices registration operation, in one place. Registers `bundle` in the LaunchServices
 * database `db`: its `bundle` row (path, identifier, Info.plist checksum, package type/creator/
 * signature) and the UTI, document-type and URL-type claims its Info.plist declares, inside one
 * transaction. An unchanged Info.plist (the row's checksum still matches) rolls the transaction back
 * and leaves the row as it is, which is what lsd's scan does.
 *
 * `db` must already carry lsd's schema and the bundle's identifier must be non-NULL -- the same
 * filter lsd's scan applies, because CFBundle hands back bundles for empty directories.
 *
 * Returns the bundle row's id, or 0 when the write failed. This is the code launchservicesd runs at
 * scan time (launchservicesd/LSBundle.m) and the code __LSRegisterURLWithOptions runs on demand
 * (LSRegister.m); see LSBundleRegistration.m for why it is one file rather than two.
 *
 * HIDDEN: it is internal to each image that compiles it (the daemon and the framework), not a new
 * name for a client to bind.
 */
__attribute__((visibility("hidden")))
int LSBundleRegisterInDatabase(FMDatabase* db, CFBundleRef bundle);

#endif
