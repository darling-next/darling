/*
 * Copyright (C) 2026 Darling contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Image4 MANP/OBJP certificate-role policy over bounded DER views.
 *
 * Semantics follow OpenCorePkg's OcAppleImg4Lib/libDERImg4
 * (DER_Img4Manifest.c, Copyright (C) 2019 Download-Fritz, BSD-3-Clause):
 * a BOOLEAN/INTEGER/OCTET STRING role property pins the entire encoded
 * property, [0] requires presence and [1] prohibits presence.  This rewrite
 * additionally enforces strict DER, rejects duplicate/ambiguous sets and
 * properties, verifies name-versus-private-tag agreement, and applies OBJP
 * constraints to every object property set in the signed body.
 */
#include <errno.h>
#include <string.h>

#include "manifest_policy.h"
#include <libDER/DER_Decode.h>

#define IMG4_MAX_DER_DEPTH 32u

#define IMG4_TAG_MANP IMG4_DER_PROPERTY_TAG(IMG4_DER_TAG_4CC('M', 'A', 'N', 'P'))
#define IMG4_TAG_OBJP IMG4_DER_PROPERTY_TAG(IMG4_DER_TAG_4CC('O', 'B', 'J', 'P'))
#define IMG4_TAG_MANB IMG4_DER_PROPERTY_TAG(IMG4_DER_TAG_4CC('M', 'A', 'N', 'B'))

typedef struct img4_property {
    img4_der_item_t item;       /* complete private-tag property TLV */
    DERTag tag;                 /* private constructed 4cc tag */
    uint32_t name;              /* 4cc value, also the IA5String name */
    img4_der_item_t value;      /* complete value TLV */
    DERTag value_tag;
} img4_property_t;

typedef struct img4_named_set {
    img4_der_item_t item;       /* complete private-tag set TLV */
    DERTag tag;
    uint32_t name;
    img4_der_item_t props;      /* complete SET TLV of properties */
} img4_named_set_t;

enum img4_value_kind {
    IMG4_VALUE_PINNED,
    IMG4_VALUE_REQUIRED,
    IMG4_VALUE_PROHIBITED,
    IMG4_VALUE_UNSUPPORTED,
};

static bool
img4_private_4cc_tag(DERTag tag)
{
    /* A property name is exactly a four-character code, not an arbitrary
     * private high-tag-number value.  The low 32 bits must be the whole tag
     * number, with only the private class and constructed method bits above.
     */
    DERTag high = tag & ~((DERTag)UINT32_MAX);
    return high == (ASN1_PRIVATE | ASN1_CONSTRUCTED);
}

static void
img4_4cc_bytes(uint32_t name, uint8_t out[4])
{
    out[0] = (uint8_t)(name >> 24);
    out[1] = (uint8_t)(name >> 16);
    out[2] = (uint8_t)(name >> 8);
    out[3] = (uint8_t)name;
}

static enum img4_value_kind
img4_value_kind(DERTag tag, bool role)
{
    if (role && tag == (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTED | 0)) {
        return IMG4_VALUE_REQUIRED;
    }
    if (role && tag == (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTED | 1)) {
        return IMG4_VALUE_PROHIBITED;
    }
    if (tag == ASN1_BOOLEAN || tag == ASN1_INTEGER ||
        tag == ASN1_OCTET_STRING) {
        return IMG4_VALUE_PINNED;
    }
    /* OpenCore's body property decoder also accepts IA5String, although
     * certificate roles may pin only BOOLEAN/INTEGER/OCTET STRING. */
    if (!role && tag == ASN1_IA5_STRING) {
        return IMG4_VALUE_PINNED;
    }
    /* A signed-body container (MANB) carries a SET value.  Certificate role
     * constraints remain primitive/[0]/[1] only.
     */
    if (!role && tag == ASN1_CONSTR_SET) {
        return IMG4_VALUE_PINNED;
    }
    return IMG4_VALUE_UNSUPPORTED;
}

static int
img4_read_one(const uint8_t *buffer, size_t length, size_t *offset,
              img4_der_item_t *item)
{
    return _img4_der_read(buffer, length, offset, item, 0);
}

/*
 * Decode one property:
 *   [private constructed 4cc] {
 *       SEQUENCE {
 *           IA5String name,   -- exactly the same 4cc
 *           <value TLV>
 *       }
 *   }
 */
