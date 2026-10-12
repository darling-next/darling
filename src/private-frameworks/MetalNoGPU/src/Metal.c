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

#include <stddef.h>

/* A TRUE-ANSWER implementation, the same shape as libRosetta. These functions are NOT stubs and must
 * not print a "STUB:" line: they are reporting a fact about this machine, which is that the guest has
 * no GPU, so no Metal device exists. A placeholder stands in for work that does not exist behind it;
 * these answers are the truth, and a reader must be able to tell the two apart by the text alone.
 *
 * MEASURED NEED, 2026-10-12, while walking Xcode 26.3's load-time dependency ladder in a guest.
 * DVTInstrumentsFoundation links /System/Library/Frameworks/Metal.framework/Versions/A/Metal STRONGLY
 * (compatibility version 1.0.0, current version 370.63.7) and binds seven symbols, all EAGERLY through
 * __DATA_CONST __got -- zero lazy binds, so all seven must exist at load whether or not they are
 * called. With the framework absent the image died at load:
 *   "Library not loaded: /System/Library/Frameworks/Metal.framework/Versions/A/Metal  Referenced from:
 *    .../SharedFrameworks/DVTInstrumentsFoundation.framework/Versions/A/DVTInstrumentsFoundation"
 *
 * THE SEVEN, SPLIT BY KIND, because the kinds behave differently:
 *   four CALLS, every one of them actually called by the client:
 *     _MTLCreateSystemDefaultDevice       2 call sites
 *     _MTLCopyAllDevices                  4
 *     _MTLCopyAllDevicesWithObserver      1
 *     _MTLRemoveDeviceObserver            2
 *   three DATA -- notification names, bound but never called.
 *
 * WHY AN EMPTY ANSWER IS THE TRUE ONE: neither call site of _MTLCreateSystemDefaultDevice checks for
 * NULL. One passes the result straight into an Objective-C message send; the other sends
 * -[%rdi _removeGPU:] to it. Messaging nil is a no-op, so the client tolerates an empty answer without
 * any test of its own -- the second site is literally asking the device to remove itself from a list
 * and does nothing when there is no device. So "there is no device" is a fact this guest can state,
 * not a value chosen to make a caller take one path.
 *
 * THE ALTERNATIVE, for the record: the tree HAS a real Metal (src/external/metal, Vulkan-backed,
 * this host has Vulkan) but it is gated behind COMPONENT_dev_gui_common and not built in this
 * configuration. Enabling it is a build-configuration decision, filed separately, and if it happens
 * this library must be removed -- two images cannot claim the same install name.
 */

void *MTLCreateSystemDefaultDevice(void)
{
	/* No GPU in this guest: there is no default device, and that is the answer. */
	return NULL;
}

void *MTLCopyAllDevices(void)
{
	/* No GPU: the list of devices is empty. Returning NULL is how this API says "none" to a
	 * caller that, as measured above, tolerates it. */
	return NULL;
}

void *MTLCopyAllDevicesWithObserver(void *observer, void *handler)
{
	/* Nothing to observe, so no observer is registered. */
	(void) observer;
	(void) handler;
	return NULL;
}

void MTLRemoveDeviceObserver(void *observer)
{
	/* A no-op because there are no devices and therefore no observers to remove -- correct rather
	 * than a stub. Do not "fix" this by making it do something. */
	(void) observer;
}

/* THE THREE NOTIFICATION NAMES.
 *
 * We do not know Apple's actual values for these, and it does not matter: nothing in this guest posts
 * them, and every client reads the same symbol, so any distinct value is equivalent and consistency is
 * guaranteed by the symbol itself. The values below are arbitrary and stable; do not treat them as
 * authoritative. A client that registers for them simply never receives one, which is consistent with
 * there being no devices to add or remove.
 */
const char *MTLDeviceWasAddedNotification = "MTLDeviceWasAddedNotification";
const char *MTLDeviceWasRemovedNotification = "MTLDeviceWasRemovedNotification";
const char *MTLDeviceRemovalRequestedNotification = "MTLDeviceRemovalRequestedNotification";
