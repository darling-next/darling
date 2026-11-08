/* Copyright (C) 2026 Darling contributors. SPDX-License-Identifier: GPL-3.0-or-later
 * Public v1 runtime primitives, not a fabricated Apple runtime identity.
 */
#include <img4/runtime.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "chip_internal.h"
#include "runtime_internal.h"

#if __LP64__
_Static_assert(sizeof(img4_buff_t) == 32, "Image4 buffer ABI");
_Static_assert(offsetof(img4_buff_t, i4b_dealloc) == 24, "Image4 deallocator ABI");
_Static_assert(sizeof(img4_runtime_t) == 96, "Image4 runtime v1 ABI");
#endif

static void initialize_registered_runtimes(void)
{
    const void *header = _dyld_get_image_header(0);
    const char *segments[] = { "__DATA_CONST", "__DATA" };
    /* XNU's linker-set macro uses DATA_CONST on LP64, DATA on ILP32.
     * Accept both layouts for main executables built with older SDK headers.
     */
    for (size_t s = 0; s < sizeof(segments) / sizeof(segments[0]); ++s) {
        unsigned long bytes = 0;
        const uint8_t *entries =
            getsectiondata(header, segments[s], "__img4_rt", &bytes);
        for (size_t i = 0; entries && i < bytes / sizeof(const img4_runtime_t *); ++i) {
            const img4_runtime_t *runtime;
            /* Native SDKs wrap the same pointer in a linker-set entry struct.
             * Read its representation without assuming that C object type. */
            memcpy(&runtime, entries + i * sizeof(runtime), sizeof(runtime));
            if (runtime && runtime->i4rt_init)
                runtime->i4rt_init(runtime);
        }
    }
}

void _img4_runtime_initialize(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, initialize_registered_runtimes);
}

static void *runtime_alloc(const img4_runtime_t *runtime, size_t length)
{
    (void)runtime;
    return malloc(length);
}

static void runtime_dealloc(const img4_runtime_t *runtime, void *bytes, size_t length)
{
    (void)runtime;
    (void)length;
    free(bytes);
}

static void *runtime_log_handle(const img4_runtime_t *runtime)
{
    (void)runtime;
    return stderr;
}

static void runtime_log(const img4_runtime_t *runtime, void *handle,
                        img4_log_level_t level, const char *format, ...)
{
    (void)runtime;
    (void)level;
    va_list arguments;
    va_start(arguments, format);
    vfprintf(handle ? handle : stderr, format, arguments);
    va_end(arguments);
}

static int query_identity(CFStringRef key, size_t width, uint64_t *value)
{
    int error = _img4_query_oracle_value(key, width, value);
    /* A missing node, malformed property or failed I/O is an oracle error,
     * distinct from an individually absent identifier.
     */
    return error && error != ENOENT ? ENODEV : error;
}

static int runtime_get_bool(const img4_runtime_t *runtime,
    const img4_chip_t *chip, img4_identifier_t identifier, bool *value)
{
    (void)runtime;
    if (chip != IMG4_CHIP_CRYPTEX1_GENERIC_X86)
        return ENOTSUP;
    CFStringRef key;
    switch (identifier) {
    case IMG4_IDENTIFIER_CPRO: key = IMG4_ORACLE_KEY("ApProductionStatus"); break;
    case IMG4_IDENTIFIER_CSEC: key = IMG4_ORACLE_KEY("ApSecurityMode"); break;
    case IMG4_IDENTIFIER_EPRO: key = IMG4_ORACLE_KEY("EffectiveProductionStatus"); break;
    case IMG4_IDENTIFIER_ESEC: key = IMG4_ORACLE_KEY("EffectiveSecurityMode"); break;
    case IMG4_IDENTIFIER_IUOU: key = IMG4_ORACLE_KEY("InternalUseOnlyUnit"); break;
    default: return ENOTSUP;
    }
    uint64_t raw;
    int error = query_identity(key, sizeof(uint32_t), &raw);
    if (!error)
        *value = raw != 0;
    return error;
}