static int
img4_parse_property(const img4_der_item_t *item, img4_property_t *prop,
                    bool role)
{
    if (!img4_private_4cc_tag(item->tag)) {
        return EFTYPE;
    }

    size_t offset = 0;
    img4_der_item_t sequence;
    int error = img4_read_one(item->content, item->content_length, &offset,
                              &sequence);
    if (error != 0) {
        return error;
    }
    if (offset != item->content_length ||
        sequence.tag != ASN1_CONSTR_SEQUENCE) {
        return EFTYPE;
    }

    offset = 0;
    img4_der_item_t name;
    img4_der_item_t value;
    error = img4_read_one(sequence.content, sequence.content_length, &offset,
                          &name);
    if (error != 0) {
        return error;
    }
    error = img4_read_one(sequence.content, sequence.content_length, &offset,
                          &value);
    if (error != 0) {
        return error;
    }
    if (offset != sequence.content_length) {
        return EFTYPE;
    }
    if (name.tag != ASN1_IA5_STRING || name.content_length != 4) {
        return EFTYPE;
    }

    uint32_t tag_name = (uint32_t)(item->tag & (DERTag)UINT32_MAX);
    uint8_t expected[4];
    img4_4cc_bytes(tag_name, expected);
    if (memcmp(name.content, expected, sizeof(expected)) != 0) {
        return EFTYPE;
    }

    enum img4_value_kind kind = img4_value_kind(value.tag, role);
    if (kind == IMG4_VALUE_UNSUPPORTED) {
        return EAUTH;
    }
    if ((kind == IMG4_VALUE_REQUIRED || kind == IMG4_VALUE_PROHIBITED) &&
        (value.content_length != 2 || value.content[0] != 0x05 ||
         value.content[1] != 0x00)) {
        return EFTYPE;
    }

    prop->item = *item;
    prop->tag = item->tag;
    prop->name = tag_name;
    prop->value = value;
    prop->value_tag = value.tag;
    return 0;
}

/*
 * Decode one named property set (role MANP/OBJP or a signed-body object set):
 *   [private constructed 4cc] {
 *       SEQUENCE {
 *           IA5String name,
 *           SET { <properties> }
 *       }
 *   }
 */
static int
img4_parse_named_set(const img4_der_item_t *item, img4_named_set_t *set)
{
    if (!img4_private_4cc_tag(item->tag)) {
        return EFTYPE;
    }

    size_t offset = 0;
    img4_der_item_t sequence;
    int error = img4_read_one(item->content, item->content_length, &offset,
                              &sequence);
    if (error != 0) {
        return error;
    }
    if (offset != item->content_length ||
        sequence.tag != ASN1_CONSTR_SEQUENCE) {
        return EFTYPE;
    }

    offset = 0;
    img4_der_item_t name;
    img4_der_item_t props;
    error = img4_read_one(sequence.content, sequence.content_length, &offset,
                          &name);
    if (error != 0) {
        return error;
    }
    error = img4_read_one(sequence.content, sequence.content_length, &offset,
                          &props);
    if (error != 0) {
        return error;
    }
    if (offset != sequence.content_length) {
        return EFTYPE;
    }
    if (name.tag != ASN1_IA5_STRING || name.content_length != 4 ||
        props.tag != ASN1_CONSTR_SET) {
        return EFTYPE;
    }

    uint32_t tag_name = (uint32_t)(item->tag & (DERTag)UINT32_MAX);
    uint8_t expected[4];
    img4_4cc_bytes(tag_name, expected);
    if (memcmp(name.content, expected, sizeof(expected)) != 0) {
        return EFTYPE;
    }

    set->item = *item;
    set->tag = item->tag;
    set->name = tag_name;
    set->props = props;
    return 0;
}

/* Canonically sorted private tags make equal names adjacent. */
static int
img4_next_property(const img4_der_item_t *set, size_t *offset,
                   DERTag *previous, img4_property_t *prop, bool role)
{
    img4_der_item_t item;
    int error = img4_read_one(set->content, set->content_length, offset, &item);
    if (error != 0) {
        return error;
    }
    error = img4_parse_property(&item, prop, role);
    if (error != 0) {
        return error;
    }
    if (*previous != 0 && prop->tag <= *previous) {
        return EFTYPE;
    }
    *previous = prop->tag;
    return 0;
}

