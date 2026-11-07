/*
 * Copyright (C) 2026 Darling contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pinned-anchor Image4 manifest authentication.
 *
 * The root-to-leaf chain construction and role requirement follow
 * OpenCorePkg's OcAppleImg4Lib/libDERImg4 (DER_Img4Manifest.c, Copyright
 * (C) 2019 Download-Fritz, BSD-3-Clause).  This is a rewrite over Darling's
 * APSL-2.0 libDER and its real Security SecKey RSA verifier.  OpenCore's
 * bundled Apple libDER is not copied.  Unlike the BSD reference, this path
 * rejects duplicate extensions/sets/properties, checks inner and outer
 * signature algorithms, applies standard X.509 CA/key-usage constraints, and
 * rejects unsupported critical extensions instead of ignoring them.
 *
 * The Darling-specific asset entry point additionally binds all supplied
 * bytes to the signed object's DGST. Neither path evaluates chip environment,
 * nonce/replay or execution authorization.
 */
#include <errno.h>
#include <string.h>
#include <pthread.h>

#include <limits.h>
#include <CommonCrypto/CommonDigest.h>
#include <img4/darling.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/SecItem.h>
#include <Security/SecKey.h>

#include <libDER/DER_CertCrl.h>
#include <libDER/DER_Decode.h>
#include <libDER/DER_Keys.h>
#include <libDER/asn1Types.h>
#include <libDER/libDER.h>
#include <libDER/oids.h>

#include "manifest.h"
#include "trust_anchor.h"

/* Bounded reuse audit for the asset binding replacement: this tree's vendored
 * libDER/Security provide DER and RSA, not a userspace firmware evaluator.
 * Apple XNU f6217f891ac0bb64f3d375211650a4c1ff8ca1ea declares evaluation but
 * supplies no userspace provider; darlinghq 07c2a8 and VibeDarling b456e root
 * trees likewise lack one. OpenCore 8d8d690 supplies BSD parsing/digest ideas,
 * but its UEFI environment and digest overrides are not reusable here.
 * Retain Darling's APSL libDER, pinned anchor, Security and role policy.
 */

#define IMG4_MAX_CERT_CHAIN 5u
#define IMG4_MAX_EXTENSIONS 32u

/* 1.2.840.113635.100.6.1.15, DER OBJECT IDENTIFIER content octets. */
static const uint8_t img4_manifest_role_oid[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x63, 0x64, 0x06, 0x01, 0x0f,
};

typedef struct img4_cert {
    DERItem full;
    DERItem tbs;
    DERItem issuer;
    DERItem subject;
    DERItem pubkey;             /* PKCS#1 RSAPublicKey, from SPKI BIT STRING */
    DERItem sig;                /* certificate signature BIT STRING bytes */
    DERItem outer_alg;          /* signatureAlgorithm OID */
    DERItem tbs_alg;            /* TBSCertificate.signature OID */

    bool has_role;
    DERItem role;               /* complete role extension SET TLV */

    bool has_ski;
    DERItem ski;
    bool has_aki;
    DERItem aki;
    bool aki_has_other;

    bool has_basic_constraints;
    bool is_ca;
    bool has_path_len;
    uint32_t path_len;

    bool has_key_usage;
    bool ku_digital_signature;
    bool ku_cert_sign;
} img4_cert_t;

typedef struct img4_manifest {
    img4_der_item_t body;          /* complete signed body SET TLV */
    img4_der_item_t signature;     /* OCTET STRING TLV */
    img4_der_item_t certificates;  /* SEQUENCE TLV */
} img4_manifest_t;

static DERItem
img4_item_der(const img4_der_item_t *item)
{
    DERItem result = {
        .data = (DERByte *)item->start,
        .length = item->total_length,
    };
    return result;
}

static DERItem
img4_content_der(const img4_der_item_t *item)
{
    DERItem result = {
        .data = (DERByte *)item->content,
        .length = item->content_length,
    };
    return result;
}

static bool
img4_deritem_equal(const DERItem *a, const DERItem *b)
{
    return a->length == b->length &&
           (a->length == 0 || memcmp(a->data, b->data, a->length) == 0);
}

static bool
img4_oid_is(const DERItem *oid, const uint8_t *bytes, size_t length)
{
    return oid->length == length &&
           memcmp(oid->data, bytes, length) == 0;
}

