/*
 * Copyright (C) 2026 Darling contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Private Image4 manifest policy and strict DER views.
 *
 * The four-character-code property and A0/A1 required/prohibited constraint
 * semantics are derived from OpenCorePkg's OcAppleImg4Lib/libDERImg4
 * (DER_Img4Manifest.c, Copyright (C) 2019 Download-Fritz, BSD-3-Clause).
 * This is a rewrite over this tree's APSL-2.0 libDER declarations, not a copy
 * of OpenCore's bundled Apple libDER.  The reference does not examine the
 * extension critical flag and does not reject unknown critical extensions;
 * neither omission is reproduced here.
 *
 * The checker below is internal production code used by
 * _img4_manifest_authenticate().  It is not a test-only entry point and it
 * never grows a "skip policy" or alternate-root switch.
 */
#ifndef DARLING_IMG4_MANIFEST_POLICY_H
#define DARLING_IMG4_MANIFEST_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libDER/asn1Types.h>
#include <libDER/libDER.h>

#define IMG4_DER_TAG_4CC(a, b, c, d) \
    ((uint32_t)(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                ((uint32_t)(c) << 8) | (uint32_t)(d)))
#define IMG4_DER_PROPERTY_TAG(name) \
    ((DERTag)(name) | ASN1_CONSTRUCTED | ASN1_PRIVATE)

/*
 * A bounded view of one complete DER TLV inside a caller-owned buffer.
 * "start" points at the identifier octet; "content" points just after the
 * length octets.  Nothing is copied or allocated.
 */
typedef struct img4_der_item {
    const uint8_t *start;
    size_t total_length;
    const uint8_t *content;
    size_t content_length;
    DERTag tag;
} img4_der_item_t;

/*
 * Read one shallow libDER-backed DER TLV view at *offset. Canonical
 * lengths and scalar forms are checked here, without visiting children.
 * depth bounds callers doing recursive traversal. On success *offset is
 * advanced past the whole TLV; _img4_der_validate performs the one bounded
 * recursive pass, including canonical SET ordering and exact consumption.
 */
__attribute__((visibility("hidden")))
int _img4_der_read(const uint8_t *buffer, size_t length, size_t *offset,
                   img4_der_item_t *item, unsigned depth);

/* Strictly validate exactly one DER TLV consuming the whole buffer. */
__attribute__((visibility("hidden")))
int _img4_der_validate(const void *buffer, size_t length);

/* Compare complete encodings (identifier + length + content). */
__attribute__((visibility("hidden")))
bool _img4_der_item_equal(const img4_der_item_t *a, const img4_der_item_t *b);

/*
 * Image4 certificate role extension checker.
 *
 * role is the complete extnValue SET TLV of certificate extension
 * 1.2.840.113635.100.6.1.15.  manifest_body is the complete signed body SET
 * TLV from the IM4M.  Both are complete encoded SET TLVs, not sequence
 * contents.  Every object property set in the body (not just one payload) is
 * checked against the OBJP constraints.
 *
 * A non-NULL digest selects object_type's DGST during the same complete MANB
 * validation. MANP is never an asset. On success the OCTET STRING view borrows
 * the body; no digest profile is assumed. NULL performs role validation only.
 *
 * Returns 0 on success, EFTYPE for malformed/ambiguous encoding or an invalid
 * selected DGST, EAUTH for unsupported role semantics, EACCES for a constraint
 * mismatch, and ENOENT when a requested object is absent.
 */
__attribute__((visibility("hidden")))
int _img4_manifest_check_role_asset(const void *role, size_t role_length,
                                    const void *manifest_body,
                                    size_t manifest_body_length,
                                    uint32_t object_type,
                                    img4_der_item_t *digest);

#endif /* DARLING_IMG4_MANIFEST_POLICY_H */
