/* Copyright (C) 2026 Darling contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DARLING_CRYPTEX_H
#define DARLING_CRYPTEX_H

#include <stdint.h>
#include <os/object.h>
#include <img4/chip.h>

#if OS_OBJECT_USE_OBJC
OS_OBJECT_DECL(cryptex);
OS_OBJECT_DECL(cryptex_attr);
OS_OBJECT_DECL(cryptex_bundle);
#else
typedef struct cryptex_s *cryptex_t;
typedef struct cryptex_attr_s *cryptex_attr_t;
typedef struct cryptex_bundle_s *cryptex_bundle_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

cryptex_attr_t cryptex_attr_create(void) OS_OBJECT_RETURNS_RETAINED;
void cryptex_attr_set_authentication(cryptex_attr_t attributes, uint64_t value);
void cryptex_attr_set_personalization_flags(cryptex_attr_t attributes, uint64_t value);
void cryptex_attr_set_mount_flags(cryptex_attr_t attributes, uint64_t value);

/* Claims *directory_fd, replacing it with -1. Parsing is deferred until copy. */
cryptex_bundle_t cryptex_bundle_create_with_directory(int *directory_fd, uint64_t flags)
    OS_OBJECT_RETURNS_RETAINED;
/* Returns a Darwin error number; success stores a retained cryptex object. */
int cryptex_bundle_copy_cryptex_for_chip(cryptex_bundle_t bundle,
    const img4_chip_instance_t *chip, const char *variant, cryptex_t *result);

/* Neither function consumes the caller's descriptor or cryptex reference. */
int cryptex_attach_personalized_manifest(cryptex_t cryptex, int descriptor);
int cryptex_fcopy_personalized_manifest(cryptex_t cryptex, int descriptor);

#ifdef __cplusplus
}
#endif
#endif
