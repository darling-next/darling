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

#ifdef __cplusplus
}
#endif
#endif /* DARLING_IMG4_DARLING_H */
