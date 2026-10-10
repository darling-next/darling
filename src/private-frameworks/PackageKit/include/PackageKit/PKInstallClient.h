/*
This file is part of Darling.

Copyright (C) 2017 Lubos Dolezel

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

@interface PKInstallClient : NSObject

@end

/*
 * Measured, not decorative: Xcode 26.3's DVTSystemPrerequisites.framework references
 * _PKInstallErrorDomain as an undefined DATA symbol, so a prefix whose PackageKit does not export it
 * fails to load that framework with "Symbol not found: _PKInstallErrorDomain" -- which is how a real
 * xcodebuild stops here. The value is the domain string Apple's own PackageKit reports in its errors,
 * observed in real install failures: "Error Domain=PKInstallErrorDomain Code=...".
 */
extern NSString * const PKInstallErrorDomain;