static bool
img4_null_params(const DERItem *params)
{
    /* AlgorithmIdentifier parameters we can honour are absent or NULL.
     * Anything else (including RSA-PSS parameters) is unsupported semantics.
     */
    return params->length == 0 ||
           (params->length == 2 && params->data[0] == ASN1_NULL &&
            params->data[1] == 0);
}

static int
img4_parse_algorithm(const DERItem *content, DERItem *oid)
{
    DERAlgorithmId alg;
    DERReturn result = DERParseSequenceContent(
        content, DERNumAlgorithmIdItemSpecs, DERAlgorithmIdItemSpecs,
        &alg, sizeof(alg));
    if (result != DR_Success) {
        return EFTYPE;
    }
    if (!img4_null_params(&alg.params)) {
        return EAUTH;
    }
    *oid = alg.oid;
    return 0;
}

static bool
img4_algorithm_supported(const DERItem *oid)
{
    return DEROidCompare(oid, &oidSha384Rsa) ||
           DEROidCompare(oid, &oidSha512Rsa);
}

/* Immutable process-lifetime provider state; input keys and verdicts are not
 * cached.  pthread_once publishes the anchor and attributes together.
 */
static pthread_once_t img4_anchor_once = PTHREAD_ONCE_INIT;
static img4_cert_t img4_anchor;
static CFDictionaryRef img4_key_attributes;
static SecKeyRef img4_anchor_key;
static int img4_anchor_error;

static int
img4_import_key(const DERItem *pubkey, SecKeyRef *key)
{
    *key = NULL;
    if (pubkey->data == NULL || pubkey->length == 0) {
        return EFTYPE;
    }
    CFDataRef data = CFDataCreateWithBytesNoCopy(
        kCFAllocatorDefault, pubkey->data, (CFIndex)pubkey->length,
        kCFAllocatorNull);
    if (data == NULL) {
        return ENOMEM;
    }
    CFErrorRef error = NULL;
    *key = SecKeyCreateWithData(data, img4_key_attributes, &error);
    CFRelease(data);
    if (error != NULL) {
        CFRelease(error);
    }
    return *key == NULL ? EFTYPE : 0;
}

/* The message algorithms perform the digest in the real Security provider. */
static int
img4_rsa_verify(SecKeyRef key, const DERItem *signature,
                const DERItem *data, const DERItem *algorithm)
{
    if (key == NULL || signature->data == NULL || signature->length == 0 ||
        data->data == NULL || data->length == 0) {
        return EFTYPE;
    }
    SecKeyAlgorithm sec_algorithm;
    if (DEROidCompare(algorithm, &oidSha384Rsa)) {
        sec_algorithm = kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA384;
    } else if (DEROidCompare(algorithm, &oidSha512Rsa)) {
        sec_algorithm = kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA512;
    } else {
        return EAUTH;
    }
    CFDataRef signed_data = CFDataCreateWithBytesNoCopy(
        kCFAllocatorDefault, data->data, (CFIndex)data->length,
        kCFAllocatorNull);
    CFDataRef signature_data = CFDataCreateWithBytesNoCopy(
        kCFAllocatorDefault, signature->data, (CFIndex)signature->length,
        kCFAllocatorNull);
    int result = ENOMEM;
    if (signed_data != NULL && signature_data != NULL) {
        CFErrorRef error = NULL;
        result = SecKeyVerifySignature(key, sec_algorithm, signed_data,
                                      signature_data, &error) ? 0 : EAUTH;
        if (error != NULL) {
            CFRelease(error);
        }
    }
    if (signed_data != NULL) {
        CFRelease(signed_data);
    }
    if (signature_data != NULL) {
        CFRelease(signature_data);
    }
    return result;
}

static bool
img4_key_usage_bit(const DERItem *bits, DERByte unused, unsigned bit)
{
    if (bits->length == 0 || unused > 7) {
        return false;
    }
    size_t total_bits = bits->length * 8u - unused;
    if ((size_t)bit >= total_bits) {
        return false;
    }
    const uint8_t mask = (uint8_t)(0x80u >> (bit % 8u));
    return (bits->data[bit / 8u] & mask) != 0;
}

