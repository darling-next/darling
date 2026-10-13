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
 * LSBundleProxy: the bundle-proxy half of LaunchServices, which lives in this sub-framework (the
 * CoreServices umbrella re-exports it, and that is what a client's dependency field names).
 *
 * LSBundleProxy is a real class of this framework, not a private one: the MacOSX SDK's
 * LaunchServices tbd lists it in its objc-classes (LaunchServices.tbd:256, next to LSApplicationProxy
 * and LSBundleRecord), so it is what a client's classref binds to.
 *
 * WHAT THE ONLY CLIENT ASKS FOR. The single binder of this class in the Xcode load closure is
 * IDEFoundation. Measured from its x86_64 slice (chained fixups decoded by hand, because the class
 * reference is a fixup and not a dyld-info row): one import of _OBJC_CLASS_$_LSBundleProxy
 * (import #4321, __DATA page 256, fixup at 0xe65048), and one code reference to that classref --
 * 0xd8e6d, in -[IDEInstallLocalMacWorker _runnableIsAnApp]:
 *
 *   d8e6d  movq 0xd8c1d4(%rip),%rdi   the LSBundleProxy class object
 *   d8e74  movq 0xd74f7d(%rip),%rsi   @selector(bundleProxyForURL:error:)
 *   d8e7b  movq %rbx,%rdx             the URL the caller tested for nil just above
 *   d8e7e  xorl %ecx,%ecx             error == NULL
 *   d8e80  callq *objc_msgSend
 *   d8e8e  testq %rax,%rax / setne    the RESULT IS ONLY TESTED FOR NIL
 *
 * So the object's ownership is the autoreleased-return convention (the caller retains and releases
 * it without ever messaging it) and its content is not read at all: what the caller consumes is the
 * predicate "is there a bundle at this URL". The answer is therefore built from the same facts this
 * sub-framework's records use -- the bundle on disk -- and a URL that names no bundle (or a
 * directory that only looks like one) answers nil, which is the answer the caller absorbs as "not
 * an app".
 *
 * The initializer's filter is the one this tree already documents as "what counts as a bundle here":
 * LSBundleRegistration.h, "the bundle's identifier must be non-NULL -- the same filter lsd's scan
 * applies, because CFBundle hands back bundles for empty directories".
 */

#import <Foundation/Foundation.h>

#include <LaunchServices/LSInfo.h>

@interface LSBundleProxy : NSObject
{
	NSURL* _bundleURL;
	NSString* _bundleIdentifier;
}
@property (readonly) NSURL* bundleURL;
@property (readonly) NSString* bundleIdentifier;
@property (readonly) NSString* localizedName;
@property (readonly) NSString* bundleVersion;
@property (readonly) NSURL* executableURL;

/* The measured entry point. Answers an autoreleased proxy for the bundle at `url`, or nil with
 * kLSApplicationNotFoundErr when no bundle with an identifier is there. */
+ (LSBundleProxy*)bundleProxyForURL:(NSURL*)url error:(NSError**)error;

- (instancetype)initWithBundleURL:(NSURL*)url bundleIdentifier:(NSString*)bundleIdentifier;
@end

@implementation LSBundleProxy

@synthesize bundleURL = _bundleURL;
@synthesize bundleIdentifier = _bundleIdentifier;

+ (LSBundleProxy*)bundleProxyForURL:(NSURL*)url error:(NSError**)error
{
	NSBundle* bundle = url != nil ? [NSBundle bundleWithURL: url] : nil;
	NSString* identifier = [[bundle infoDictionary] objectForKey: @"CFBundleIdentifier"];

	if (identifier == nil || [identifier length] == 0)
	{
		if (error != NULL)
			*error = [NSError errorWithDomain: NSOSStatusErrorDomain
						     code: kLSApplicationNotFoundErr
						 userInfo: nil];
		return nil;
	}

	return [[[self alloc] initWithBundleURL: url
			       bundleIdentifier: identifier] autorelease];
}

- (instancetype)initWithBundleURL:(NSURL*)url bundleIdentifier:(NSString*)bundleIdentifier
{
	self = [super init];
	if (self != nil)
	{
		_bundleURL = [url copy];
		_bundleIdentifier = [bundleIdentifier copy];
	}
	return self;
}

- (void)dealloc
{
	[_bundleURL release];
	[_bundleIdentifier release];
	[super dealloc];
}

/* The bundle on disk, which supplies everything the initializer did not: nil once it is gone. */
- (NSBundle*)bundle
{
	return [NSBundle bundleWithURL: _bundleURL];
}

- (NSURL*)executableURL
{
	return [[self bundle] executableURL];
}

/* As in LSBundleRecord: this tree's NSBundle does not implement -localizedName, so the name comes
 * out of the Info.plist itself, and the directory name is the fallback. */
- (NSString*)localizedName
{
	NSDictionary* info = [[self bundle] infoDictionary];
	NSString* name = info[@"CFBundleDisplayName"];

	if (name == nil)
		name = info[@"CFBundleName"];
	if (name == nil)
		name = [[_bundleURL path] lastPathComponent];
	return name;
}

- (NSString*)bundleVersion
{
	return [[[self bundle] infoDictionary] objectForKey: @"CFBundleVersion"];
}

@end
