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

/*
 * THE REGISTRATION DIRECTION of LaunchServices: the two entry points that put a bundle into
 * /private/var/db/launchservices.db on demand -- __LSRegisterURLWithOptions (the private name
 * DVTFoundation binds; the macOS SDK's LaunchServices.tbd lists it among the LaunchServices
 * sub-framework's exports, which the CoreServices umbrella re-exports) and the public LSRegisterURL.
 *
 * WHAT THE ONE MEASURED CALLER PASSES (Xcode 26.3's DVTFoundation, x86_64 slice, its
 * LC_DYLD_CHAINED_FIXUPS imports decoded by hand -- llvm-objdump's --dyld-info mislabels these
 * neighbouring slots):
 *
 *   __LSRegisterURLWithOptions  bind slot 0x688D10 (__DATA_CONST,__got), field ordinal 33 =
 *   /System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices, STRONG, EAGER.
 *   ONE call site, 0x89353, inside
 *   -[DVTLocalComputer debugXPCServices:onPairedDevice:completionSemaphore:]:
 *     movq %rax, %rdi   ; the NSURL [NSURL fileURLWithPath: <a service's path>] built at 0x89348
 *     movl $0x2, %esi   ; the options word: 2
 *     callq __LSRegisterURLWithOptions
 *   and the RESULT IS NOT READ (the call falls through to the loop's own bookkeeping; the two
 *   branches into 0x89358 skip the call entirely when that URL is nil). So the measured contract is
 *   `OSStatus f(CFURLRef url, <32-bit options word>)` -- NOT a URL plus an options dictionary, and
 *   NOT an out-parameter.
 *
 * WHAT THE TREE CAN HONESTLY DO WITH IT. The registration itself is lsd's operation (LSBundle):
 * the daemon walks /System/Library/Frameworks, /Applications and /System/Applications and writes one
 * row per bundle plus the UTI/document-type/URL-type claims the bundle declares. This file runs THAT
 * code -- LSBundleRegistration.m, the same file launchservicesd compiles -- for the bundle the URL
 * names, because this caller asks about a bundle OUTSIDE the directories lsd scans. The answer is
 * therefore real when the tree can serve it, and the failure is this family's own when it cannot:
 *
 *   * no URL, or a URL that is not a file URL            -> paramErr;
 *   * no /private/var/db/launchservices.db              -> fnfErr, the same answer LSInfo.m gives
 *     when its database queue is nil. The database is lsd's: lsd opens it read-write and creates the
 *     schema at start (LSBundle.m setupDBSchema/createDBSchema), and this file deliberately does not
 *     create a second one -- a private schema would be a second authority on the database;
 *   * a path that is not a bundle, or a bundle with no CFBundleIdentifier -> kLSNotAnApplicationErr,
 *     the same filter lsd's scan applies (CFBundle hands back bundles for empty directories);
 *   * a database that refuses the write               -> kLSUnknownErr.
 *
 * THE OPTIONS WORD is accepted and not interpreted. This tree's registration has ONE behaviour --
 * insert, or update when the Info.plist's checksum changed, which is lsd's own test, reused -- so no
 * option bit is invented here. The public wrapper passes its own documented flag through in the low
 * bit rather than claiming that the measured value 2 means something this tree has not measured.
 */

#include <LaunchServices/LaunchServices.h>
#include <LaunchServices/LaunchServicesPriv.h>
#include <CoreFoundation/CFBundle.h>
#include <CarbonCore/MacErrors.h>
#import <Foundation/Foundation.h>
#import <fmdb/FMDatabase.h>
#include "LSBundleRegistration.h"

/* The one path lsd and LSInfo.m both name for the LaunchServices database. */
#define LS_DATABASE_PATH @"/private/var/db/launchservices.db"

OSStatus _LSRegisterURLWithOptions(CFURLRef inURL, UInt32 inOptions)
{
	(void) inOptions; /* measured, not interpreted: see the file comment */

	if (inURL == NULL || CFGetTypeID(inURL) != CFURLGetTypeID())
		return paramErr;

	CFStringRef scheme = CFURLCopyScheme(inURL);
	Boolean isFile = scheme != NULL &&
		CFStringCompare(scheme, CFSTR("file"), kCFCompareCaseInsensitive) == kCFCompareEqualTo;
	if (scheme != NULL)
		CFRelease(scheme);
	if (!isFile)
		return paramErr;

	/* lsd's database, or nothing to register into: report the absence rather than writing a private
	 * database (LSInfo.m answers fnfErr for the same reason). */
	if (![[NSFileManager defaultManager] fileExistsAtPath: LS_DATABASE_PATH])
		return fnfErr;

	CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, inURL);
	if (bundle == NULL)
		return kLSNotAnApplicationErr;

	if (CFBundleGetIdentifier(bundle) == NULL)
	{
		CFRelease(bundle);
		return kLSNotAnApplicationErr;
	}

	FMDatabase* db = [FMDatabase databaseWithPath: LS_DATABASE_PATH];
	if (![db open])
	{
		CFRelease(bundle);
		return fnfErr;
	}

	int bundleId = LSBundleRegisterInDatabase(db, bundle);
	NSString* error = [db lastErrorMessage];
	[db close];
	CFRelease(bundle);

	if (bundleId == 0)
	{
		if (error != nil)
			NSLog(@"LaunchServices: could not register the bundle at %@: %@", (NSURL*) inURL, error);
		return kLSUnknownErr;
	}

	return noErr;
}

OSStatus LSRegisterURL(CFURLRef inURL, Boolean inUpdate)
{
	return _LSRegisterURLWithOptions(inURL, inUpdate ? 1u : 0u);
}