static int
img4_parse_basic_constraints(const DERItem *value, img4_cert_t *cert)
{
    DERBasicConstraints constraints;
    DERReturn result = DERParseSequence(
        value, DERNumBasicConstraintsItemSpecs,
        DERBasicConstraintsItemSpecs, &constraints, sizeof(constraints));
    if (result != DR_Success) {
        return EFTYPE;
    }

    bool is_ca = false;
    if (constraints.cA.length != 0) {
        if (DERParseBoolean(&constraints.cA, &is_ca) != DR_Success) {
            return EFTYPE;
        }
    }

    bool has_path_len = false;
    uint32_t path_len = 0;
    if (constraints.pathLenConstraint.length != 0) {
        if (DERParseInteger(&constraints.pathLenConstraint, &path_len) !=
            DR_Success) {
            return EFTYPE;
        }
        has_path_len = true;
    }
    if (has_path_len && !is_ca) {
        return EAUTH;
    }

    cert->has_basic_constraints = true;
    cert->is_ca = is_ca;
    cert->has_path_len = has_path_len;
    cert->path_len = path_len;
    return 0;
}

static int
img4_parse_key_usage(const DERItem *value, img4_cert_t *cert)
{
    size_t offset = 0;
    img4_der_item_t bit_string;
    int error = _img4_der_read(value->data, value->length, &offset,
                               &bit_string, 0);
    if (error != 0) {
        return error;
    }
    if (offset != value->length || bit_string.tag != ASN1_BIT_STRING) {
        return EFTYPE;
    }

    DERItem contents = img4_content_der(&bit_string);
    DERItem bits;
    DERByte unused = 0;
    if (DERParseBitString(&contents, &bits, &unused) != DR_Success) {
        return EFTYPE;
    }
    /* RFC 5280 keyUsage has only bits 0..8 and must not be empty.
     * encipherOnly/decipherOnly are meaningful only with keyAgreement.
     */
    if (bits.length == 0 || bits.length > 2 ||
        (bits.length == 2 && (bits.data[1] & 0x7f) != 0) ||
        (bits.data[0] == 0 && (bits.length == 1 || bits.data[1] == 0)) ||
        ((img4_key_usage_bit(&bits, unused, 7) ||
          img4_key_usage_bit(&bits, unused, 8)) &&
         !img4_key_usage_bit(&bits, unused, 4))) {
        return EFTYPE;
    }

    cert->has_key_usage = true;
    cert->ku_digital_signature = img4_key_usage_bit(&bits, unused, 0);
    cert->ku_cert_sign = img4_key_usage_bit(&bits, unused, 5);
    return 0;
}

static int
img4_parse_subject_key_identifier(const DERItem *value, img4_cert_t *cert)
{
    size_t offset = 0;
    img4_der_item_t identifier;
    int error = _img4_der_read(value->data, value->length, &offset,
                               &identifier, 0);
    if (error != 0) {
        return error;
    }
    if (offset != value->length ||
        identifier.tag != ASN1_OCTET_STRING ||
        identifier.content_length == 0) {
        return EFTYPE;
    }

    cert->has_ski = true;
    cert->ski = img4_content_der(&identifier);
    return 0;
}

static int
img4_parse_authority_key_identifier(const DERItem *value, bool critical,
                                    img4_cert_t *cert)
{
    DERAuthorityKeyIdentifier identifier;
    DERReturn result = DERParseSequence(
        value, DERNumAuthorityKeyIdentifierItemSpecs,
        DERAuthorityKeyIdentifierItemSpecs, &identifier,
        sizeof(identifier));
    if (result != DR_Success) {
        return EFTYPE;
    }
    const bool has_issuer = identifier.authorityCertIssuer.data != NULL;
    const bool has_serial = identifier.authorityCertSerialNumber.data != NULL;
    if (has_issuer != has_serial ||
        (has_issuer && (identifier.authorityCertIssuer.length == 0 ||
                        identifier.authorityCertSerialNumber.length == 0))) {
        return EFTYPE;
    }

    if (identifier.keyIdentifier.data != NULL) {
        if (identifier.keyIdentifier.length == 0) {
            return EFTYPE;
        }
        cert->has_aki = true;
        cert->aki = identifier.keyIdentifier;
    }

    if (identifier.authorityCertIssuer.length != 0 ||
        identifier.authorityCertSerialNumber.length != 0) {
        cert->aki_has_other = true;
        if (critical) {
            /* We associate by issuer name, key identifier and signature.  A
             * critical AKI that additionally requires issuer/serial matching
             * has semantics this verifier does not claim to implement.
             */
            return EAUTH;
        }
    }

    return 0;
}

