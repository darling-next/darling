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
 * libMobileGestalt's MODERN DEVICE SPI: the eight names Xcode 26.3's XCTHarness binds EAGERLY
 * from the libMobileGestalt field (/usr/lib/libMobileGestalt.dylib, its dependency ordinal 17).
 *
 * WHY THIS FILE EXISTS.  The rest of /usr/lib/libMobileGestalt.dylib in this tree is the classic
 * answer farm (MGCopyAnswer and friends, src/MobileGestalt.m) and it carries none of these names.
 * The loader therefore refuses XCTHarness -- the image that demanded the previous rung's
 * _macho_arch_name_for_mach_header from libSystem -- before any of its code runs.
 *
 * WHAT THE DEMANDS ARE, measured from XCTHarness's own x86_64 slice
 * (llvm-objdump --macho --arch=x86_64 --chained-fixups, "dyld chained import" table):
 *
 *   entry 779  lib_ordinal 17  weak_import 0  _MobileGestalt_copy_humanReadableProductVersionString_obj
 *   entry 780  lib_ordinal 17  weak_import 0  _MobileGestalt_copy_productType_obj
 *   entry 781  lib_ordinal 17  weak_import 1  _MobileGestalt_copy_runnableArchitectures_obj   (NOT a stop)
 *   entry 782  lib_ordinal 17  weak_import 0  _MobileGestalt_copy_supplementalBuildVersion_obj
 *   entry 783  lib_ordinal 17  weak_import 0  _MobileGestalt_copy_uniqueDeviceID_obj
 *   entry 784  lib_ordinal 17  weak_import 0  _MobileGestalt_copy_userAssignedDeviceName_obj
 *   entry 785  lib_ordinal 17  weak_import 0  _MobileGestalt_get_cpuSubType
 *   entry 786  lib_ordinal 17  weak_import 0  _MobileGestalt_get_cpuType
 *   entry 787  lib_ordinal 17  weak_import 0  _MobileGestalt_get_current_device
 *
 * Eight of the nine are strong (eager) demands: those are implemented here.  The ninth,
 * _MobileGestalt_copy_runnableArchitectures_obj, is weak_import 1 -- dyld binds it to NULL instead
 * of stopping the load -- so it is NOT in the unmet set and is deliberately not implemented here
 * (its ONE client nils-tests it: `testq %rbx,%rbx; je` into its own empty-array path, measured at
 * 0x15445c of XCTHarness's disassembly).
 *
 * THE OWNERSHIP CONTRACT, measured from the client's own code rather than guessed from the names.
 * For every one of the nine the client's sequence is
 *
 *     callq <name>
 *     movq  %rax, %rdi
 *     callq _objc_retainAutoreleasedReturnValue      <- a +0 (autoreleased) return
 *     ...                                            <- for the string ones:
 *     callq String._unconditionallyBridgeFromObjectiveC(NSString?)
 *     ...
 *     callq *_objc_release                           <- balances the retain above
 *
 * (XCTHarness 0x154197, 0x154bd5, 0x154d93, 0x15799a, 0x157a11, 0x157a96.)  The compiler emits
 * _objc_retainAutoreleasedReturnValue only for a +0 return, so despite the `copy` in the name these
 * entry points answer with a borrowed-then-autoreleased object and the client's retain/release pair
 * balances it.  Answering +1 would leak one object per call; answering a non-object would crash
 * _objc_retainAutoreleasedReturnValue, which reads the isa.
 *
 * THE DEVICE HANDLE IS NOT OPTIONAL, and this is the one place the client is NOT defensive:
 * _MobileGestalt_get_current_device must answer an object.  Its result is force-unwrapped -- every
 * NULL branch in the client is a `ud2` trap, not a fallback:
 *
 *   XCTHLocalDevice init (name)          0x15798b testq/je 0x157c98 ud2
 *   XCTHLocalDevice init (identifier)    0x157a02 testq/je 0x157c9a ud2
 *   XCTHLocalDevice init (os version)    0x157a87 testq/je 0x157c9c ud2
 *   operatingSystemVersionWithBuildNumber 0x154188 testq/je 0x154242 ud2
 *   runnableArchitectures (weak field)   0x154438 testq/je 0x1544a3 ud2
 *   nativeArchitecture (cpuType)         0x154600 testq/je 0x154699 ud2
 *   nativeArchitecture (cpuSubType)      0x15462c testq/je 0x15469b ud2
 *   modelCode (productType)              0x154d8b testq/je 0x154de0 ud2
 *   modelUTType (productType)            0x154bcd testq/je 0x154c41 ud2   (device)
 *                                        0x154bf1 testq/je 0x154c43 ud2   (THE VALUE, below)
 *
 * So this file's device handle is a real, memory-managed object of a class defined here, and it is
 * the SAME object every call (a stateless handle: the answers below are read live, not stored on
 * it).  An aborting stub -- the shape this wave's libtrace rung found in _os_log_get_type -- would
 * have been invisible to the loader graph and would have killed the guest on first use; a NULL
 * handle would have done the same thing through the client's force-unwrap.
 *
 * WHAT THE ANSWERS READ, and why each is a fact rather than a plausible value:
 *
 *   cpuType / cpuSubType   the architecture this slice executes on (the process arch, which for a
 *                          load-command closure is the device's arch).  The guest's own
 *                          `sysctl hw.cputype` is REFUSED even though the sysctl exists: its value
 *                          comes from darlingserver's host_info, which reports
 *                          CPU_TYPE_X86/CPU_SUBTYPE_X86_64_ALL
 *                          (src/external/darlingserver/duct-tape/src/host.c:62-66), and the client
 *                          feeds the pair to macho_arch_name_for_cpu_type(), whose table
 *                          (src/external/dyld/src/dyldAPIsInLibSystem.cpp:3235) matches cputype
 *                          EXACTLY -- so hw.cputype's collapsed CPU_TYPE_X86 would answer "i386"
 *                          for a 64-bit device.  That is a plausible wrong value, so it is not used.
 *                          The guest's own hw.machine ("x86_64", the same fact in string form, and
 *                          the only CPU key the tree measures as answered -- see
 *                          src/private-frameworks/AppleSystemInfo/src/CurrentCPUDescription.c)
 *                          agrees with the compile-time pair.
 *
 *   the version strings    the guest's own configuration: /System/Library/CoreServices/
 *                          SystemVersion.plist (deployed: ProductVersion 14.0, ProductBuildVersion
 *                          23A344), read through CoreFoundation's own system-version entry point --
 *                          the same one this tree's sw_vers uses (src/tools/sw_vers.c:41-63).
 *                          humanReadableProductVersionString is the marketing version and
 *                          supplementalBuildVersion the build: the client's own composition fixes
 *                          that reading (operatingSystemVersionWithBuildNumber builds
 *                          "<version> (<build>)" at 0x1541e1-0x154216), and ProductBuildVersion is
 *                          this tree's canonical build-version fact (_kCFSystemVersionBuildVersionKey).
 *
 *   uniqueDeviceID         the machine's own stable unique id, via gethostuuid() -- the entry point
 *                          the tree already answers from the host's /etc/machine-id
 *                          (src/external/xnu/darling/.../bsd/impl/misc/gethostuuid.c) and already
 *                          consumes elsewhere for the host UUID
 *                          (src/external/corefoundation/.../Preferences.subproj/CFPreferences.c).
 *                          Formatted with CFUUIDCreateString, the same way that consumer formats it.
 *
 *   productType            the machine's own model.  There is NO Apple model here: the tree defines
 *                          no device model, the guest does not emulate hw.model (no HW_MODEL entry
 *                          in the guest's own sysctl table,
 *                          src/external/xnu/darling/.../helper/misc/sysctl_hw.c:38-54), and the
 *                          prefix carries no model in its configuration.  Absence is NOT an option
 *                          for this one, though: the client force-unwraps it (the modelUTType `ud2`
 *                          above, reached from XCTHLocalDevice's own initializer through
 *                          modelUTI -> modelUTType), so a NULL answer is a SIGILL on first use.
 *                          So it answers the machine's own model as the machine reports it -- the
 *                          host's DMI product name, read through the host-root path the guest
 *                          already has (/Volumes/SystemRoot, the mount the workload itself reads its
 *                          toolchain through), falling back to the path inside the guest's own root
 *                          if a sysfs is bound there.  On this host that is "Virtual Machine"
 *                          (/sys/class/dmi/id/product_name).  That is a real fact about the machine
 *                          this emulated device IS, not an invented Apple model code; and nothing
 *                          downstream parses its shape -- UTType.init(_deviceModelCode:enclosureColor:)
 *                          answers nil on this platform by design
 *                          (src/external/swift/overlays/UniformTypeIdentifiers/UniformTypeIdentifiers.swift:393).
 *                          If the machine reports no product name at all, the answer is NULL, and
 *                          the trap above is the measured consequence -- named here rather than
 *                          hidden.
 *
 *   userAssignedDeviceName NULL, and that is the honest answer: the name is the user's, and this
 *                          tree's own SystemConfiguration keeps it in
 *                          /Library/Preferences/SystemConfiguration/preferences.plist
 *                          System/ComputerName (src/external/configd/SystemConfiguration.fproj/
 *                          SCDHostName.c:99-140), which the prefix does not have.  The client's
 *                          NULL path is a real fallback -- the literal "Local device"
 *                          (0x1579d1: movabsq 'Local de' / 'vice') -- measured, not a hang.
 *
 * Everything here is per-call and stateless; the handle exists because the ABI hands one back.
 */

#import <Foundation/Foundation.h>
#include <MobileGestalt/MobileGestalt.h>

#include <CoreFoundation/CFPriv.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/machine.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ the device handle */

/* An opaque, stateless handle.  It is a real Objective-C object because the client retains and
 * releases it (and _objc_retainAutoreleasedReturnValue reads its isa), and a singleton because
 * nothing about it is per-instance. */
@interface MGDarlingDevice : NSObject
@end

@implementation MGDarlingDevice
@end

static MGDarlingDevice* gCurrentDevice = nil;
static pthread_once_t gCurrentDeviceOnce = PTHREAD_ONCE_INIT;

static void MGCreateCurrentDevice(void)
{
    gCurrentDevice = [[MGDarlingDevice alloc] init];
}

/* +0 for the caller, like Apple's Get-shaped SPI: the handle is owned by this library for the
 * lifetime of the process, so the client's retain/release pair (its own ownership) is what is
 * balanced -- and neither call can free the object another client is holding. */
void* MobileGestalt_get_current_device(void)
{
    pthread_once(&gCurrentDeviceOnce, MGCreateCurrentDevice);
    return gCurrentDevice;
}

/* ------------------------------------------------------------------ helpers */

/* A whitespace-trimmed UTF-8 line from a file, or nil.  Used only for facts the machine publishes
 * as files; a missing file, an empty file and invalid UTF-8 all answer nil, which every caller
 * below treats as "this machine does not report it". */
static NSString* MGStringFromFile(const char* path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return nil;
    }

    char buffer[256];
    ssize_t length = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (length <= 0) {
        return nil;
    }

    while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r'
                          || buffer[length - 1] == ' ' || buffer[length - 1] == '\t')) {
        --length;
    }
    if (length == 0) {
        return nil;
    }

    NSString* string = [[NSString alloc] initWithBytes:buffer
                                                length:(NSUInteger)length
                                              encoding:NSUTF8StringEncoding];
    return [string autorelease];
}