/* Merge sorted constraints and values. Consume even unconstrained body
 * properties, so their structure and scope cannot escape validation. */
static int
img4_apply_constraints(const img4_der_item_t *role,
                       const img4_der_item_t *body,
                       img4_der_item_t *digest)
{
    size_t ro = 0, bo = 0;
    DERTag rp = 0, bp = 0;
    img4_property_t value;
    bool have_value = false;
    while (role != NULL && ro < role->content_length) {
        img4_property_t constraint;
        int error = img4_next_property(role, &ro, &rp, &constraint, true);
        if (error != 0) {
            return error;
        }
        while ((!have_value || value.tag < constraint.tag) &&
               body != NULL && bo < body->content_length) {
            error = img4_next_property(body, &bo, &bp, &value, false);
            if (error != 0) {
                return error;
            }
            if (value.value_tag == ASN1_CONSTR_SET) {
                return EFTYPE;
            }
            if (digest != NULL &&
                value.name == IMG4_DER_TAG_4CC('D', 'G', 'S', 'T')) {
                if (value.value_tag != ASN1_OCTET_STRING) return EFTYPE;
                *digest = value.value;
            }
            have_value = true;
        }
        bool present = have_value && value.tag == constraint.tag;
        switch (img4_value_kind(constraint.value_tag, true)) {
        case IMG4_VALUE_REQUIRED:
            if (!present) return EACCES;
            break;
        case IMG4_VALUE_PROHIBITED:
            if (present) return EACCES;
            break;
        case IMG4_VALUE_PINNED:
            if (!present ||
                !_img4_der_item_equal(&constraint.item, &value.item)) {
                return EACCES;
            }
            break;
        default:
            return EAUTH;
        }
    }
    while (body != NULL && bo < body->content_length) {
        int error = img4_next_property(body, &bo, &bp, &value, false);
        if (error != 0) return error;
        if (value.value_tag == ASN1_CONSTR_SET) return EFTYPE;
        if (digest != NULL &&
            value.name == IMG4_DER_TAG_4CC('D', 'G', 'S', 'T')) {
            if (value.value_tag != ASN1_OCTET_STRING) return EFTYPE;
            *digest = value.value;
        }
    }
    return 0;
}