static int
img4_parse_extensions(const DERTBSCert *tbs, bool is_leaf,
                      img4_cert_t *cert)
{
    if (tbs->extensions.length == 0) {
        return 0;
    }

    DERTag tag = 0;
    DERSequence sequence;
    DERReturn result = DERDecodeSeqInit(&tbs->extensions, &tag, &sequence);
    if (result != DR_Success) {
        return EFTYPE;
    }
    if (tag != ASN1_CONSTR_SEQUENCE) {
        return EFTYPE;
    }

    DERItem seen_oids[IMG4_MAX_EXTENSIONS];
    size_t seen_count = 0;

    for (;;) {
        DERDecodedInfo info;
        result = DERDecodeSeqNext(&sequence, &info);
        if (result == DR_EndOfSequence) {
            break;
        }
        if (result != DR_Success || info.tag != ASN1_CONSTR_SEQUENCE) {
            return EFTYPE;
        }

        DERExtension extension;
        result = DERParseSequenceContent(
            &info.content, DERNumExtensionItemSpecs,
            DERExtensionItemSpecs, &extension, sizeof(extension));
        if (result != DR_Success) {
            return EFTYPE;
        }

        bool critical = false;
        if (extension.critical.length != 0) {
            if (DERParseBoolean(&extension.critical, &critical) !=
                DR_Success) {
                return EFTYPE;
            }
        }

        /* Every extension value is DER of the extension syntax.  Validate it
         * even when the extension is unknown or non-critical: a malformed
         * value must not be silently ignored.
         */
        int error = _img4_der_validate(
            extension.extnValue.data, extension.extnValue.length);
        if (error != 0) {
            return error;
        }

        if (seen_count == IMG4_MAX_EXTENSIONS) {
            return EFTYPE;
        }
        for (size_t i = 0; i < seen_count; i++) {
            if (DEROidCompare(&seen_oids[i], &extension.extnID)) {
                /* Duplicate extension OIDs are ambiguous. */
                return EFTYPE;
            }
        }
        seen_oids[seen_count++] = extension.extnID;

        if (img4_oid_is(&extension.extnID, img4_manifest_role_oid,
                        sizeof(img4_manifest_role_oid))) {
            /* The Image4 MANP/OBJP role belongs to the leaf.  Do not require
             * the critical flag (the fixture's TRUE is not universal
             * evidence), but always interpret and enforce a present role.
             */
            if (!is_leaf) {
                return EAUTH;
            }
            size_t offset = 0;
            img4_der_item_t role;
            error = _img4_der_read(
                extension.extnValue.data, extension.extnValue.length,
                &offset, &role, 0);
            if (error != 0) {
                return error;
            }
            if (offset != extension.extnValue.length ||
                role.tag != ASN1_CONSTR_SET) {
                return EFTYPE;
            }
            if (cert->has_role) {
                return EFTYPE;
            }
            cert->has_role = true;
            cert->role = extension.extnValue;
        } else if (DEROidCompare(&extension.extnID, &oidBasicConstraints)) {
            error = img4_parse_basic_constraints(&extension.extnValue, cert);
            if (error != 0) {
                return error;
            }
        } else if (DEROidCompare(&extension.extnID, &oidKeyUsage)) {
            error = img4_parse_key_usage(&extension.extnValue, cert);
            if (error != 0) {
                return error;
            }
        } else if (DEROidCompare(&extension.extnID,
                                 &oidSubjectKeyIdentifier)) {
            error = img4_parse_subject_key_identifier(
                &extension.extnValue, cert);
            if (error != 0) {
                return error;
            }
        } else if (DEROidCompare(&extension.extnID,
                                 &oidAuthorityKeyIdentifier)) {
            error = img4_parse_authority_key_identifier(
                &extension.extnValue, critical, cert);
            if (error != 0) {
                return error;
            }
        } else if (critical) {
            /* This includes every other Apple 1.2.840.113635.100.6.* OID.
             * The generic SecCertificate Apple-arc whitelist is deliberately
             * not used: it would accept critical semantics we do not
             * implement.
             */
            return EAUTH;
        }
    }

    return 0;
}

