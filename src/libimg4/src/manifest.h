/*
 * Copyright (C) 2026 Darling contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Private Image4 manifest trust-chain authentication.
 *
 * The chain construction and certificate-role policy are derived from
 * OpenCorePkg's OcAppleImg4Lib/libDERImg4 (BSD-3-Clause, Copyright (C)
 * 2019 Download-Fritz), rewritten over this tree's libDER and Security
 * SecKey/SecCertificate-independent policy.  OpenCore's bundled Apple libDER
 * is not copied or relicensed.
 */
#ifndef DARLING_IMG4_MANIFEST_H
#define DARLING_IMG4_MANIFEST_H

#include <stddef.h>

#include "manifest_policy.h"

/*
 * Authenticate an installed-format IM4M buffer against the pinned Apple
 * "Extra Content Global Root CA - G1" anchor.
 *
 * Verification covers: strict DER and exact outer consumption, IM4M magic and
 * version, root-to-leaf issuer/subject and signature chain, inner/outer
 * signature-algorithm agreement, RSA4096/SHA384 profile signature over the
 * complete signed body, recognized critical X.509 extension semantics,
 * Image4 MANP/OBJP certificate role constraints, duplicate/ambiguity rejection,
 * and rejection of unsupported critical extensions.
 *
 * There is no caller-supplied root, no skip mode and no partial success.
 * This establishes certificate/manifest trust only; it does not evaluate a
 * firmware payload digest, chip environment, nonce, replay or execution
 * authorization.
 * Only immutable anchor metadata/provider objects are cached thread-safely;
 * caller-owned manifest bytes are authenticated anew on every invocation.
 *
 * Returns Darwin errno values:
 *   EINVAL  null or empty input
 *   EFTYPE  malformed DER/IM4M/certificate/role or ambiguous duplicates
 *   EAUTH   untrusted chain, bad signature, missing/unsupported critical
 *           semantics, or an unsupported signature algorithm
 *   EACCES  authenticated manifest body violates required/prohibited/exact
 *           Image4 role constraints
 *   ENOMEM  system allocation failure in the verification provider
 */
__attribute__((visibility("hidden")))
int _img4_manifest_authenticate(const void *manifest, size_t length);

#endif /* DARLING_IMG4_MANIFEST_H */