int
_img4_manifest_check_role_asset(const void *role, size_t role_length,
                               const void *manifest_body,
                               size_t manifest_body_length,
                               uint32_t object_type,
                               img4_der_item_t *digest)
{
    if (digest != NULL) memset(digest, 0, sizeof(*digest));
    int error = _img4_der_validate(role, role_length);
    if (error != 0) return error;
    error = _img4_der_validate(manifest_body, manifest_body_length);
    if (error != 0) return error;
    size_t offset = 0;
    img4_der_item_t roles, body;
    error = img4_read_one(role, role_length, &offset, &roles);
    if (error != 0) return error;
    offset = 0;
    error = img4_read_one(manifest_body, manifest_body_length, &offset, &body);
    if (error != 0) return error;
    if (roles.tag != ASN1_CONSTR_SET || body.tag != ASN1_CONSTR_SET) {
        return EFTYPE;
    }
    img4_der_item_t manp = {0}, objp = {0}, manb = {0};
    offset = 0;
    DERTag previous = 0;
    while (offset < roles.content_length) {
        img4_der_item_t item;
        error = img4_read_one(roles.content, roles.content_length, &offset, &item);
        if (error != 0) return error;
        img4_named_set_t set;
        error = img4_parse_named_set(&item, &set);
        if (error != 0) return error;
        if (previous != 0 && set.tag <= previous) return EFTYPE;
        previous = set.tag;
        if (set.tag == IMG4_TAG_MANP) manp = set.props;
        else if (set.tag == IMG4_TAG_OBJP) objp = set.props;
        else return EAUTH;
        /* Validate unused/vacuous constraints too. */
        size_t po = 0;
        DERTag pp = 0;
        while (po < set.props.content_length) {
            img4_property_t prop;
            error = img4_next_property(&set.props, &po, &pp, &prop, true);
            if (error != 0) return error;
        }
    }
    if (manp.start == NULL && objp.start == NULL) return EFTYPE;
    offset = 0;
    previous = 0;
    while (offset < body.content_length) {
        img4_property_t prop;
        error = img4_next_property(&body, &offset, &previous, &prop, false);
        if (error != 0) return error;
        if (prop.tag == IMG4_TAG_MANB) {
            if (prop.value_tag != ASN1_CONSTR_SET) return EFTYPE;
            manb = prop.value;
        } else if (prop.value_tag == ASN1_CONSTR_SET) {
            return EFTYPE;
        }
    }
    if (manb.start == NULL) return EFTYPE;
    bool has_manp = false, has_object = false;
    offset = 0;
    previous = 0;
    while (offset < manb.content_length) {
        img4_der_item_t item;
        error = img4_read_one(manb.content, manb.content_length, &offset, &item);
        if (error != 0) return error;
        img4_named_set_t set;
        error = img4_parse_named_set(&item, &set);
        if (error != 0) return error;
        if (previous != 0 && set.tag <= previous) return EFTYPE;
        previous = set.tag;
        bool is_manp = set.tag == IMG4_TAG_MANP;
        if (is_manp) has_manp = true;
        else has_object = true;
        const img4_der_item_t *constraints = is_manp ? &manp : &objp;
        bool selected = digest != NULL && !is_manp && set.name == object_type;
        error = img4_apply_constraints(
            constraints->start != NULL ? constraints : NULL, &set.props,
            selected ? digest : NULL);
        if (error != 0) return error;
        if (selected && digest->start == NULL) return EFTYPE;
    }
    if (!has_manp && manp.start != NULL) {
        error = img4_apply_constraints(&manp, NULL, NULL);
        if (error != 0) return error;
    }
    if (!has_object && objp.start != NULL) {
        error = img4_apply_constraints(&objp, NULL, NULL);
        if (error != 0) return error;
    }
    return digest != NULL && digest->start == NULL ? ENOENT : 0;
}


int
_img4_der_read(const uint8_t *buffer, size_t length, size_t *offset,
               img4_der_item_t *item, unsigned depth)
{
    if (item != NULL) {
        memset(item, 0, sizeof(*item));
    }
    if (buffer == NULL || offset == NULL || depth > IMG4_MAX_DER_DEPTH) {
        return EFTYPE;
    }

    const size_t start = *offset;
    if (start >= length) return EFTYPE;
    DERItem encoded = { (DERByte *)(buffer + start), length - start };
    DERDecodedInfo decoded;
    if (DERDecodeItem(&encoded, &decoded) != DR_Success) return EFTYPE;
    const DERTag tag = decoded.tag;
    const unsigned tag_class = buffer[start] >> 6;
    const bool constructed = (tag & ASN1_CONSTRUCTED) != 0;
    const DERTag tag_number =
        tag & ~(ASN1_PRIVATE | ASN1_CONSTRUCTED);
    const size_t content_start = (size_t)(decoded.content.data - buffer);
    const size_t content_length = decoded.content.length;
    const size_t end = content_start + content_length;
    /* libDER already checks high tags, definite lengths, leading zero length
     * octets and bounds. Only long-form lengths below 128 remain non-DER.
     * Locate that octet from the decoded header size, without decoding it. */
    size_t minimal_header = 2;
    if ((buffer[start] & 0x1f) == 0x1f) {
        DERTag number = tag_number;
        do {
            minimal_header++;
            number >>= 7;
        } while (number != 0);
    }
    if (content_length >= 128) {
        size_t remaining = content_length;
        do {
            minimal_header++;
            remaining >>= 8;
        } while (remaining != 0);
    }
    if (content_start - start != minimal_header) return EFTYPE;

    if (tag_class == 0) {
        switch (tag_number) {
        case 0: /* end-of-contents */
            return EFTYPE;
        case ASN1_BOOLEAN:
            if (constructed || content_length != 1 ||
                (buffer[content_start] != 0x00 &&
                 buffer[content_start] != 0xff)) {
                return EFTYPE;
            }
            break;
        case ASN1_INTEGER:
            if (constructed || content_length == 0 ||
                (buffer[content_start] & 0x80u) != 0) {
                return EFTYPE;
            }
            if (content_length > 1 && buffer[content_start] == 0x00 &&
                (buffer[content_start + 1] & 0x80u) == 0) {
                return EFTYPE;
            }
            break;
        case ASN1_BIT_STRING: {
            if (constructed || content_length == 0) {
                return EFTYPE;
            }
            const uint8_t unused = buffer[content_start];
            if (unused > 7 || (content_length == 1 && unused != 0)) {
                return EFTYPE;
            }
            if (unused != 0) {
                const uint8_t mask = (uint8_t)((1u << unused) - 1u);
                if ((buffer[end - 1] & mask) != 0) {
                    return EFTYPE;
                }
            }
            break;
        }
        case ASN1_NULL:
            if (constructed || content_length != 0) {
                return EFTYPE;
            }
            break;
        case ASN1_OBJECT_ID: {
            if (constructed || content_length == 0) {
                return EFTYPE;
            }
            bool at_subidentifier_start = true;
            for (size_t i = 0; i < content_length; i++) {
                const uint8_t byte = buffer[content_start + i];
                if (at_subidentifier_start && byte == 0x80u) {
                    return EFTYPE;
                }
                at_subidentifier_start = (byte & 0x80u) == 0;
            }
            if (!at_subidentifier_start) {
                return EFTYPE;
            }
            break;
        }
        case ASN1_SEQUENCE:
        case ASN1_SET:
            if (!constructed) {
                return EFTYPE;
            }
            break;
        case ASN1_IA5_STRING:
            if (constructed) return EFTYPE;
            for (size_t i = 0; i < content_length; i++) {
                if (buffer[content_start + i] > 0x7f) return EFTYPE;
            }
            break;
        default:
            /* A constructed encoding of a universal primitive (for example
             * an OCTET STRING) is BER, not the DER this parser accepts.
             */
            if (constructed) {
                return EFTYPE;
            }
            break;
        }
    }

    if (item != NULL) {
        item->start = buffer + start;
        item->total_length = end - start;
        item->content = buffer + content_start;
        item->content_length = content_length;
        item->tag = tag;
    }


    *offset = end;
    return 0;
}

