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

#import <LoggingSupport/OSLogDeserializedEventStream.h>

/* A DOCUMENTED METHOD-FREE CLASS. Read this before adding anything to it.
 *
 * MEASURED NEED, 2026-10-12, while walking Xcode 26.3's load-time dependency ladder in a guest.
 * DVTInstrumentsFoundation links /System/Library/PrivateFrameworks/LoggingSupport.framework and binds
 * SEVEN classrefs from it, all eagerly through __DATA __objc_classrefs. Six of the seven are full
 * implementations elsewhere in this component (OSLogEventLiveStore.m, OSLogEventLiveStream.m,
 * OSLogEventStore.m, OSLogEventStream.m, OSLogPreferencesCategory.m, OSLogPreferencesSubsystem.m).
 * This class was the seventh and the only one the tree did not have, so without it the image loads and
 * then fails on an eager classref -- which is why the fix is a class and not a declaration.
 *
 * WHY IT IS METHOD-FREE BY DECISION, not by omission. The client binds the classref and sends it
 * NOTHING: zero selectors reference it anywhere in the image (objc metadata and disassembly both
 * return nothing; the only other trace is an ivar _deserializedEventStream on the client's own
 * DTOSLogLoader, which holds a value the build path never fills). The classes that ARE used by this
 * client, OSLogEventStream and OSLogEventStore among them, also show zero references in code, so this
 * is the ordinary shape here and not a warning sign about this class in particular.
 *
 * Inventing behaviour would be guessing: a subclass of OSLogEventStream, or an init that pretends to
 * deserialize, would look like it knows what the class means and would be wrong in a way nobody could
 * see. That is the same mistake as guessing an AA* signature, one class wide instead of one symbol
 * wide. Left method-free, any future caller gets ordinary Objective-C defaults rather than a
 * plausible wrong answer, and the absence of behaviour is deliberate and named rather than accidental.
 *
 * LOUDNESS NOTE: this family makes placeholders loud, but loudness cannot be a load-time print here --
 * Darling's dyld does not run initializers for late-loaded images (filed as dar-mr18). So the
 * loudness is documentation: this comment, the commit message, and the filed task for the real
 * deserialization. Same mechanism, same reason, as libAppleArchive's notice.
 *
 * IF A FUTURE CALLER NEEDS BEHAVIOUR, THIS FILE IS WHERE IT GOES. Implement it for real then, with the
 * measurement of what that caller demands; do not grow this comment into a promise it cannot keep.
 */
@implementation OSLogDeserializedEventStream

@end
