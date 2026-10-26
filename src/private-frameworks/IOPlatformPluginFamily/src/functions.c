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

#include <stdio.h>
#include <stdlib.h>
#include <CoreFoundation/CFString.h>
#include <IOKit/IOKitLib.h>
static int verbose = 0;
__attribute__((constructor)) static void initme(void) {
    verbose = getenv("STUB_VERBOSE") != NULL;
}
void* IOPPFGetDiagProperty(void) { if (verbose) puts("STUB: IOPPFGetDiagProperty called"); return NULL; }
void* IOPPFGetDiagPropertyFromDict(void) { if (verbose) puts("STUB: IOPPFGetDiagPropertyFromDict called"); return NULL; }
void* IOPPFGetHandler(void) { if (verbose) puts("STUB: IOPPFGetHandler called"); return NULL; }
CFTypeRef IOPPFGetProperty(CFStringRef key) {
    if (!key) return NULL;

    /*
     * Native IOPlatformPluginFamily (x86_64 UUID 75739A55-083B-3998-AEBC-9D79ADEAC825)
     * selects the service class "IOPlatformPluginFamily", then reads the requested
     * property with IORegistryEntryCreateCFProperty and returns that owned value.
     * The selector and return ownership are the measured native ABI.
     *
     * This tree cannot perform that lookup yet: iokitd's single-service matching
     * RPCs (is_io_service_get_matching_service, _bin and _ool) return
     * KERN_NOT_SUPPORTED, and IOServiceGetMatchingService converts the failure to
     * MACH_PORT_NULL.  A property request therefore reports absence (NULL), not a
     * positive platform value.  The VibeDarling iokitd donor has a newer
     * firstMatchingService implementation, but it is not ported here and no
     * platform service is fabricated to hide the gap.
     */
    io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                       IOServiceMatching("IOPlatformPluginFamily"));
    if (MACH_PORT_NULL == service) {
        if (verbose) puts("IOPPFGetProperty: no IOPlatformPluginFamily service (matching unsupported)");
        return NULL;
    }

    CFTypeRef value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    IOObjectRelease(service);

    if (verbose && !value) puts("IOPPFGetProperty: property absent");
    return value;
}
void* IOPPFPrintTest(void) { if (verbose) puts("STUB: IOPPFPrintTest called"); return NULL; }
void* IOPPFSetFanThrottleMode(void) { if (verbose) puts("STUB: IOPPFSetFanThrottleMode called"); return NULL; }
void* IOPPFSetProperties(void) { if (verbose) puts("STUB: IOPPFSetProperties called"); return NULL; }
