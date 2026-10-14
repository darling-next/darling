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

/* /usr/lib/liboah.dylib: WHO NEEDS IT, WHAT IT BINDS, AND WHY ONLY ONE NAME IS EXPORTED.
 *
 * MEASURED NEED, 2026-10-14. Xcode 26.3's initial plug-in scan cannot finish because the iOS
 * platform plug-in will not load:
 *   "Failed to load code for plug-in com.apple.dt.IDE.IDEiOSSupportCore ... (dlopen_preflight(
 *    .../Contents/PlugIns/IDEiOSSupportCore.framework/IDEiOSSupportCore): Library not loaded:
 *    /usr/lib/liboah.dylib  Referenced from: .../IDEiOSSupportCore  Reason: image not found)"
 * xcodebuild then takes its error path and never reaches a compiler.
 *
 * SIX images in that Xcode contain the string "/usr/lib/liboah.dylib", and they are NOT all the same
 * shape -- the load commands and the binding tables were read per image (llvm-objdump --macho
 * --dylibs-used / --chained-fixups / --dyld-info, llvm-nm -u):
 *
 *   Contents/PlugIns/IDEiOSSupportCore.framework/Versions/A/IDEiOSSupportCore
 *       LC_LOAD_DYLIB /usr/lib/liboah.dylib -- STRONG (ordinal 3), and the image's 608-entry chained
 *       import table contains ZERO entries for that ordinal. It needs the image to EXIST and binds
 *       NOTHING from it. This is the load the wall above is about.
 *   Contents/Frameworks/IDEFoundation.framework/Versions/A/IDEFoundation
 *       LC_LOAD_WEAK_DYLIB (ordinal 10), 0 imports -- a missing weak link would be skipped anyway.
 *   Contents/PlugIns/IDEInterfaceBuilderCocoaTouchIntegration.framework/Versions/A/
 *   IDEInterfaceBuilderCocoaTouchIntegration
 *       LC_LOAD_DYLIB (ordinal 23) -- STRONG, and exactly ONE import:
 *       _oah_is_current_process_translated, bound eagerly through __DATA_CONST,__got. THE SYMBOL
 *       BELOW IS FOR THIS IMAGE.
 *   Contents/SharedFrameworks/SymbolicationDT.framework/Versions/A/SymbolicationDT
 *       no load command; it dlopen()s the absolute path at run time inside
 *       ___findOahFuncs_block_invoke and dlsym()s "oah_get_x86_thread_state" and
 *       "oah_get_runtime_location". The disassembly tests the dlopen result for NULL and tests the
 *       second dlsym for NULL before calling it, so a missing handler is a path this client already
 *       has.
 *   Contents/SharedFrameworks/DVTInstrumentsFoundation.framework/Versions/A/Resources/
 *   liboainject.dylib and .../RemoteInjectionAgent
 *       no load command either; the path is a dlopen string in the "inject into a TRANSLATED process"
 *       path, which this guest never takes.
 *
 * THE REST OF THE DECLARED SURFACE IS NOT EXPORTED, ON PURPOSE. The only liboah.tbd in the whole
 * Xcode tree is the iPhoneOS SDK stub, and it declares 41 names -- the Rosetta/AOT translation
 * machinery (aot_*, oah_*, rosetta_*, plus objc_thread_get_rip). This tree has no provider for them
 * (grep: no definition of any name beginning aot_ or oah_ anywhere under darling/src;
 * objc_thread_get_rip is
 * CALLED by src/external/objc4/runtime/objc-cache.mm but only inside
 * `#if TARGET_OS_OSX && !defined(DARLING)`, so the Darling libobjc does not reference it). Their
 * signatures are not recorded anywhere this host can read, and a guessed one would be an invented
 * implementation answering wrongly. An image with no such surface cannot answer wrongly: the first
 * client that binds one fails at LOAD, loudly. libAppleArchive (src/libAppleArchive) is the same
 * policy for a library with no symbol of its own at all; libRosetta (src/libRosetta) is the same
 * one-symbol shape as this file.
 *
 * THE CALLER'S ABI, from the call site rather than from a guess. In
 * IDEInterfaceBuilderCocoaTouchIntegration the call is
 *  12ba49: callq _oah_is_current_process_translated
 *  12ba4e: testb %al, %al
 *  12ba50: je    ...
 * with no argument registers set up, so the signature is bool f(void).
 *
 * THE ANSWER IS NOT A STUB. Rosetta translates x86_64 code to arm64 on Apple silicon, and liboah is
 * the runtime that answers "am I being translated?". Darling's guest is x86_64 code running natively
 * on an x86_64 host, so this process is not being translated, and "not translated" is the honest
 * answer rather than a value chosen to make a caller take one path.
 *
 * NAME: the C identifier carries no leading underscore, so the Mach-O linker symbol is exactly the
 * caller's _oah_is_current_process_translated.
 */
bool oah_is_current_process_translated(void)
{
	return false;
}