/* A heterogeneous ASN.1 SET is tag-ordered; equal-tag SET OF members use
 * complete DER lexicographic order (not length-first order). Image4 names
 * have identical private class/method bits and are therefore numeric-sorted.
 * Schema-level duplicate names are rejected by the streaming matcher. */
static int
img4_validate_item(const img4_der_item_t *item, unsigned depth)
{
    if ((item->tag & ASN1_CONSTRUCTED) == 0) return 0;
    size_t offset = 0;
    img4_der_item_t previous = {0};
    while (offset < item->content_length) {
        img4_der_item_t child;
        int error = _img4_der_read(item->content, item->content_length,
                                   &offset, &child, depth + 1);
        if (error != 0) return error;
        if (item->tag == ASN1_CONSTR_SET && previous.start != NULL) {
            DERTag a = previous.tag & ~ASN1_CONSTRUCTED;
            DERTag b = child.tag & ~ASN1_CONSTRUCTED;
            if (a > b) return EFTYPE;
            if (a == b) {
                size_t common = previous.total_length < child.total_length ?
                    previous.total_length : child.total_length;
                int order = memcmp(previous.start, child.start, common);
                if (order > 0 || (order == 0 &&
                    previous.total_length > child.total_length)) return EFTYPE;
            }
        }
        error = img4_validate_item(&child, depth + 1);
        if (error != 0) return error;
        previous = child;
    }
    return 0;
}

int
_img4_der_validate(const void *buffer, size_t length)
{
    if (buffer == NULL || length == 0) {
        return EFTYPE;
    }

    const uint8_t *bytes = buffer;
    size_t offset = 0;
    img4_der_item_t item;
    const int error = _img4_der_read(bytes, length, &offset, &item, 0);
    if (error != 0) {
        return error;
    }
    if (offset != length) return EFTYPE;
    return img4_validate_item(&item, 0);
}

bool
_img4_der_item_equal(const img4_der_item_t *a, const img4_der_item_t *b)
{
    if (a == NULL || b == NULL || a->start == NULL || b->start == NULL) {
        return false;
    }
    return a->total_length == b->total_length &&
           memcmp(a->start, b->start, a->total_length) == 0;
}
