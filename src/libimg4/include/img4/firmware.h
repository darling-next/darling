/* Copyright (C) 2026 Darling contributors. SPDX-License-Identifier: GPL-3.0-or-later
 * Implemented non-executable Image4 sentinel interface. Selection authenticates
 * the manifest chain/role, not runtime identity, nonce, payload or mount policy.
 */
#ifndef DARLING_IMG4_FIRMWARE_H
#define DARLING_IMG4_FIRMWARE_H

#include <img4/runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _img4_firmware *img4_firmware_t;
typedef uint64_t img4_firmware_flags_t;

#define IMG4_FIRMWARE_FLAG_INIT 0ull
#define IMG4_FIRMWARE_FLAG_ATTACHED_MANIFEST (1ull << 0)
#define IMG4_FIRMWARE_FLAG_BARE (1ull << 1)
#define IMG4_FIRMWARE_FLAG_SUBSEQUENT_STAGE (1ull << 2)
#define IMG4_FIRMWARE_FLAG_RESPECT_AMNM (1ull << 3)
#define IMG4_FIRMWARE_STRUCT_RECOMMENDED_SIZE 1536u

/* Storage belongs to the caller and must outlive the firmware object. */
img4_firmware_t img4_firmware_init_from_buff(void *storage, size_t length);
void img4_firmware_init_sentinel(img4_firmware_t firmware,
    const img4_runtime_t *runtime, img4_firmware_flags_t flags);
/* Transfers the deallocator, clearing it in manifest but preserving its view.
 * A replacement releases the previous owned attachment immediately.
 */
void img4_firmware_attach_manifest(img4_firmware_t firmware,
                                  img4_buff_t *manifest);
const img4_chip_t *img4_firmware_select_chip(img4_firmware_t firmware,
    const img4_chip_t **chips, size_t chips_count);
void img4_firmware_destroy(img4_firmware_t *firmware);

#ifdef __cplusplus
}
#endif
#endif
