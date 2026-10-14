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

#import <UniformTypeIdentifiers/NSString+UTAdditions.h>

@implementation NSString (UTAdditions)

// Semantics: Apple's SDK header (MacOSX.sdk .../UniformTypeIdentifiers.framework/Headers/
// UTAdditions.h, quoted in this rung's evidence) -- "If the receiver already has a path extension,
// and that path extension is valid for file system objects of type contentType, no additional
// extension is appended to the receiver before constructing the result ... If the extension could
// not be appended, this method returns a copy of self."
- (NSString *)stringByAppendingPathExtensionForType:(UTType *)contentType
{
	if (![contentType isKindOfClass: [UTType class]])
		return [[self copy] autorelease];

	// An extension the receiver already carries is kept when a type with that extension conforms to
	// the requested one; otherwise the type's own preferred extension is appended.
	NSString *extension = [self pathExtension];
	if ([extension length] > 0 && [[UTType typeWithFilenameExtension: extension] conformsToType: contentType])
		return [[self copy] autorelease];

	NSString *preferred = [contentType preferredFilenameExtension];
	if ([preferred length] == 0)
		return [[self copy] autorelease];
	return [self stringByAppendingPathExtension: preferred];
}

@end