static int
img4_parse_cert(const img4_der_item_t *item, bool is_leaf, img4_cert_t *cert)
{
    memset(cert, 0, sizeof(*cert));

    int error = _img4_der_validate(item->start, item->total_length);
    if (error != 0) {
        return error;
    }
    if (item->tag != ASN1_CONSTR_SEQUENCE) {
        return EFTYPE;
    }

    cert->full = img4_item_der(item);

    DERSignedCertCrl signed_cert;
    DERReturn result = DERParseSequence(
        &cert->full, DERNumSignedCertCrlItemSpecs,
        DERSignedCertCrlItemSpecs, &signed_cert, sizeof(signed_cert));
    if (result != DR_Success) {
        return EFTYPE;
    }

    DERTBSCert tbs;
    result = DERParseSequence(
        &signed_cert.tbs, DERNumTBSCertItemSpecs, DERTBSCertItemSpecs,
        &tbs, sizeof(tbs));
    if (result != DR_Success) {
        return EFTYPE;
    }
    uint32_t version = 0;
    if (tbs.version.length != 0) {
        size_t version_offset = 0;
        img4_der_item_t version_item;
        error = _img4_der_read(tbs.version.data, tbs.version.length,
                               &version_offset, &version_item, 0);
        if (error != 0 || version_offset != tbs.version.length ||
            version_item.tag != ASN1_INTEGER) {
            return EFTYPE;
        }
        DERItem contents = img4_content_der(&version_item);
        if (DERParseInteger(&contents, &version) != DR_Success ||
            version == 0 || version > 2) {
            return EFTYPE;
        }
    }
    if ((tbs.extensions.length != 0 && version != 2) ||
        ((tbs.issuerID.length != 0 || tbs.subjectID.length != 0) &&
         version == 0)) {
        return EFTYPE;
    }

    error = img4_parse_algorithm(&signed_cert.sigAlg, &cert->outer_alg);
    if (error != 0) {
        return error;
    }
    error = img4_parse_algorithm(&tbs.tbsSigAlg, &cert->tbs_alg);
    if (error != 0) {
        return error;
    }
    if (!DEROidCompare(&cert->outer_alg, &cert->tbs_alg) ||
        !img4_deritem_equal(&signed_cert.sigAlg, &tbs.tbsSigAlg)) {
        /* Inner and outer signature algorithms disagree. */
        return EAUTH;
    }
    if (!img4_algorithm_supported(&cert->outer_alg)) {
        return EAUTH;
    }

    cert->tbs = signed_cert.tbs;
    cert->issuer = tbs.issuer;
    cert->subject = tbs.subject;

    DERSubjPubKeyInfo spki;
    result = DERParseSequence(
        &tbs.subjectPubKey, DERNumSubjPubKeyInfoItemSpecs,
        DERSubjPubKeyInfoItemSpecs, &spki, sizeof(spki));
    if (result != DR_Success) {
        return EFTYPE;
    }

    DERAlgorithmId spki_algorithm;
    result = DERParseSequenceContent(
        &spki.algId, DERNumAlgorithmIdItemSpecs,
        DERAlgorithmIdItemSpecs, &spki_algorithm, sizeof(spki_algorithm));
    if (result != DR_Success) {
        return EFTYPE;
    }
    if (!DEROidCompare(&spki_algorithm.oid, &oidRsa) ||
        !img4_null_params(&spki_algorithm.params)) {
        return EAUTH;
    }

    DERItem public_key;
    DERByte unused = 0;
    result = DERParseBitString(&spki.pubKey, &public_key, &unused);
    if (result != DR_Success || unused != 0 || public_key.length == 0) {
        return EFTYPE;
    }
    /* The SPKI BIT STRING is itself DER (PKCS#1 RSAPublicKey).  Validate its
     * canonical form and exact consumption before handing it to the provider.
     */
    error = _img4_der_validate(public_key.data, public_key.length);
    if (error != 0) {
        return error;
    }
    cert->pubkey = public_key;
    DERRSAPubKeyPKCS1 rsa;
    if (DERParseSequence(&public_key, DERNumRSAPubKeyPKCS1ItemSpecs,
                         DERRSAPubKeyPKCS1ItemSpecs, &rsa, sizeof(rsa)) !=
        DR_Success || rsa.modulus.length == 0 ||
        (rsa.modulus.data[0] & 0x80) != 0) {
        return EFTYPE;
    }
    if (is_leaf &&
        (rsa.modulus.length != 513 || rsa.modulus.data[0] != 0 ||
         (rsa.modulus.data[1] & 0x80) == 0)) {
        return EAUTH;
    }

    DERItem signature;
    result = DERParseBitString(&signed_cert.sig, &signature, &unused);
    if (result != DR_Success || unused != 0 || signature.length == 0) {
        return EFTYPE;
    }
    cert->sig = signature;

    error = img4_parse_extensions(&tbs, is_leaf, cert);
    if (error != 0) {
        return error;
    }

    if (is_leaf) {
        if (cert->has_basic_constraints && cert->is_ca) {
            return EAUTH;
        }
        if (cert->has_path_len) {
            return EAUTH;
        }
        if (cert->has_key_usage &&
            (!cert->ku_digital_signature || cert->ku_cert_sign)) {
            return EAUTH;
        }
    } else {
        /* Every issuer in this firmware chain is a CA and must be able to
         * certify.  If key usage is present it must say so; if a CA omits
         * basicConstraints entirely it is not a valid issuer.
         */
        if (!cert->has_basic_constraints || !cert->is_ca) {
            return EAUTH;
        }
        if (cert->has_key_usage && !cert->ku_cert_sign) {
            return EAUTH;
        }
    }

    return 0;
}

