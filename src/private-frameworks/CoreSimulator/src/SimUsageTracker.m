/*
 This file is part of Darling.

 Copyright (C) 2023 Darling Team

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

/* SimUsageTracker -- the per-platform usage clock Xcode's IDESimulatorFoundation reads.
 *
 * WHY THIS EXISTS. IDESimulatorFoundation (Xcode 26.3, sha256
 * 83db461982c7f9846adf4a160baaa6b6d73925852a3d3f2429d6756b84873c0f) binds
 * _OBJC_CLASS_$_SimUsageTracker from the dependency field
 * /Library/Developer/PrivateFrameworks/CoreSimulator.framework/Versions/A/CoreSimulator, strong and
 * eager, together with seven names the tree's CoreSimulator already backs. dlsym of the class used
 * to return NULL, so once the two missing images above it exist the loader stops with
 * "Symbol not found: _OBJC_CLASS_$_SimUsageTracker" -- an implementation gap inside a present image,
 * reported by the previous rung's CORESIMULATOR-EXPORT-GAP line, closed here.
 *
 * THE CONTRACT, MEASURED FROM TWO INDEPENDENT PLACES, NEVER GUESSED:
 *
 *   (1) THE CALLER. The plug-in's only use of the class is its Swift method
 *       `IDESimulatorFoundation.IDESimulatorAvailabilityProvider.lastUsageDate(forPlatform:architecture:)`
 *       (demangled from _$s22IDESimulatorFoundation0A20AvailabilityProviderC13lastUsageDate11forPlatform12architecture0B00G0VSS_SStKFZTo),
 *       disassembled at 0x2bd90 in the plug-in: it builds "platform.architecture"
 *       (String.append of "." then the architecture), bridges it to ONE NSString, then
 *       objc_msgSend(classref, @selector(sharedTrackerWithIdentifier:), nsstring) -- the class
 *       object, one NSString argument -- and then objc_msgSend(retained result, @selector(lastUsage)),
 *       whose NSDate result it bridges with Foundation.Date._unconditionallyBridgeFromObjectiveC.
 *       Both selectors are the plug-in's own __objc_selrefs (0x3c5e8 -> 0x35b5f "sharedTrackerWithIdentifier:",
 *       0x3c5f0 -> 0x35739 "lastUsage"). So the WHOLE demanded surface is those two members, and the
 *       result of -lastUsage is passed to a NON-optional bridge, i.e. it must return a date.
 *
 *   (2) APPLE'S OWN IMPLEMENTATION, read out of Xcode's own payload. CoreSimulator.framework inside
 *       Xcode 26.3's Contents/Resources/Packages/XcodeSystemResources.pkg defines SimUsageTracker
 *       (x86_64 slice, `_OBJC_CLASS_$_SimUsageTracker` at 0xf49b0, methods below read from its
 *       symbol table and disassembly):
 *           + (instancetype)sharedTrackerWithIdentifier:   (0xf012)
 *           - (instancetype)initWithIdentifier:            (0xf2f7)
 *           - (NSString *)identifier / -setIdentifier:     (0xf534 / 0xf53e)
 *           - (NSString *)key       / -setKey:             (0xf54d / 0xf557)
 *           - (NSDate *)lastUsage                          (0xf3c4)
 *           - (void)use                                    (0xf499)
 *       ivars _identifier and _key. -initWithIdentifier: stores the identifier and builds
 *       key = [NSString stringWithFormat:@"SimUsageTracker.%@", identifier] (format string at
 *       0xa70ca). -use is [[NSUserDefaults simulatorDefaults] setObject:[NSDate date] forKey:key].
 *       -lastUsage is: read [simulatorDefaults objectForKey:key]; if that is an NSDate, return
 *       -copy of it; otherwise return [NSDate distantPast]. +sharedTrackerWithIdentifier: is a
 *       dispatch_once-created registry keyed by identifier, reached through a serial queue labelled
 *       "com.apple.CoreSimulator.SimUsageTracker.sharedTrackerQueue" (string at 0xa708f).
 *       `simulatorDefaults` is NOT in CoreSimulator: it is
 *       +[NSUserDefaults(SimDefaults) simulatorDefaults] in the package's CoreSimulatorUtilities
 *       (at 0xdd9c, with the once-guarded static `simulatorDefaults.defaults`), whose suite name is
 *       the string "com.apple.CoreSimulator" -- read from that image, not assumed.
 *
 * WHAT THIS FILE WRITES OUT, AND WHY. The same observable contract, over this tree's own
 * Foundation: `identifier`/`key` are the properties above, the store is an NSUserDefaults in the
 * suite Apple's own CoreSimulatorUtilities uses (com.apple.CoreSimulator) with Apple's own key
 * format, `-use` records [NSDate date], and -lastUsage answers the stored date when it is an NSDate
 * and [NSDate distantPast] otherwise. distantPast is not a fabricated value: it is Apple's own
 * "no record" answer, and it is the true answer here -- nothing in this guest records simulator
 * usage (the plug-in only reads), so the store is empty and every tracker reports "never used",
 * which is exactly what the same two members answer on a Mac whose defaults have no such key.
 * A nil return would instead be a behaviour change: the caller bridges the result through
 * Foundation.Date._unconditionallyBridgeFromObjectiveC, whose force-unwrap traps on nil.
 */

