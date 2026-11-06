/*
 * Copyright (C) 2026 Darling contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Reuse audit: vendored XNU supplies declarations/kernel dispatch, not a
 * userspace implementation; darlinghq and VibeDarling have no libimg4 source.
 * ABI: apple-oss-distributions/xnu EXTERNAL_HEADERS/img4/chip{,_cryptex1}.h.
 * Generic-x86 policy and oracle mapping were checked against libimage4 349.100.9
 * x86 code/data: chip_instance_instantiate, chip_get_property_expert,
 * __darwin_cpl3_query_property_*, expert_compute_udid. No native image is linked
 * or loaded. Fixed values below describe the virtual family, NOT the host AP.
 *
 * There is intentionally no firmware authentication or personalization API.
 * An instance with omitted fields is not proof of any security property.
 */
#include <img4/chip.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <string.h>

struct _img4_chip {
    uint32_t identifier;
};

const img4_chip_t _img4_chip_cryptex1_generic_x86 = { 0x63316778u };

#if __LP64__
_Static_assert(sizeof(img4_chip_instance_t) == 144, "Image4 v6 ABI size");
_Static_assert(offsetof(img4_chip_instance_t, chid_udid) == 64, "Image4 digest ABI");
_Static_assert(offsetof(img4_chip_instance_t, chid_fchp) == 128, "Image4 v6 tail ABI");
#endif

/* Native identity properties are byte buffers, not CFNumber/CFBoolean values.
 * A shorter buffer is zero-extended, as in the native runtime's initialized
 * scalar destination. Neither absent properties nor invalid types become zero.
 */
static int read_identity(io_registry_entry_t oracle, CFStringRef key,
                         size_t width, uint64_t *value)
{
    CFTypeRef property = IORegistryEntryCreateCFProperty(oracle, key,
                                                       kCFAllocatorDefault, 0);
    if (!property)
        return ENOENT;
    int error = 0;
    if (CFGetTypeID(property) != CFDataGetTypeID()) {
        error = ENODATA;
    } else {
        CFIndex length = CFDataGetLength((CFDataRef)property);
        if (length < 0 || (size_t)length > width) {
            error = EOVERFLOW;
        } else {
            *value = 0;
            if (length)
                memcpy(value, CFDataGetBytePtr((CFDataRef)property), (size_t)length);
        }
    }
    CFRelease(property);
    return error;
}

#define ORACLE_KEY(name) CFSTR("94B73556-2197-4702-82A8-3E1337DAFBFB:" name)

int img4_chip_instantiate(const img4_chip_t *chip, img4_chip_instance_t *instance)
{
    if (!instance || chip != IMG4_CHIP_CRYPTEX1_GENERIC_X86 ||
        instance->chid_version != IMG4_CHIP_INSTANCE_STRUCT_VERSION)
        return EINVAL;

    /* This is the actual Image4 identity oracle. IOPlatformUUID, Linux machine
     * IDs and CPUID are not substitutes for an AP's certificate/fusing state.
     * Failure to access the oracle differs from an absent individual property.
     */
    io_registry_entry_t oracle = IORegistryEntryFromPath(kIOMasterPortDefault,
                                                        "IODeviceTree:/options");
    if (oracle == IO_OBJECT_NULL)
        return EXDEV;

    img4_chip_instance_t result = {0};
    result.chid_version = IMG4_CHIP_INSTANCE_STRUCT_VERSION;
    result.chid_chip_family = chip;
    result.chid_omit = (1u << 19) - 1;

    /* Generic-x86's static expert availability mask is 0x1c01: certificate
     * epoch, cryptex chip/type/subtype. The x86 runtime defines product class
     * 0xf0. None of these values asserts a physical machine identity.
     */
    result.chid_cepo = 1;
    result.chid_fchp = 0xff11;
    result.chid_type = 3;
    result.chid_styp = 0xff;
    result.chid_clas = 0xf0;
    result.chid_omit &= ~(uint64_t)(IMG4_CHIP_INSTANCE_OMIT_CEPO |
        IMG4_CHIP_INSTANCE_OMIT_FCHP | IMG4_CHIP_INSTANCE_OMIT_TYPE |
        IMG4_CHIP_INSTANCE_OMIT_STYP | IMG4_CHIP_INSTANCE_OMIT_CLAS);

    int error = 0;
    uint64_t value;
#define READ_FIELD(field, name, width, omit) do { \
    error = read_identity(oracle, ORACLE_KEY(name), width, &value); \
    if (!error) { \
        result.field = value; \
        result.chid_omit &= ~(uint64_t)(omit); \
    } else if (error != ENOENT && error != ENODEV && error != ENOTSUP) { \
        goto out; \
    } \
} while (0)
    READ_FIELD(chid_bord, "ApBoardID", 4, IMG4_CHIP_INSTANCE_OMIT_BORD);
    READ_FIELD(chid_chip, "ApChipID", 4, IMG4_CHIP_INSTANCE_OMIT_CHIP);
    READ_FIELD(chid_sdom, "ApSecurityDomain", 4, IMG4_CHIP_INSTANCE_OMIT_SDOM);
    READ_FIELD(chid_ecid, "ApECID", 8, IMG4_CHIP_INSTANCE_OMIT_ECID);
    READ_FIELD(chid_cpro, "ApProductionStatus", 4, IMG4_CHIP_INSTANCE_OMIT_CPRO);
    READ_FIELD(chid_csec, "ApSecurityMode", 4, IMG4_CHIP_INSTANCE_OMIT_CSEC);
    READ_FIELD(chid_epro, "EffectiveProductionStatus", 4, IMG4_CHIP_INSTANCE_OMIT_EPRO);
    READ_FIELD(chid_esec, "EffectiveSecurityMode", 4, IMG4_CHIP_INSTANCE_OMIT_ESEC);
    READ_FIELD(chid_iuou, "InternalUseOnlyUnit", 4, IMG4_CHIP_INSTANCE_OMIT_IUOU);
#undef READ_FIELD

    /* The native UDID is a 16-byte encoding, not a hash or a generated UUID:
     * zero32 || big-endian CHIP32 || big-endian ECID64. Missing either source
     * omits UDID. Research/engineering/extended-domain/factory-trust properties
     * have no producer in this x86 runtime and remain omitted.
     */
    if (!(result.chid_omit & (IMG4_CHIP_INSTANCE_OMIT_CHIP |
                             IMG4_CHIP_INSTANCE_OMIT_ECID))) {
        result.chid_udid.i4d_len = 16;
        for (unsigned i = 0; i < 4; ++i)
            result.chid_udid.i4d_bytes[4 + i] = result.chid_chip >> (24 - i * 8);
        for (unsigned i = 0; i < 8; ++i)
            result.chid_udid.i4d_bytes[8 + i] = result.chid_ecid >> (56 - i * 8);
        result.chid_omit &= ~(uint64_t)IMG4_CHIP_INSTANCE_OMIT_UDID;
    }
    *instance = result;
    error = 0;
out:
    IOObjectRelease(oracle);
    return error;
}