static void
img4_initialize_anchor(void)
{
    const void *keys[] = { kSecAttrKeyType, kSecAttrKeyClass };
    const void *values[] = { kSecAttrKeyTypeRSA, kSecAttrKeyClassPublic };
    img4_key_attributes = CFDictionaryCreate(
        kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (img4_key_attributes == NULL) {
        img4_anchor_error = ENOMEM;
        return;
    }
    size_t offset = 0;
    img4_der_item_t item;
    img4_anchor_error = _img4_der_read(
        img4_trust_anchor_root_der, img4_trust_anchor_root_der_size,
        &offset, &item, 0);
    if (img4_anchor_error == 0 &&
        offset != img4_trust_anchor_root_der_size) {
        img4_anchor_error = EFTYPE;
    }
    if (img4_anchor_error == 0) {
        img4_anchor_error = img4_parse_cert(&item, false, &img4_anchor);
    }
    if (img4_anchor_error == 0) {
        img4_anchor_error = img4_import_key(&img4_anchor.pubkey,
                                             &img4_anchor_key);
    }
    if (img4_anchor_error != 0) {
        CFRelease(img4_key_attributes);
        img4_key_attributes = NULL;
    }
}

static int
img4_parse_manifest(const void *manifest, size_t length,
                    img4_manifest_t *parsed)
{
    if (manifest == NULL || length == 0) {
        return EINVAL;
    }

    int error = _img4_der_validate(manifest, length);
    if (error != 0) {
        return error;
    }

    const uint8_t *bytes = manifest;
    size_t offset = 0;
    img4_der_item_t outer;
    error = _img4_der_read(bytes, length, &offset, &outer, 0);
    if (error != 0) {
        return error;
    }
    if (offset != length || outer.tag != ASN1_CONSTR_SEQUENCE) {
        return EFTYPE;
    }

    img4_der_item_t magic;
    img4_der_item_t version;
    img4_der_item_t body;
    img4_der_item_t signature;
    img4_der_item_t certificates;

    offset = 0;
    error = _img4_der_read(outer.content, outer.content_length, &offset,
                           &magic, 0);
    if (error != 0) {
        return error;
    }
    error = _img4_der_read(outer.content, outer.content_length, &offset,
                           &version, 0);
    if (error != 0) {
        return error;
    }
    error = _img4_der_read(outer.content, outer.content_length, &offset,
                           &body, 0);
    if (error != 0) {
        return error;
    }
    error = _img4_der_read(outer.content, outer.content_length, &offset,
                           &signature, 0);
    if (error != 0) {
        return error;
    }
    error = _img4_der_read(outer.content, outer.content_length, &offset,
                           &certificates, 0);
    if (error != 0) {
        return error;
    }
    if (offset != outer.content_length) {
        /* Trailing data inside the outer sequence. */
        return EFTYPE;
    }

    if (magic.tag != ASN1_IA5_STRING || magic.content_length != 4 ||
        memcmp(magic.content, "IM4M", 4) != 0) {
        return EFTYPE;
    }
    if (version.tag != ASN1_INTEGER) {
        return EFTYPE;
    }
    DERItem version_item = img4_content_der(&version);
    uint32_t version_value = 0;
    if (DERParseInteger(&version_item, &version_value) != DR_Success ||
        version_value != 0) {
        return EFTYPE;
    }
    if (body.tag != ASN1_CONSTR_SET) {
        return EFTYPE;
    }
    if (signature.tag != ASN1_OCTET_STRING ||
        signature.content_length == 0) {
        return EFTYPE;
    }
    if (certificates.tag != ASN1_CONSTR_SEQUENCE) {
        return EFTYPE;
    }

    parsed->body = body;
    parsed->signature = signature;
    parsed->certificates = certificates;
    return 0;
}

static int
img4_authenticate_chain(const img4_manifest_t *manifest,
                        const img4_der_item_t *cert_items, size_t cert_count,
                        img4_cert_t *leaf)
{
    int error = pthread_once(&img4_anchor_once, img4_initialize_anchor);
    if (error != 0) {
        return error;
    }
    if (img4_anchor_error != 0) {
        return img4_anchor_error;
    }
    img4_cert_t parent = img4_anchor;
    SecKeyRef parent_key = img4_anchor_key; /* borrowed only for the anchor */
    bool owns_key = false;
    uint32_t path_limits[IMG4_MAX_CERT_CHAIN + 1];
    size_t limit_count = 0;
    if (parent.has_path_len) {
        path_limits[limit_count++] = parent.path_len;
    }

    for (size_t i = 0; i < cert_count; i++) {
        const bool is_leaf = i + 1 == cert_count;
        img4_cert_t child;
        error = img4_parse_cert(&cert_items[i], is_leaf, &child);
        if (error != 0) {
            goto out;
        }
        if (!img4_deritem_equal(&child.issuer, &parent.subject) ||
            (child.has_aki && parent.has_ski &&
             !img4_deritem_equal(&child.aki, &parent.ski))) {
            error = EAUTH;
            goto out;
        }
        error = img4_rsa_verify(parent_key, &child.sig, &child.tbs,
                                &child.outer_alg);
        if (error != 0) {
            goto out;
        }
        if (!is_leaf) {
            /* RFC 5280 pathLen counts non-self-issued intermediate CAs,
             * not the target leaf or self-issued rollover certificates.
             */
            if (!img4_deritem_equal(&child.issuer, &child.subject)) {
                for (size_t j = 0; j < limit_count; j++) {
                    if (path_limits[j] == 0) {
                        error = EAUTH;
                        goto out;
                    }
                    path_limits[j]--;
                }
            }
            if (child.has_path_len) {
                path_limits[limit_count++] = child.path_len;
            }
        } else if (!child.has_role) {
            error = EAUTH;
            goto out;
        }
        SecKeyRef child_key = NULL;
        error = img4_import_key(&child.pubkey, &child_key);
        if (error != 0) {
            goto out;
        }
        if (owns_key) {
            CFRelease(parent_key);
        }
        parent_key = child_key;
        owns_key = true;
        parent = child;
    }

    /* Generic-x86 DDI-global profile: RSA4096/SHA384.  The algorithm
     * signing the leaf certificate belongs to its issuer, not this body.
     */
    if (SecKeyGetBlockSize(parent_key) != 512) {
        error = EAUTH;
        goto out;
    }
    DERItem signature = img4_content_der(&manifest->signature);
    DERItem body = img4_item_der(&manifest->body);
    error = img4_rsa_verify(parent_key, &signature, &body, &oidSha384Rsa);
    if (error == 0) {
        *leaf = parent;
    }
out:
    if (owns_key) {
        CFRelease(parent_key);
    }
    return error;
}

static int
img4_authenticate(const void *manifest, size_t length, uint32_t object_type,
                   img4_der_item_t *digest)
{
    img4_manifest_t parsed;
    int error = img4_parse_manifest(manifest, length, &parsed);
    if (error != 0) {
        return error;
    }

    img4_der_item_t cert_items[IMG4_MAX_CERT_CHAIN];
    size_t cert_count = 0;
    size_t offset = 0;

    while (offset < parsed.certificates.content_length) {
        img4_der_item_t item;
        error = _img4_der_read(parsed.certificates.content,
                               parsed.certificates.content_length, &offset,
                               &item, 0);
        if (error != 0) {
            return error;
        }
        if (item.tag != ASN1_CONSTR_SEQUENCE) {
            return EFTYPE;
        }
        if (cert_count == IMG4_MAX_CERT_CHAIN) {
            return EFTYPE;
        }
        cert_items[cert_count++] = item;
    }
    if (cert_count == 0) {
        return EFTYPE;
    }

    img4_cert_t leaf;
    error = img4_authenticate_chain(&parsed, cert_items, cert_count, &leaf);
    if (error != 0) {
        return error;
    }

    /* The role was extracted from the authenticated leaf certificate.  It is
     * interpreted against the complete signed body only after the body
     * signature above has been verified.
     */
    error = _img4_manifest_check_role_asset(
        leaf.role.data, leaf.role.length,
        parsed.body.start, parsed.body.total_length, object_type, digest);
    return error;
}

int
_img4_manifest_authenticate(const void *manifest, size_t length)
{
    return img4_authenticate(manifest, length, 0, NULL);
}

int
darling_img4_verify_asset(const void *manifest, size_t manifest_length,
                          uint32_t object_type,
                          const void *asset, size_t asset_length)
{
    if (manifest == NULL || manifest_length == 0 ||
        (asset == NULL && asset_length != 0)) {
        return EINVAL;
    }
    img4_der_item_t digest;
    int error = img4_authenticate(manifest, manifest_length, object_type,
                                  &digest);
    if (error != 0) return error;
    /* Only the evidenced whole-file SHA384 profile is supported. */
    if (digest.content_length != CC_SHA384_DIGEST_LENGTH) return ENOTSUP;

    CC_SHA512_CTX context;
    unsigned char actual[CC_SHA384_DIGEST_LENGTH];
    CC_SHA384_Init(&context);
    const unsigned char *bytes = asset;
    while (asset_length != 0) {
        CC_LONG chunk = asset_length > UINT_MAX ? UINT_MAX :
                        (CC_LONG)asset_length;
        CC_SHA384_Update(&context, bytes, chunk);
        bytes += chunk;
        asset_length -= chunk;
    }
    CC_SHA384_Final(actual, &context);
    return memcmp(actual, digest.content, sizeof(actual)) == 0 ? 0 : EAUTH;
}

/*
 * Bounded gtgv reuse audit: vendored libcryptex has no payload decoder;
 * current apfs-fuse only initializes sealed-volume fexts, and libfsapfs's
 * format documentation supplies no gtgv/IM4P schema. Apple's public APFS
 * repository endpoint returned 404, not proof that source does not exist.
 * Native APFS2811.120.14 (UUID b1326ec3-44d2-34b0-843d-8c54ff18d538),
 * _validate_payload_and_manifest / _authenticate_root_hash, is the behavioral
 * reference, not reusable source. It authenticates firmware before comparing
 * this 208-byte descriptor to independent integrity metadata. Its selector
 * uses +16/+80/+144 for 4K/8K/16K; we support only 4K, without its unknown-size
 * fallback. Native equality checks version/type/length/selected digest, NOT
 * flags: flags zero below is our supported-profile restriction.
 */
static uint32_t
img4_seal_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

int
darling_img4_verify_apfs_seal(const void *manifest, size_t manifest_length,
                             const void *asset, size_t asset_length,
                             uint32_t block_size,
                             darling_img4_apfs_seal_t *seal)
{
    if (seal == NULL) return EINVAL;
    int error = darling_img4_verify_asset(manifest, manifest_length,
        UINT32_C(0x67746776), asset, asset_length);
    if (error != 0) return error;

    /* Parse only the same immutable whole asset whose digest was verified. */
    error = _img4_der_validate(asset, asset_length);
    if (error != 0) return error;
    size_t offset = 0;
    img4_der_item_t sequence;
    error = _img4_der_read(asset, asset_length, &offset, &sequence, 0);
    if (error != 0) return error;
    if (offset != asset_length || sequence.tag != ASN1_CONSTR_SEQUENCE)
        return EFTYPE;
    img4_der_item_t fields[4];
    offset = 0;
    for (size_t i = 0; i < 4; i++) {
        error = _img4_der_read(sequence.content, sequence.content_length,
                               &offset, &fields[i], 0);
        if (error != 0) return error;
    }
    if (offset != sequence.content_length ||
        fields[0].tag != ASN1_IA5_STRING || fields[0].content_length != 4 ||
        memcmp(fields[0].content, "IM4P", 4) != 0 ||
        fields[1].tag != ASN1_IA5_STRING || fields[1].content_length != 4 ||
        memcmp(fields[1].content, "gtgv", 4) != 0 ||
        fields[2].tag != ASN1_IA5_STRING ||
        fields[3].tag != ASN1_OCTET_STRING || fields[3].content_length != 208)
        return EFTYPE;
    const uint8_t *payload = fields[3].content;
    uint32_t version = img4_seal_le32(payload);
    if (block_size != 4096 || (version != 1 && version != 2) ||
        img4_seal_le32(payload + 4) != 0 ||
        img4_seal_le32(payload + 8) != 1 ||
        img4_seal_le32(payload + 12) != 32)
        return ENOTSUP;

    darling_img4_apfs_seal_t result = { .integrity_version = version };
    memcpy(result.root_hash, payload + 16, sizeof(result.root_hash));
    *seal = result;
    return 0;
}
