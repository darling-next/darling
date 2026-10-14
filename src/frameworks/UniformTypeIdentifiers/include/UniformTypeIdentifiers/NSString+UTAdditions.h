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

#include <Foundation/Foundation.h>
#include <UniformTypeIdentifiers/UTType.h>

@interface NSString (UTAdditions)

// Apple's UTAdditions.h declares two members here. Only this one is sent by any image of the
// workload's load closure (XCResultKit and XCTHarness reference the selector; nothing references
// -stringByAppendingPathComponent:conformingToType:), so only this one is implemented. The
// documentation the body follows is that header's.
- (NSString *)stringByAppendingPathExtensionForType:(UTType *)contentType;

@end
