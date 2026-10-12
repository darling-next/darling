/*
 This file is part of Darling.

 Copyright (C) 2026 Darling contributors

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

#import <CoreLocation/CLLocation.h>

/* A DOCUMENTED METHOD-FREE CLASS, and here the tree had already decided that: the header this
 * implements (include/CoreLocation/CLLocation.h) declares @interface CLLocation : NSObject with no
 * methods at all, so method-free is not a choice made here but the shape that was already written
 * down. This file only supplies the class SYMBOL.
 *
 * MEASURED NEED, 2026-10-12, while walking Xcode 26.3's load-time dependency ladder in a guest.
 * DVTInstrumentsFoundation binds _OBJC_CLASS_$_CLLocation eagerly through __DATA __objc_classrefs.
 * The header existed; the implementation did not, so nothing provided the class and the image failed
 * at load with the framework absent. The client sends the class NOTHING: zero references to CLLocation
 * anywhere in the image's code, only the classref. By the same measure it binds 223 classrefs there in
 * total, so this is the ordinary shape rather than a warning about this class.
 *
 * Guessing location semantics would be the AA*-signature mistake one class wide; left method-free, any
 * future caller gets ordinary Objective-C defaults rather than a plausible wrong answer.
 *
 * IF A FUTURE CALLER NEEDS BEHAVIOUR, THIS FILE IS WHERE IT GOES, together with the header's interface.
 */
@implementation CLLocation

@end
