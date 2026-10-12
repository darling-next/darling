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

#include <unistd.h>

/* A LOUD PLACEHOLDER, not an implementation. Read this before adding anything to this file.
 *
 * MEASURED NEED, 2026-10-12, while walking Xcode 26.3's load-time dependency ladder in a guest.
 * /usr/lib/libAppleArchive.dylib has exactly ONE referrer in the whole of Xcode:
 * SharedFrameworks/DebugSymbolsDT.framework. The reference is a NON-WEAK LC_LOAD_DYLIB demanding
 * compatibility version 1.0.0, current version 1.0.0, and the image died at load with
 *   "Library not loaded: /usr/lib/libAppleArchive.dylib  Referenced from:
 *    .../SharedFrameworks/DebugSymbolsDT.framework/Versions/A/DebugSymbolsDT  Reason: image not found"
 *
 * WHAT THE ONE REFERENCE IS, measured rather than assumed: NOTHING IS CALLED. DebugSymbolsDT binds
 * ZERO symbols from this library -- zero eager binds, zero lazy binds, zero weak binds. The image is
 * linked and loaded and no function in it is ever reached.
 *
 * AN EARLIER MEASUREMENT OF MINE SAID DVTFoundation ALSO REFERENCED IT, AND THAT WAS A SUBSTRING
 * ERROR: DVTFoundation links /usr/lib/swift/libswiftAppleArchive.dylib, the Swift overlay, not this C
 * library, and that link is marked WEAK in its load command. The overlay repo does not ship
 * libswiftAppleArchive, which is harmless precisely because the link is weak -- dyld skips a missing
 * weak link. So the only hard AppleArchive reference anywhere in the chain is the symbol-free one
 * above, and the overlay question is resolved rather than open.
 *
 * WHY AN EMPTY SURFACE IS THE HONEST ANSWER HERE: the real library is the Apple Archive format API --
 * AAArchiveStream and the extraction/compression operations. That is archive manipulation, it is not
 * implemented in Darling, and it is filed as separate work. Shipping a guessed AA* symbol would be
 * inventing an implementation and could answer wrongly; shipping nothing at all would leave
 * DebugSymbolsDT -- Xcode's own debug-symbols framework, the kind of thing an ordinary build reaches
 * -- unloadable. An image with NO symbol surface cannot answer wrongly: the first client that binds a
 * real symbol fails at LOAD, loudly. The notice below is what makes the image self-describing rather
 * than a blank file.
 *
 * IF YOU ARE HERE TO IMPLEMENT APPLE ARCHIVE: replace this notice and the empty surface together, and
 * delete this comment's "placeholder" framing. Until then the notice must stay, and it must stay
 * unconditional -- a reader who sees this library load deserves to know what it is without setting an
 * environment variable.
 */
__attribute__((constructor))
static void darling_libAppleArchive_is_not_implemented(void)
{
	static const char notice[] =
		"Darling: libAppleArchive is not implemented; this image only satisfies a load-time "
		"dependency (DebugSymbolsDT links it and calls nothing in it). Any bind of an Apple "
		"Archive symbol will fail.\n";
	/* write(2, ...) rather than fprintf(stderr, ...): this runs during initializer processing, and a
	 * raw descriptor write needs no stream setup, so the notice cannot be lost to an uninitialized or
	 * redirected stderr. It is also why this library links libSystem at all. */
	(void) write(2, notice, sizeof(notice) - 1);
}
