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

#include <stdbool.h>

/* The single symbol this library exists for, and the TRUE answer on this machine.
 *
 * MEASURED NEED: Xcode 26.3's SharedFrameworks/DVTFoundation.framework hard-links
 * /usr/lib/libRosetta.dylib (compatibility version 1.0.0, current version 1.0.0), and
 * `llvm-nm -u DVTFoundation` lists exactly one symbol from it --
 * _rosetta_is_current_process_translated, and nothing else. Three images in that Xcode reference the
 * dylib (DVTFoundation, SharedFrameworks/AssetRuntime/DVTFoundation, CoreSymbolicationDT). With the
 * symbol absent, a minimal xcodebuild project died at load time with
 *   "Library not loaded: /usr/lib/libRosetta.dylib ... Referenced from: .../DVTFoundation ... Reason:
 *    image not found"
 * before it ever reached a compiler.
 *
 * THE CALLER'S ABI, from the call site rather than from a guess: in _DVTNativeHostArchitectureString
 * the call is
 *   5d51b: callq _rosetta_is_current_process_translated
 *   5d52e: testb %al, %al
 * with no argument registers set up, so the signature is bool f(void).
 *
 * THE ANSWER IS NOT A STUB. Rosetta translates x86_64 code to arm64 on Apple silicon. Darling's guest
 * is x86_64 code running natively on an x86_64 host, so this process is not being translated, and
 * "not translated" is the honest answer rather than a value chosen to make a caller take one path.
 * If Darling ever runs a translated guest, this function has to report that truthfully instead.
 *
 * NAME: the C identifier carries no leading underscore, so the Mach-O linker symbol is exactly the
 * caller's _rosetta_is_current_process_translated. Writing the identifier with a leading underscore
 * produced __rosetta_is_current_process_translated, which satisfied nobody.
 */
bool rosetta_is_current_process_translated(void)
{
	return false;
}