/* One value out of the deployed SystemVersion.plist, through CoreFoundation's own reader (+0 for
 * the caller: retained here, autoreleased on the way out, because the dictionary is released). */
static NSString* MGSystemVersionValue(CFStringRef key)
{
    CFDictionaryRef version = _CFCopySystemVersionDictionary();
    if (version == NULL) {
        return nil;
    }

    CFStringRef value = CFDictionaryGetValue(version, key);
    if (value != NULL) {
        CFRetain(value);
    }
    CFRelease(version);

    return [(id)value autorelease];
}

/* ------------------------------------------------------------------ the answers */

/* The architecture this slice is executing as -- and that is a fact about this process, not a
 * default: a process can only load an image of its own architecture, so the pair below is the one
 * the client's macho_arch_name_for_cpu_type() must answer the device's own name with.  An
 * unhandled architecture is a build error rather than a wrong number (the same choice
 * darlingserver makes for the same unknown-arch case, duct-tape/src/host.c:68). */
static cpu_type_t MGDarlingCPUType(void)
{
#if defined(__x86_64__)
    return CPU_TYPE_X86_64;
#elif defined(__i386__)
    return CPU_TYPE_I386;
#elif defined(__arm64__)
    return CPU_TYPE_ARM64;
#elif defined(__arm__)
    return CPU_TYPE_ARM;
#else
#error MobileGestalt: unknown architecture for the device's cpu type
#endif
}

