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

/*
 * ASI_GetCurrentCPUDescriptionWithFormat.
 *
 * This is real behaviour, not a stub: it answers from data the guest actually has. Everything below
 * was measured rather than assumed, because this function exists only because a client binds it.
 *
 * The client (Xcode 26.3's DVTFoundation, the only one that binds it) calls it like this:
 *
 *     movl $0x10000, %edi
 *     callq _ASI_GetCurrentCPUDescriptionWithFormat
 *     testq %rax, %rax
 *     cmovneq %rax, %rdx          ; NULL falls back to a literal the client owns
 *     movq %r15, %rdi
 *     callq *%r13                 ; the client releases the result
 *
 * So: ONE integer argument (the client passes 0x10000), and the result is a CF object the caller
 * owns. TWO THINGS ARE NOT OBSERVABLE from that call site and are named here rather than guessed
 * silently: whether the argument is a maximum length or a flags word (it is treated as a cap, which
 * is harmless if it is flags), and what "WithFormat" refers to, since no format is passed (the
 * format applied is the one below).
 *
 * The data: the guest answers hw.machine ("x86_64") and hw.ncpu (12), and does NOT answer
 * machdep.cpu.brand_string or hw.cpufrequency -- the former is an unknown oid and the latter
 * segfaults sysctl -- so no brand string or clock is invented here. When neither key answers, this
 * returns NULL and the client uses its own fallback, which is the honest answer rather than a
 * well-formed string built from nothing.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <CoreFoundation/CoreFoundation.h>

void* ASI_GetCurrentCPUDescriptionWithFormat(uint32_t maxLength) {
    char machine[64];
    int ncpu = 0;
    size_t machineLength = sizeof(machine);
    size_t ncpuLength = sizeof(ncpu);

    machine[0] = '\0';
    if (sysctlbyname("hw.machine", machine, &machineLength, NULL, 0) != 0) {
        machine[0] = '\0';
    }
    machine[sizeof(machine) - 1] = '\0';

    if (sysctlbyname("hw.ncpu", &ncpu, &ncpuLength, NULL, 0) != 0) {
        ncpu = 0;
    }

    if (machine[0] == '\0' && ncpu <= 0) {
        /* Nothing real to report; the client handles NULL (measured above). */
        return NULL;
    }

    CFStringRef description;
    if (machine[0] != '\0' && ncpu > 0) {
        description = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("%s (%d cores)"), machine, ncpu);
    } else if (ncpu > 0) {
        description = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("%d cores"), ncpu);
    } else {
        description = CFStringCreateWithCString(kCFAllocatorDefault, machine, kCFStringEncodingUTF8);
    }

    if (description != NULL && maxLength != 0 && (uint32_t)CFStringGetLength(description) > maxLength) {
        /* The argument is not observable from the call site; honour it as a cap rather than ignore
         * it. A CPU description is far shorter than the client's 0x10000, so this never bites. */
        CFStringRef capped = CFStringCreateWithSubstring(kCFAllocatorDefault, description, CFRangeMake(0, (CFIndex)maxLength));
        CFRelease(description);
        description = capped;
    }

    return (void*)description;
}