#import <CoreSimulator/SimUsageTracker.h>
#import <dispatch/dispatch.h>

/* The suite Apple's own +[NSUserDefaults(SimDefaults) simulatorDefaults] opens (measured from the
 * package's CoreSimulatorUtilities, see the header comment). */
static NSString *const SimUsageTrackerDefaultsSuite = @"com.apple.CoreSimulator";

@implementation SimUsageTracker

/* Explicit ivars and synthesis: this target's compile line (measured, `ninja -v` on
 * src/SimUsageTracker.m) carries no -fobjc-runtime and the compiler rejected an implicit
 * `_identifier`/`_key` as undeclared, so the ivars are declared in the header and synthesized here. */
@synthesize identifier = _identifier;
@synthesize key = _key;

/* Apple's own key format, string at 0xa70ca of Apple's CoreSimulator: "SimUsageTracker.%@". */
static NSString *SimUsageTrackerKeyForIdentifier(NSString *identifier)
{
	return [NSString stringWithFormat: @"SimUsageTracker.%@", identifier];
}

/* Apple's registry: one tracker per identifier, created on first use behind a serial queue. */
static NSMutableDictionary<NSString *, SimUsageTracker *> *sharedTrackers = nil;
static dispatch_queue_t sharedTrackerQueue = nil;
static dispatch_once_t sharedTrackerOnce = 0;

+ (instancetype)sharedTrackerWithIdentifier: (NSString *)identifier
{
	dispatch_once(&sharedTrackerOnce, ^{
		sharedTrackers = [[NSMutableDictionary alloc] init];
		sharedTrackerQueue = dispatch_queue_create(
			"com.apple.CoreSimulator.SimUsageTracker.sharedTrackerQueue",
			DISPATCH_QUEUE_SERIAL);
	});
	__block SimUsageTracker *tracker = nil;
	dispatch_sync(sharedTrackerQueue, ^{
		NSString *key = [identifier copy];
		tracker = [sharedTrackers objectForKey: key];
		if (tracker == nil) {
			tracker = [[self alloc] initWithIdentifier: key];
			[sharedTrackers setObject: tracker forKey: key];
		}
	});
	return tracker;
}

- (instancetype)initWithIdentifier: (NSString *)identifier
{
	self = [super init];
	if (self != nil) {
		_identifier = [identifier copy];
		_key = [SimUsageTrackerKeyForIdentifier(identifier) copy];
	}
	return self;
}

/* Apple's own store, opened as the same suite with the same lifetime (created once). */
static NSUserDefaults *SimUsageTrackerDefaults(void)
{
	static NSUserDefaults *defaults = nil;
	static dispatch_once_t once = 0;
	dispatch_once(&once, ^{
		defaults = [[NSUserDefaults alloc] initWithSuiteName: SimUsageTrackerDefaultsSuite];
	});
	return defaults;
}

- (NSDate *)lastUsage
{
	id value = [SimUsageTrackerDefaults() objectForKey: _key];
	if ([value isKindOfClass: [NSDate class]]) {
		return [value copy];
	}
	return [NSDate distantPast];
}

- (void)use
{
	[SimUsageTrackerDefaults() setObject: [NSDate date] forKey: _key];
}

@end
