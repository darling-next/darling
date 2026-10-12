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
 * The LaunchServices registration operation, in ONE place: one bundle's row plus the UTI,
 * document-type and URL-type claims it declares, written into /private/var/db/launchservices.db.
 *
 * WHO RUNS IT. lsd, at scan time -- launchservicesd/LSBundle.m walks /System/Library/Frameworks,
 * /Applications and /System/Applications and registers every bundle it finds -- and the
 * LaunchServices framework, on demand, for __LSRegisterURLWithOptions: the caller asks for a bundle
 * OUTSIDE the directories lsd scans. Both run THIS code, so the database keeps one writer convention
 * (the SQL, the transaction and the "Info.plist unchanged" test are lsd's, moved here unchanged)
 * instead of gaining a second, framework-private one.
 *
 * WHAT IT NEEDS, AND WHAT IT REFUSES. A bundle whose CFBundleGetIdentifier is non-NULL (the same
 * filter lsd's scan applies -- CFBundle hands back bundles for empty directories) and a database that
 * already carries lsd's schema. The database is lsd's: lsd opens it read-write and creates the schema
 * at start (LSBundle.m setupDBSchema/createDBSchema). This code deliberately does NOT create the
 * schema -- a second copy of it would be a second authority on the database -- so a caller that finds
 * no database reports that as its own failure rather than writing one here.
 */

#include "LSBundleRegistration.h"
#import <Foundation/Foundation.h>
#import <fmdb/FMDatabase.h>
#include <LaunchServices/UTType.h>
#include <zlib.h>

@implementation NSString (CRC32)
-(uint32_t)crc32
{
	const char* str = [self UTF8String];
	return crc32(0, (const Bytef*) str, strlen(str));
}
@end

// An SQL-safe fourcc string
static NSString* fourcc(UInt32 code)
{
	char str[5];
	str[0] = (code >> 24) & 0xff;
	str[1] = (code >> 16) & 0xff;
	str[2] = (code >> 8) & 0xff;
	str[3] = code & 0xff;
	str[4] = '\0';
	return [NSString stringWithCString:str encoding:NSASCIIStringEncoding];
}

static void processUTIs(FMDatabase* db, NSArray<NSDictionary<NSString*,id>*>* utis, NSNumber* bundleId)
{
	// Maintain a set of previously existing UTIs so that we know which ones need to be deleted
	NSMutableSet<NSString*>* previousUtis = [NSMutableSet setWithCapacity: 0];

	FMResultSet* rs = [db executeQuery:@"select type_identifier from uti where bundle = ?",
		bundleId];

	while ([rs next])
		[previousUtis addObject: [rs stringForColumn: @"type_identifier"]];
	[rs close];

	for (NSDictionary<NSString*,id>* uti in utis)
	{
		NSNumber* utiId;
		NSString* typeId = uti[(NSString*) kUTTypeIdentifierKey];

		if (![typeId isKindOfClass: [NSString class]])
			continue;

		NSString* description = uti[(NSString*) kUTTypeDescriptionKey];
		if (![description isKindOfClass: [NSString class]])
			description = typeId;

		FMResultSet* rs = [db executeQuery:@"select id, description from uti where type_identifier = ? and bundle = ?",
			typeId, bundleId];
		if ([rs next])
		{
			// We already have this UTI record
			utiId = [NSNumber numberWithInt: [rs intForColumn:@"id"]];
			NSString* oldDesc = [rs stringForColumn:@"description"];

			// Update the description text if it changed
			if (![oldDesc isEqualToString: description])
			{
				[db executeUpdate:@"update uti set description = ? where id = ?", oldDesc, utiId];
			}
		}
		else
		{
			// Insert a new UTI record
			[db executeUpdate:@"insert into uti (type_identifier, description, bundle) values (?,?,?)",
				typeId, description, bundleId];
			utiId = [NSNumber numberWithInt: [db lastInsertRowId]];
		}
		[rs close];

		[previousUtis removeObject: typeId];

		// Process UTTypeConformsTo
		[db executeUpdate: @"delete from uti_conforms where uti = ?", utiId];

		id conformsTo = uti[(NSString*) kUTTypeConformsToKey];
		if ([conformsTo isKindOfClass: [NSString class]])
		{
			[db executeUpdate:@"insert into uti_conforms (uti, conforms_to) values (?,?)",
				utiId, conformsTo];
		}
		else if ([conformsTo isKindOfClass: [NSArray class]])
		{
			for (NSString* ct in ((NSArray<NSString*>*) conformsTo))
			{
				if (![ct isKindOfClass: [NSString class]])
					continue;
				[db executeUpdate:@"insert into uti_conforms (uti, conforms_to) values (?,?)",
					utiId, ct];
			}
		}

		// Process UTTypeIconFile / UTTypeIconFiles
		[db executeUpdate: @"delete from uti_icon where uti = ?", utiId];
		NSString* icon = uti[(NSString*) kUTTypeIconFileKey];
		if (icon != nil)
		{
			[db executeUpdate:@"insert into uti_icon (uti, file) values (?,?)",
				utiId, icon];
		}
		NSArray<NSString*>* icons = uti[@"UTTypeIconFiles"];
		if (icons != nil)
		{
			for (NSString* icon in icons)
			{
				if (![icon isKindOfClass: [NSString class]])
					continue;
				[db executeUpdate:@"insert into uti_icon (uti, file) values (?,?)",
					utiId, icon];
			}
		}

		// Process UTTypeTagSpecification
		[db executeUpdate: @"delete from uti_tag where uti = ?", utiId];
		NSDictionary<NSString*, id>* tags = uti[(NSString*) kUTTypeTagSpecificationKey];
		if (tags != nil)
		{
			for (NSString* tag in tags)
			{
				id tagValue = tags[tag];
				if ([tagValue isKindOfClass: [NSString class]])
				{
					[db executeUpdate:@"insert into uti_tag (uti, tag, value) values (?,?,?)",
						utiId, tag, tagValue];
				}
				else if ([tagValue isKindOfClass: [NSArray class]])
				{
					for (NSString* value in (NSArray<NSString*>*) tagValue)
					{
						if (![value isKindOfClass: [NSString class]])
							continue;
						[db executeUpdate:@"insert into uti_tag (uti, tag, value) values (?,?,?)",
							utiId, tag, value];
					}
				}
			}
		}
	}

	for (NSString* deletedId in previousUtis)
	{
		[db executeUpdate:@"delete from uti where type_identifier = ? and bundle = ?",
			deletedId, bundleId];
	}
}

// https://developer.apple.com/documentation/bundleresources/information_property_list/cfbundledocumenttypes?language=objc
static void processFileAssociation(FMDatabase* db, NSDictionary* dict, NSNumber* bundleId)
{
	NSNumber* appDocId;

	NSString* iconFile = dict[@"CFBundleTypeIconFile"];
	NSString* displayName = dict[@"CFBundleTypeName"];
	NSString* role = dict[@"CFBundleTypeRole"];
	NSString* rank = dict[@"LSHandlerRank"];
	NSString* documentClass = dict[@"NSDocumentClass"];

	// TODO: exportable types?

	if (!role)
		role = @"None";
	if (!rank)
		rank = @"Default";

	[db executeUpdate:@"insert into app_doc (icon,name,role,rank,class,bundle) values (?,?,?,?,?,?)",
		iconFile, displayName, role, rank, documentClass, bundleId];
	appDocId = [NSNumber numberWithInt: [db lastInsertRowId]];

	NSArray<NSString*>* contentTypes = dict[@"LSItemContentTypes"];
	if (contentTypes)
	{
		for (NSString* uti in contentTypes)
		{
			if (![uti isKindOfClass: [NSString class]])
				continue;

			[db executeUpdate:@"insert into app_doc_uti (doc, uti) values (?,?)", appDocId, uti];
		}
	}
	else
	{
		// Support for obsolete CFBundleTypeExtensions and CFBundleTypeMIMETypes
		NSArray<NSString*>* extensions = dict[@"CFBundleTypeExtensions"];

		if (extensions)
		{
			for (NSString* extension in extensions)
			{
				if (![extension isKindOfClass: [NSString class]])
					continue;

				[db executeUpdate:@"insert into app_doc_extension (doc, extension) values (?,?)", appDocId, extension];
			}
		}

		NSArray<NSString*>* mimeTypes = dict[@"CFBundleTypeMIMETypes"];

		if (mimeTypes)
		{
			for (NSString* mime in mimeTypes)
			{
				if (![mime isKindOfClass: [NSString class]])
					continue;

				[db executeUpdate:@"insert into app_doc_mime (doc, mime) values (?,?)", appDocId, mime];
			}
		}
	}
}

static void processFileAssociations(FMDatabase* db, NSDictionary<NSString*,id>* infoDict, NSNumber* bundleId)
{
	[db executeUpdate:@"delete from app_doc where bundle = ?", bundleId];

	NSArray<NSDictionary*>* types = (NSArray*) infoDict[@"CFBundleDocumentTypes"];
	if (types)
	{
		for (NSDictionary* type in types)
			processFileAssociation(db, type, bundleId);
	}
	if (infoDict[@"CFBundleTypeRole"] != nil)
	{
		processFileAssociation(db, infoDict, bundleId);
	}
}

static void processURLTypes(FMDatabase* db, NSDictionary* infoDict, NSNumber* bundleId)
{
	NSArray<NSDictionary*>* urlTypes = (NSArray*) infoDict[@"CFBundleURLTypes"];

	[db executeUpdate:@"delete from bundle_url_type where bundle = ?", bundleId];
	if (urlTypes != nil)
	{
		for (NSDictionary<NSString*, id>* type in urlTypes)
		{
			NSString* role = type[@"CFBundleTypeRole"];
			NSString* iconFile = type[@"CFBundleURLTypes"];
			NSString* name = type[@"CFBundleURLName"];

			if (role == nil)
				role = @"None";

			NSArray<NSString*>* schemes = type[@"CFBundleURLSchemes"];

			[db executeUpdate:@"insert into bundle_url_type (bundle, role, name, icon) values (?,?,?,?)",
				bundleId, role, name, iconFile];

			if (schemes)
			{
				NSNumber* typeId = [NSNumber numberWithInt:[db lastInsertRowId]];

				for (NSString* scheme in schemes)
					[db executeUpdate:@"insert into bundle_url_type_scheme (type, scheme) values (?,?)", typeId, scheme];
			}
		}
	}
}

/* (bundle id, needs processing) for the bundle's `bundle` row: insert a new row, update an existing
 * one whose Info.plist changed, or report the row as-is when the checksum still matches. */
static BOOL setupBundleID(FMDatabase* db, CFBundleRef bundle, int* outBundleId)
{
	NSURL* url = (NSURL*) CFBundleCopyBundleURL(bundle);
	NSString* path = [url path];
	NSDictionary* infoDict = (NSDictionary*) CFBundleGetInfoDictionary(bundle);
	const uint32_t newChecksum = [[infoDict description] crc32];
	int bundleId = 0;

	[url release];
	FMResultSet* rs = [db executeQuery:@"select id, checksum from bundle where path = ?", path];

	if ([rs next])
	{
		bundleId = [rs intForColumn:@"id"];

		// Has the info dict changed?
		uint32_t dbChecksum = [rs intForColumn:@"checksum"];
		if (dbChecksum == newChecksum)
		{
			NSLog(@"Bundle at '%@' hasn't changed\n", path);
			[rs close];
			*outBundleId = bundleId;
			return FALSE;
		}
	}

	[rs close];

	UInt32 packageType = 0, packageCreator = 0;
	CFBundleGetPackageInfo(bundle, &packageType, &packageCreator);

	NSString* packageTypeStr = nil;
	NSString* packageCreatorStr = nil;
	NSString* bundleSignature = infoDict[@"CFBundleSignature"];

	if (packageType != 0)
		packageTypeStr = fourcc(packageType);
	if (packageCreator != 0)
		packageCreatorStr = fourcc(packageCreator);

	if (!bundleId)
	{
		CFStringRef identifier = CFBundleGetIdentifier(bundle);

		NSLog(@"Registering new bundle at '%@', identifier '%@'\n", path, identifier);

		[db executeUpdate:@"insert into bundle (path, bundle_id, checksum, package_type, creator, signature) values (?,?,?,?,?,?)",
			path, identifier, [NSNumber numberWithInt:newChecksum], packageTypeStr, packageCreatorStr, bundleSignature];

		bundleId = [db lastInsertRowId];
	}
	else
	{
		NSLog(@"Updating bundle at '%@'\n", path);

		// We're in a transaction, so it's OK to set the new checksum now
		[db executeUpdate:@"update bundle set checksum = ?, package_type = ?, creator = ?, signature = ? where id = ?",
			[NSNumber numberWithInt:newChecksum], packageTypeStr, packageCreatorStr,
			bundleSignature, [NSNumber numberWithInt: bundleId]];
	}

	*outBundleId = bundleId;
	return TRUE;
}

int LSBundleRegisterInDatabase(FMDatabase* db, CFBundleRef bundle)
{
	int bundleId = 0;
	BOOL needsProcessing;

	[db beginTransaction];
	needsProcessing = setupBundleID(db, bundle, &bundleId);

	if (!needsProcessing)
	{
		[db rollback];
		return bundleId;
	}

	NSDictionary<NSString*,id>* infoDict = (NSDictionary*) CFBundleGetInfoDictionary(bundle);
	NSNumber* ourBundleId = [NSNumber numberWithInt: bundleId];

	NSArray<NSDictionary*>* utis = infoDict[(NSString*) kUTExportedTypeDeclarationsKey];
	if (utis != nil)
	{
		processUTIs(db, utis, ourBundleId);
	}

	processFileAssociations(db, infoDict, ourBundleId);
	processURLTypes(db, infoDict, ourBundleId);
	[db commit];

	return bundleId;
}
