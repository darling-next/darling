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

#import <CoreLocation/CLSimulationManager.h>

/* A DOCUMENTED METHOD-FREE CLASS, and here the tree had already decided that: the header this
 * implements (include/CoreLocation/CLSimulationManager.h) declares
 * @interface CLSimulationManager : NSObject with no methods at all. This file only supplies the class
 * SYMBOL.
 *
 * MEASURED NEED, 2026-10-12, while walking Xcode 26.3's load-time dependency ladder in a guest.
 * DVTInstrumentsFoundation binds _OBJC_CLASS_$_CLSimulationManager eagerly through __DATA
 * __objc_classrefs, alongside _OBJC_CLASS_$_CLLocation -- the only two classes of this framework it
 * wants, and the two the tree had headers for but no implementations. The client sends this class
 * NOTHING: zero references anywhere in the image's code, only the classref. A simulation manager that
 * guessed at simulated locations would be an invented implementation; method-free, it answers nothing
 * rather than answering wrongly.
 *
 * IF A FUTURE CALLER NEEDS BEHAVIOUR, THIS FILE IS WHERE IT GOES, together with the header's interface.
 */
@implementation CLSimulationManager

@end
