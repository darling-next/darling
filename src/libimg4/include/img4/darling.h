/*
 * Copyright (C) 2026 Darling contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef DARLING_IMG4_DARLING_H
#define DARLING_IMG4_DARLING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Authenticate an IM4M against the pinned Apple Extra Content Global Root
 * CA - G1 and bind exactly object_type's signed DGST to ALL asset_length
 * supplied bytes, including any stored wrapper. object_type is a big-endian
 * four-character code (e.g. 0x67696e66 denotes "ginf"), not a host byte array.
 * Only the evidenced 48-byte SHA384 digest profile is supported.
 *
 * Both buffers are borrowed and must remain immutable for the entire call.
 * The caller must ensure subsequent consumption uses exactly the verified
 * bytes, not a reopened/replaced file or a mutable alias. No ownership is
 * transferred and no verdict is cached. manifest must be non-NULL/nonempty;
 * asset may be NULL if and only if asset_length is zero.
 *
 * Zero establishes pinned chain, signed body and certificate-role
 * authentication plus exact object-to-byte integrity binding ONLY. It does
 * NOT authorize environment, nonce/replay, root-slot selection, mounting or
 * execution, and does not decode IM4P. MANP cannot be selected as an asset.
 * This is a Darling API, not Apple's private firmware_evaluate contract.
 *
 * Returns Darwin errno values directly (does not set errno):
 * EINVAL: invalid buffer arguments.
 * ENOENT: requested MANB object absent (including a MANP request).
 * EFTYPE: malformed/ambiguous DER, objects/properties, missing or wrong-typed
 *         DGST, or malformed certificate/role encoding.
 * ENOTSUP: DGST is not the supported 48-byte SHA384 profile.
 * EAUTH: asset mismatch, untrusted chain/signature or unsupported chain/role
 *        semantics.
 * EACCES: authenticated body violates certificate-role constraints.
 * ENOMEM: verification-provider allocation failure.
 * Existing provider/chain initialization errors are propagated unchanged.
 */
int darling_img4_verify_asset(const void *manifest, size_t manifest_length,
                              uint32_t object_type,
                              const void *asset, size_t asset_length);

typedef struct {
    uint32_t integrity_version;
    uint8_t root_hash[32];
} darling_img4_apfs_seal_t;

/*
 * Authenticate the entire borrowed asset using verify_asset's pinned chain,
 * body and role policy for the fixed signed object "gtgv", then decode its
 * bare IM4P APFS seal descriptor. Both buffers have the same immutability and
 * subsequent-consumption requirements as verify_asset above.
 *
 * Supported profile: 4096-byte blocks, integrity version 1 or 2, flags zero,
 * hash type 1 (SHA256), digest length 32. Only the 4K slot is selected; other
 * slots are neither searched nor required to be zero. The backend must bind
 * BOTH returned fields to independently parsed volume integrity metadata.
 *
 * Zero proves authenticated content and a supported root descriptor only,
 * NOT environment, replay, peer, mount or execution authorization.
 * seal must be non-NULL and is left untouched on every error.
 * Returns verify_asset's errno family, plus EFTYPE for malformed IM4P,
 * wrong type or payload length, and ENOTSUP for unsupported valid profiles
 * (including block size). Invalid arguments return EINVAL.
 */
int darling_img4_verify_apfs_seal(const void *manifest, size_t manifest_length,
                                 const void *asset, size_t asset_length,
                                 uint32_t block_size,
                                 darling_img4_apfs_seal_t *seal);

#ifdef __cplusplus
}
#endif
#endif /* DARLING_IMG4_DARLING_H */
