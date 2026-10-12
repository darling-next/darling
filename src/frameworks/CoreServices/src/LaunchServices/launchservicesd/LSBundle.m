/*
This file is part of Darling.

Copyright (C) 2020 Lubos Dolezel

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

#include "LSBundle.h"
#include "LSBundleRegistration.h"
#include <FSEvents/FSEvents.h>
#include <sqlite3.h>
#import <Foundation/Foundation.h>
#import <fmdb/FMDatabase.h>

#define DATABASE_VERSION 1

static FMDatabase* g_database;
extern dispatch_queue_t g_serverQueue;

static void setupDBSchema(void);
static void fsEventCallback(ConstFSEventStreamRef streamRef, void *clientCallBackInfo, size_t numEvents,
	void *eventPaths, const FSEventStreamEventFlags *eventFlags, const FSEventStreamEventId *eventIds);

static NSArray<NSString*>* MONITORED_DIRECTORIES;
static FSEventStreamRef g_eventStream;

@implementation LSBundle

@synthesize bundleId = _bundleId;

-(id) initWithBundle:(CFBundleRef) bundle
{
	_bundle = (CFBundleRef) CFRetain(bundle);

	return self;
}

-(void)dealloc
{
	CFRelease(_bundle);
	[super dealloc];
}

-(void)process
{
	_bundleId = LSBundleRegisterInDatabase(g_database, _bundle);
}

+(void)initialize
{
	if (self == [LSBundle class])
	{
		MONITORED_DIRECTORIES = @[
			// This path doesn't contain apps, but may still contain UTI definitions
			@"/System/Library/Frameworks",

			@"/Applications",
			@"/System/Applications",
		];

		FMDatabase* db = [FMDatabase databaseWithPath: @"/private/var/db/launchservices.db"];
		if (![db open])
		{
			NSLog(@"Cannot open LS database: %@", [db lastErrorMessage]);
			return;
		}
		g_database = [db retain];

		setupDBSchema();
	}
}

+(void)scanForBundles:(NSString*)dir
{
	@autoreleasepool
	{
		NSURL* url = [NSURL fileURLWithPath:dir isDirectory:YES];
		NSArray* bundles = (NSArray*) CFBundleCreateBundlesFromDirectory(NULL, (CFURLRef) url, NULL);

		for (id bundle in bundles)
		{
			// CFBundle is not exactly clever and gives us CFBundles of empty directories
			if (CFBundleGetIdentifier((CFBundleRef) bundle) == NULL)
				continue;

			@autoreleasepool
			{
				LSBundle* b = [[[LSBundle alloc] initWithBundle: (CFBundleRef) bundle] autorelease];
				[b process];
			}

			CFRelease((CFBundleRef) bundle);
		}

		[bundles release];
	}
}

+(void)scanForBundles
{
	for (NSString* dir in MONITORED_DIRECTORIES)
		[LSBundle scanForBundles: dir];
	NSLog(@"scanForBundles done\n");
}

+(void)watchForBundles
{
	g_eventStream = FSEventStreamCreate(NULL, fsEventCallback, NULL, (CFArrayRef) MONITORED_DIRECTORIES,
		kFSEventStreamEventIdSinceNow, 1, kFSEventStreamCreateFlagUseCFTypes);

	FSEventStreamSetDispatchQueue(g_eventStream, g_serverQueue);
	FSEventStreamStart(g_eventStream);
}

+(void)deleteBundleAtPath:(NSString*) path
{
	// Delete DB entries referring to path
	[g_database beginTransaction];
	[g_database executeQuery: @"delete from bundle where path = ?", path];
	[g_database commit];
}

@end

static void createDBSchema(void)
{
	NSString* sqlSchema = [NSString stringWithContentsOfFile: @"/System/Library/Frameworks/CoreServices.framework/Versions/A/Resources/launchservicesd-schema.sql"
		encoding: NSUTF8StringEncoding
		error: nil];

	const char* sql = [sqlSchema cStringUsingEncoding: NSUTF8StringEncoding];
	char* errorMessage;

	sqlite3_exec(g_database.sqliteHandle, sql, NULL, NULL, &errorMessage);

	if (errorMessage != NULL)
	{
		NSLog(@"launchservicesd: createDBSchema error: %s\n", errorMessage);
		sqlite3_free(errorMessage);

		// TODO: abort_with_payload() instead?
		exit(1);
	}
}

static void setupDBSchema(void)
{
	FMResultSet* rs = [g_database executeQuery:@"select value from `globals` where key = ?", @"version"];
	if (![rs next])
	{
		createDBSchema();
	}
	else
	{
		if ([rs intForColumn: @"value"] != DATABASE_VERSION)
		{
			// TODO: This is where we'll do DB schema updates or possibly just delete everything and index from scratch
		}
		[rs close];
	}
}

static void fsEventCallback(ConstFSEventStreamRef streamRef, void *clientCallBackInfo, size_t numEvents,
	void *eventPaths, const FSEventStreamEventFlags *eventFlags, const FSEventStreamEventId *eventIds)
{
	NSArray<NSString*>* changes = (NSArray*) eventPaths;
	int index = 0;
	for (NSString* change in changes)
	{
		@autoreleasepool
		{
			if (eventFlags[index] & kFSEventStreamEventFlagItemIsFile)
			{
				if ([[change lastPathComponent] caseInsensitiveCompare: @"Info.plist"] == NSOrderedSame)
				{
					NSString* bundlePath = [[change stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
					CFBundleRef bundle = CFBundleCreate(NULL, (CFURLRef) [NSURL fileURLWithPath:bundlePath isDirectory:YES]);

					if (bundle != NULL)
					{
						LSBundle* lsb = [[[LSBundle alloc] initWithBundle: bundle] autorelease];
						[lsb process];

						CFRelease(bundle);
					}
					else
					{
						[LSBundle deleteBundleAtPath:bundlePath];
					}
				}

				// TODO: If somebody renames the bundle's directory, we still need to pick it up
			}
		}
		index++;
	}
}