static cpu_subtype_t MGDarlingCPUSubType(void)
{
#if defined(__x86_64__)
    return CPU_SUBTYPE_X86_64_ALL;
#elif defined(__i386__)
    return CPU_SUBTYPE_I386_ALL;
#elif defined(__arm64__)
    return CPU_SUBTYPE_ARM64_ALL;
#elif defined(__arm__)
    return CPU_SUBTYPE_ARM_ALL;
#else
#error MobileGestalt: unknown architecture for the device's cpu subtype
#endif
}

int32_t MobileGestalt_get_cpuType(void* device)
{
    (void)device;
    return (int32_t)MGDarlingCPUType();
}

int32_t MobileGestalt_get_cpuSubType(void* device)
{
    (void)device;
    return (int32_t)MGDarlingCPUSubType();
}

CFStringRef MobileGestalt_copy_humanReadableProductVersionString_obj(void* device)
{
    (void)device;
    return (CFStringRef)MGSystemVersionValue(_kCFSystemVersionProductVersionKey);
}

CFStringRef MobileGestalt_copy_supplementalBuildVersion_obj(void* device)
{
    (void)device;
    return (CFStringRef)MGSystemVersionValue(_kCFSystemVersionBuildVersionKey);
}

CFStringRef MobileGestalt_copy_uniqueDeviceID_obj(void* device)
{
    (void)device;

    /* gethostuuid() answers the machine's own id (the tree reads it from the host's
     * /etc/machine-id); an infinite timeout is the convention this tree's other caller uses
     * (CFPreferences.c).  Formatted exactly as that caller formats it. */
    struct timespec timeout = { 0, 0 };
    unsigned char uuidBytes[16];
    if (gethostuuid(uuidBytes, &timeout) != 0) {
        return NULL;
    }

    CFUUIDRef uuid = CFUUIDCreateFromUUIDBytes(kCFAllocatorDefault, *(CFUUIDBytes*)uuidBytes);
    CFStringRef string = CFUUIDCreateString(kCFAllocatorDefault, uuid);
    CFRelease(uuid);

    return (CFStringRef)[(id)string autorelease];
}

CFStringRef MobileGestalt_copy_productType_obj(void* device)
{
    (void)device;

    /* The machine's own model, as the machine reports it.  Both spellings of the same file: the
     * host root this guest mounts (/Volumes/SystemRoot, the path the workload reads its toolchain
     * through) and the guest's own root, if a sysfs is bound there.  Nothing is invented if neither
     * answers -- see the productType note at the top of this file for what NULL then means. */
    static const char* const paths[] = {
        "/sys/class/dmi/id/product_name",
        "/Volumes/SystemRoot/sys/class/dmi/id/product_name",
    };

    for (size_t index = 0; index < sizeof(paths) / sizeof(paths[0]); ++index) {
        NSString* name = MGStringFromFile(paths[index]);
        if (name != nil) {
            return (CFStringRef)name;
        }
    }

    return NULL;
}

CFStringRef MobileGestalt_copy_userAssignedDeviceName_obj(void* device)
{
    (void)device;
    /* No user has assigned one: the client's own fallback is the measured answer (see this file's
     * header). */
    return NULL;
}