static int runtime_get_uint32(const img4_runtime_t *runtime,
    const img4_chip_t *chip, img4_identifier_t identifier, uint32_t *value)
{
    (void)runtime;
    if (chip != IMG4_CHIP_CRYPTEX1_GENERIC_X86)
        return ENOTSUP;
    if (identifier == IMG4_IDENTIFIER_CEPO) {
        *value = IMG4_GENERIC_X86_CERTIFICATE_EPOCH;
        return 0;
    }
    /* Reuse: current apple-oss-distributions/xnu
     * EXTERNAL_HEADERS/img4/runtime.h explicitly requires ENOENT for these
     * profile-owned identifiers. Sibling vibedarling has only XNU declarations,
     * not a reusable userspace provider. Do not query an ambient registry key
     * or report the selected profile's constants as independently supplied
     * runtime identity. ENOTSUP would incorrectly conflate this specified
     * absence with an identifier this provider cannot implement.
     */
    if (identifier == IMG4_IDENTIFIER_FCHP ||
        identifier == IMG4_IDENTIFIER_TYPE ||
        identifier == IMG4_IDENTIFIER_STYP)
        return ENOENT;
    CFStringRef key;
    switch (identifier) {
    case IMG4_IDENTIFIER_BORD: key = IMG4_ORACLE_KEY("ApBoardID"); break;
    case IMG4_IDENTIFIER_CHIP: key = IMG4_ORACLE_KEY("ApChipID"); break;
    case IMG4_IDENTIFIER_SDOM: key = IMG4_ORACLE_KEY("ApSecurityDomain"); break;
    default: return ENOTSUP;
    }
    uint64_t raw;
    int error = query_identity(key, sizeof(uint32_t), &raw);
    if (!error)
        *value = (uint32_t)raw;
    return error;
}

static int runtime_get_uint64(const img4_runtime_t *runtime,
    const img4_chip_t *chip, img4_identifier_t identifier, uint64_t *value)
{
    (void)runtime;
    if (chip != IMG4_CHIP_CRYPTEX1_GENERIC_X86 || identifier != IMG4_IDENTIFIER_ECID)
        return ENOTSUP;
    return query_identity(IMG4_ORACLE_KEY("ApECID"), sizeof(*value), value);
}

static int runtime_get_digest(const img4_runtime_t *runtime,
    const img4_chip_t *chip, img4_identifier_t identifier, img4_dgst_t *value)
{
    (void)runtime;
    (void)chip;
    (void)identifier;
    (void)value;
    /* CHMH requires an actual previous secure-boot stage. This userspace
     * runtime has no such producer; a manifest's own hash is not a substitute.
     */
    return ENOTSUP;
}

const img4_runtime_t _img4_runtime_default = {
    .i4rt_version = IMG4_RUNTIME_STRUCT_VERSION,
    .i4rt_name = "Darling userspace",
    .i4rt_alloc = runtime_alloc,
    .i4rt_dealloc = runtime_dealloc,
    .i4rt_log = runtime_log,
    .i4rt_log_handle = runtime_log_handle,
    .i4rt_get_identifier_bool = runtime_get_bool,
    .i4rt_get_identifier_uint32 = runtime_get_uint32,
    .i4rt_get_identifier_uint64 = runtime_get_uint64,
    .i4rt_get_identifier_digest = runtime_get_digest,
};

void img4_buff_dealloc(img4_buff_t *buffer)
{
    if (!buffer || !buffer->i4b_dealloc)
        return;
    _img4_runtime_initialize();
    img4_runtime_dealloc_t dealloc = buffer->i4b_dealloc;
    buffer->i4b_dealloc = NULL;
    dealloc(IMG4_RUNTIME_DEFAULT, buffer->i4b_bytes, buffer->i4b_len);
}
