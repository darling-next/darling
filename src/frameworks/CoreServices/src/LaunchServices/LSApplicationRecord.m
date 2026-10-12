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
 * LSApplicationRecord, LSApplicationExtensionRecord and the LSApplicationState their
 * -applicationState returns: the record half of LaunchServices, which lives in this sub-framework
 * (the CoreServices umbrella re-exports it, which is where a client's dependency field points).
 *
 * WHERE A RECORD'S ANSWERS COME FROM. Every record this guest can produce is one row of the bundle
 * table of /private/var/db/launchservices.db -- the same database LSInfo.m queries through
 * getDatabaseQueue(), and the same one lsd fills by scanning /Applications, /System/Applications
 * and /System/Library/Frameworks (launchservicesd/LSBundle.m, which is also the only place a
 * package type is recorded). The row supplies the record's URL and bundle identifier; the
 * remaining properties are read from the bundle on disk, and are nil when the bundle is no longer
 * there. A query that matches no row is not-found -- an initializer that returns nil with
 * kLSApplicationNotFoundErr, an enumerator with nothing in it, an empty extension list -- because
 * that is what the database says; a record assembled from anything else would report an
 * application that this guest does not have.
 *
 * WHAT THE ONLY CLIENT ASKS FOR. The single binder of these two classes in the Xcode load closure
 * is DVTInstrumentsFoundation, and its demand is measured, not guessed (x86_64 slice, chained-fixup
 * imports decoded by hand; llvm-objdump's --dyld-info mislabels these neighbouring slots):
 *
 *   _OBJC_CLASS_$_LSApplicationRecord          bind slot 0x139980   (__objc_classrefs)
 *   _OBJC_CLASS_$_LSApplicationExtensionRecord bind slot 0x139998   (__objc_classrefs)
 *
 *   +[LSApplicationRecord enumeratorWithOptions:0]              0x5e07   (fast-enumerated from 0x5e21)
 *   +[LSApplicationExtensionRecord enumeratorWithOptions:0]     0x600c   (fast-enumerated from 0x6026)
 *   [[LSApplicationRecord alloc] initWithBundleIdentifier:
 *        allowPlaceholder:0 error:&error]                       0x512c   (error read back at 0x5162,
 *                                                                        logged when the record is nil)
 *   -URL                        0x6b93        -applicationState  0x540a, 0x54ba
 *   -bundleIdentifier           0x6bfe        -developerType     0x5480
 *   -executableURL              0x6c4a        -applicationExtensionRecords 0x5239, 0x5570
 *   -localizedName              0x6cac        -containingBundleRecord      0x5910
 *   -bundleVersion              0x6cef        -isPlaceholder / -isRestricted 0x5429, 0x54d6
 *                                             (both on the object -applicationState returned)
 *
 * Only the record's -URL, -bundleIdentifier, -executableURL, -localizedName and -bundleVersion also
 * come from NSBundle, whose -localizedName this tree does not implement, so the record reads
 * CFBundleDisplayName/CFBundleName out of the Info.plist itself and falls back to the directory name
 * rather than messaging a selector the runtime would not answer.
 */

#import <Foundation/Foundation.h>
#include <LaunchServices/LSInfo.h>
#include <fmdb/FMDatabaseQueue.h>

/* The application database, opened by LSInfo.m (which declares the same queue hidden to the image). */
extern FMDatabaseQueue* getDatabaseQueue(void);

/* The values the one measured client maps to strings by comparing the low byte (0x5734/0x5748:
 * 1 -> "System", 3 -> "User", anything else -> "Unknown"). Every record this guest can produce
 * answers Unknown: the database row carries no signing information and Darling does no code
 * signing, so which authority a bundle came from is not a fact this guest has. */
typedef NS_ENUM(NSUInteger, LSDeveloperType)
{
	LSDeveloperTypeUnknown = 0,
	LSDeveloperTypeSystem = 1,
	LSDeveloperTypeUser = 3,
};

@class LSApplicationExtensionRecord;

/* The bundle table rows a query matches, each as @[path, bundleIdentifier], in the order the
 * database returns them. An absent or unreadable database yields no rows -- FMDB hands back a nil
 * result set, not an error -- which is the same answer as an empty table: this guest knows of no
 * such bundle. */
static NSArray<NSArray<NSString*>*>* rowsForQuery(NSString* query, NSArray* arguments)
{
	FMDatabaseQueue* queue = getDatabaseQueue();
	if (queue == nil)
		return @[];

	NSMutableArray<NSArray<NSString*>*>* rows = [NSMutableArray array];
	[queue inDatabase: ^(FMDatabase* db) {
		FMResultSet* rs = arguments != nil ? [db executeQuery: query withArgumentsInArray: arguments]
						   : [db executeQuery: query];
		while ([rs next])
		{
			NSString* path = [rs stringForColumn: @"path"];
			NSString* identifier = [rs stringForColumn: @"bundle_id"];
			if (path != nil && identifier != nil)
				[rows addObject: @[path, identifier]];
		}
		[rs close];
	}];

	return rows;
}

/* The record-side spelling of "this bundle is an application": the package type LSInfo.m's own
 * LSFindApplicationForInfo filters on. */
static NSString* const kApplicationRowsQuery =
	@"select path, bundle_id from bundle where package_type = 'APPL'";

@interface LSApplicationState : NSObject
{
	BOOL _valid;
	BOOL _installed;
	BOOL _placeholder;
	BOOL _restricted;
}
@property (readonly, getter=isValid) BOOL valid;
@property (readonly, getter=isInstalled) BOOL installed;
@property (readonly, getter=isPlaceholder) BOOL placeholder;
@property (readonly, getter=isRestricted) BOOL restricted;
- (instancetype)initWithValid:(BOOL)valid installed:(BOOL)installed placeholder:(BOOL)placeholder restricted:(BOOL)restricted;
@end

@interface LSBundleRecord : NSObject
{
	NSURL* _URL;
	NSString* _bundleIdentifier;
}
@property (readonly) NSURL* URL;
@property (readonly) NSString* bundleIdentifier;
@property (readonly) NSURL* executableURL;
@property (readonly) NSString* localizedName;
@property (readonly) NSString* bundleVersion;

/* The designated initializer of both record classes. Answers kLSApplicationNotFoundErr and nil
 * when the database has no such row: allowPlaceholder cannot change that answer, because a
 * placeholder (an application that is not on disk yet) is not something this database can hold --
 * a row exists only once lsd has read a bundle. */
- (instancetype)initWithBundleIdentifier:(NSString*)bundleIdentifier
			allowPlaceholder:(BOOL)allowPlaceholder
				   error:(NSError**)error;

/* A row that has already been found: the initializer above and the enumerators share it. */
- (instancetype)initWithPath:(NSString*)path bundleIdentifier:(NSString*)bundleIdentifier;

/* Which rows describe this kind of record; the subclass narrows it. */
- (NSArray<NSArray<NSString*>*>*)rowsForBundleIdentifier:(NSString*)bundleIdentifier;
@end

@interface LSApplicationRecord : LSBundleRecord
+ (NSEnumerator<LSApplicationRecord*>*)enumeratorWithOptions:(NSUInteger)options;
@property (readonly) LSApplicationState* applicationState;
@property (readonly) NSUInteger developerType;
- (NSArray<LSApplicationExtensionRecord*>*)applicationExtensionRecords;
@end

@interface LSApplicationExtensionRecord : LSBundleRecord
+ (NSEnumerator<LSApplicationExtensionRecord*>*)enumeratorWithOptions:(NSUInteger)options;
@property (readonly) LSBundleRecord* containingBundleRecord;
@end

@implementation LSApplicationState

@synthesize valid = _valid;
@synthesize installed = _installed;
@synthesize placeholder = _placeholder;
@synthesize restricted = _restricted;

- (instancetype)initWithValid:(BOOL)valid installed:(BOOL)installed placeholder:(BOOL)placeholder restricted:(BOOL)restricted
{
	self = [super init];
	if (self != nil)
	{
		_valid = valid;
		_installed = installed;
		_placeholder = placeholder;
		_restricted = restricted;
	}
	return self;
}

@end

@implementation LSBundleRecord

@synthesize URL = _URL;
@synthesize bundleIdentifier = _bundleIdentifier;

- (instancetype)initWithPath:(NSString*)path bundleIdentifier:(NSString*)bundleIdentifier
{
	self = [super init];
	if (self != nil)
	{
		_URL = [[NSURL alloc] initFileURLWithPath: path isDirectory: YES];
		_bundleIdentifier = [bundleIdentifier copy];
	}
	return self;
}

- (instancetype)initWithBundleIdentifier:(NSString*)bundleIdentifier
			allowPlaceholder:(BOOL)allowPlaceholder
				   error:(NSError**)error
{
	if (bundleIdentifier == nil)
	{
		[self release];
		if (error != NULL)
			*error = [NSError errorWithDomain: NSOSStatusErrorDomain
						     code: kLSApplicationNotFoundErr
						 userInfo: nil];
		return nil;
	}

	NSArray* rows = [self rowsForBundleIdentifier: bundleIdentifier];
	if ([rows count] != 1)
	{
		[self release];
		if (error != NULL)
			*error = [NSError errorWithDomain: NSOSStatusErrorDomain
						     code: kLSApplicationNotFoundErr
						 userInfo: nil];
		return nil;
	}

	return [self initWithPath: rows[0][0] bundleIdentifier: rows[0][1]];
}

/* The rows of an application: the package type LSInfo.m's own LSFindApplicationForInfo filters on.
 * LSApplicationExtensionRecord overrides this with the narrower extension query. */
- (NSArray<NSArray<NSString*>*>*)rowsForBundleIdentifier:(NSString*)bundleIdentifier
{
	return rowsForQuery(@"select path, bundle_id from bundle where bundle_id = ? and package_type = 'APPL'",
		@[bundleIdentifier]);
}

- (void)dealloc
{
	[_URL release];
	[_bundleIdentifier release];
	[super dealloc];
}

/* The bundle on disk, which supplies everything the row does not: nil once the bundle is gone. */
- (NSBundle*)bundle
{
	return [NSBundle bundleWithURL: _URL];
}

- (NSURL*)executableURL
{
	return [[self bundle] executableURL];
}

- (NSString*)localizedName
{
	NSDictionary* info = [[self bundle] infoDictionary];
	NSString* name = info[@"CFBundleDisplayName"];
	if (name == nil)
		name = info[@"CFBundleName"];
	if (name == nil)
		name = [[_URL path] lastPathComponent];
	return name;
}

- (NSString*)bundleVersion
{
	return [[[self bundle] infoDictionary] objectForKey: @"CFBundleVersion"];
}

@end

@implementation LSApplicationRecord

/* The options select record kinds -- placeholders, bookmarks -- that this database does not hold:
 * every row in it was written from a bundle lsd read on disk (launchservicesd/LSBundle.m), so the
 * enumeration is every application row there is, whatever the caller asked to include. */
+ (NSEnumerator<LSApplicationRecord*>*)enumeratorWithOptions:(NSUInteger)options
{
	NSMutableArray<LSApplicationRecord*>* records = [NSMutableArray array];
	for (NSArray<NSString*>* row in rowsForQuery(kApplicationRowsQuery, nil))
	{
		LSApplicationRecord* record = [[[self alloc] initWithPath: row[0] bundleIdentifier: row[1]] autorelease];
		[records addObject: record];
	}
	return [records objectEnumerator];
}

/* Every record this guest can produce is a bundle the database found on disk, so it is installed
 * and valid, it is not a placeholder (that is an application whose download has not finished, and
 * lsd registers a bundle only after reading it), and it is not restricted (Darling has no
 * restriction mechanism, so nothing marks a bundle restricted). The client checks isRestricted to
 * decide whether to list the record at all and isPlaceholder to describe it. */
- (LSApplicationState*)applicationState
{
	return [[[LSApplicationState alloc] initWithValid: YES installed: YES placeholder: NO restricted: NO] autorelease];
}

- (NSUInteger)developerType
{
	return LSDeveloperTypeUnknown;
}

/* Application extensions (.appex bundles) are registered by nothing in this guest: lsd scans
 * /Applications, /System/Applications and /System/Library/Frameworks, and an .appex lives inside an
 * application's PlugIns directory, which none of those scans descends into. The database therefore
 * holds no row for one, and this is the same "the database has no such row" answer the initializer
 * gives -- an empty list, not an extension invented to fill it. */
- (NSArray<LSApplicationExtensionRecord*>*)applicationExtensionRecords
{
	return @[];
}

@end

@implementation LSApplicationExtensionRecord

/* Same "the options select kinds this database does not hold" as the application side. */
+ (NSEnumerator<LSApplicationExtensionRecord*>*)enumeratorWithOptions:(NSUInteger)options
{
	NSMutableArray<LSApplicationExtensionRecord*>* records = [NSMutableArray array];
	for (NSArray<NSString*>* row in rowsForQuery(@"select path, bundle_id from bundle where path like '%.appex'", nil))
	{
		LSApplicationExtensionRecord* record = [[[self alloc] initWithPath: row[0] bundleIdentifier: row[1]] autorelease];
		[records addObject: record];
	}
	return [records objectEnumerator];
}

/* An extension bundle is registered by nothing (see -applicationExtensionRecords), so any lookup
 * here answers not-found, which is what the initializer it inherits does once this narrower row
 * query has run and matched nothing. */
- (NSArray<NSArray<NSString*>*>*)rowsForBundleIdentifier:(NSString*)bundleIdentifier
{
	return rowsForQuery(@"select path, bundle_id from bundle where bundle_id = ? and path like '%.appex'",
		@[bundleIdentifier]);
}

/* The extension's containing application: the nearest enclosing .app directory on disk, whose own
 * bundle identifier is then looked up in the database so the answer is a real row or nothing. */
- (LSBundleRecord*)containingBundleRecord
{
	NSURL* ancestor = [_URL URLByDeletingLastPathComponent];
	while (ancestor != nil && ![[ancestor path] isEqualToString: @"/"])
	{
		if ([[ancestor pathExtension] isEqualToString: @"app"])
		{
			NSString* identifier = [[NSBundle bundleWithURL: ancestor] bundleIdentifier];
			if (identifier == nil)
				return nil;
			return [[[LSApplicationRecord alloc] initWithBundleIdentifier: identifier
									  allowPlaceholder: NO
										     error: NULL] autorelease];
		}
		ancestor = [ancestor URLByDeletingLastPathComponent];
	}
	return nil;
}

@end
