/* Copyright (C) 2026 Darling contributors. SPDX-License-Identifier: GPL-3.0-or-later
 * Reuse audit: public XNU provides declarations/kernel dispatch, not a userspace
 * sentinel implementation. Newer public XNU and reviewed sibling forks have no
 * reusable provider. ABI/lifecycle were checked against libimage4 349.100.9;
 * no native executable code is linked or deployed.
 */
#include <img4/firmware.h>
#include <string.h>
#include "manifest.h"
#include "runtime_internal.h"

struct _img4_firmware {
    const img4_runtime_t *runtime;
    img4_firmware_flags_t flags;
    img4_buff_t manifest;
};

img4_firmware_t img4_firmware_init_from_buff(void *storage, size_t length)
{
    if (!storage || length < sizeof(struct _img4_firmware))
        return NULL;
    memset(storage, 0, sizeof(struct _img4_firmware));
    return storage;
}

void img4_firmware_init_sentinel(img4_firmware_t firmware,
    const img4_runtime_t *runtime, img4_firmware_flags_t flags)
{
    _img4_runtime_initialize();
    memset(firmware, 0, sizeof(*firmware));
    firmware->runtime = runtime;
    firmware->flags = flags;
}

void img4_firmware_attach_manifest(img4_firmware_t firmware, img4_buff_t *manifest)
{
    /* The native replacement operation loses the old deallocator. Release
     * superseded owned storage here instead of deliberately retaining a leak.
     */
    img4_buff_dealloc(&firmware->manifest);
    firmware->manifest = *manifest;
    manifest->i4b_dealloc = NULL;
}

const img4_chip_t *img4_firmware_select_chip(img4_firmware_t firmware,
    const img4_chip_t **chips, size_t chips_count)
{
    if (!firmware || !firmware->runtime || !chips ||
        !(firmware->flags & IMG4_FIRMWARE_FLAG_SUBSEQUENT_STAGE))
        return NULL;
    /* Only the generic-x86 Cryptex expert and its DDI-global trust anchor are
     * implemented. Pointer identity avoids interpreting foreign chip layouts.
     */
    for (size_t i = 0; i < chips_count; ++i) {
        if (chips[i] != IMG4_CHIP_CRYPTEX1_GENERIC_X86)
            continue;
        /* Reauthenticate the referenced bytes on each call; attachment did not
         * copy them and a prior success cannot authenticate a changed buffer.
         */
        return _img4_manifest_authenticate(firmware->manifest.i4b_bytes,
            firmware->manifest.i4b_len) == 0 ? chips[i] : NULL;
    }
    return NULL;
}

void img4_firmware_destroy(img4_firmware_t *firmware)
{
    if (!firmware || !*firmware)
        return;
    /* Keep the caller's firmware pointer live during the buffer callback,
     * matching native lifetime ordering. Object storage remains caller-owned.
     */
    img4_buff_dealloc(&(*firmware)->manifest);
    memset(*firmware, 0, sizeof(**firmware));
    *firmware = NULL;
}
