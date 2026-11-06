/* Copyright (C) 2026 Darling contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DARLING_CRYPTEX_INTERNAL_H
#define DARLING_CRYPTEX_INTERNAL_H

#import <Foundation/Foundation.h>
#include <os/object_private.h>
#include <stdbool.h>
#include "cryptex.h"

/* Private Darling layout, not a copy of an architecture-specific Apple object. */
struct cryptex_identity {
    uint32_t version;
    uint64_t flags;
    uint32_t chip, board, security_domain;
    uint32_t cryptex_chip, type, subtype, product_class;
};

enum cryptex_asset_kind {
    CRYPTEX_ASSET_CPX_DMG, CRYPTEX_ASSET_TRUST_CACHE, CRYPTEX_ASSET_INFO,
    CRYPTEX_ASSET_MANIFEST, CRYPTEX_ASSET_ROOT, CRYPTEX_ASSET_CONTENT,
    CRYPTEX_ASSET_PERSONALIZED_DMG, CRYPTEX_ASSET_ROOT_HASH,
    CRYPTEX_ASSET_GENERIC_TRUST_CACHE, CRYPTEX_ASSET_GENERIC_INFO,
    CRYPTEX_ASSET_GENERIC_DMG, CRYPTEX_ASSET_GENERIC_VOLUME,
    CRYPTEX_ASSET_PROPERTIES, CRYPTEX_ASSET_COUNT
};

struct cryptex_asset {
    int fd;
    char *path;
};

struct cryptex_attr_s {
    _OS_OBJECT_HEADER(void *isa, ref_count, xref_count);
    uint64_t authentication, personalization_flags, mount_flags;
};

struct cryptex_s {
    _OS_OBJECT_HEADER(void *isa, ref_count, xref_count);
    struct cryptex_s *next;
    struct cryptex_identity identity;
    uint64_t flags;
    NSString *variant;
    NSArray *signatures;
    struct cryptex_asset assets[CRYPTEX_ASSET_COUNT];
    uint32_t nonce_domain, subtype;
    NSString *version, *preauthorization_version;
    bool use_product_class;
};

struct cryptex_bundle_s {
    _OS_OBJECT_HEADER(void *isa, ref_count, xref_count);
    uint64_t flags;
    int directory_fd, restore_fd;
    NSDictionary *manifest;
    struct cryptex_s *entries;
};

struct cryptex_s *_cryptex_alloc(void);
struct cryptex_bundle_s *_cryptex_bundle_alloc(void);
/* Claims a descriptor only on successful acquisition of its canonical path. */
int _cryptex_asset_claim(struct cryptex_asset *asset, int *fd);
void _cryptex_asset_close(struct cryptex_asset *asset);
int _cryptex_assets_open(struct cryptex_s *cryptex);
void _cryptex_assets_close(struct cryptex_s *cryptex);

#endif
