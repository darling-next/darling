/* Copyright (C) 2026 Darling contributors. SPDX-License-Identifier: GPL-3.0-or-later
 * Image4 userspace runtime/buffer ABI, from XNU EXTERNAL_HEADERS/img4/runtime.h.
 * The structure version describes this interface, not an Apple runtime release.
 */
#ifndef DARLING_IMG4_RUNTIME_H
#define DARLING_IMG4_RUNTIME_H

#include <img4/chip.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t img4_struct_version_t;
typedef struct _img4_runtime img4_runtime_t;
typedef struct _img4_buff img4_buff_t;
typedef uint64_t img4_identifier_t;
typedef uint64_t img4_log_level_t;

enum {
    IMG4_IDENTIFIER_CEPO,
    IMG4_IDENTIFIER_BORD,
    IMG4_IDENTIFIER_CHIP,
    IMG4_IDENTIFIER_SDOM,
    IMG4_IDENTIFIER_ECID,
    IMG4_IDENTIFIER_CPRO,
    IMG4_IDENTIFIER_CSEC,
    IMG4_IDENTIFIER_EPRO,
    IMG4_IDENTIFIER_ESEC,
    IMG4_IDENTIFIER_IUOU,
    IMG4_IDENTIFIER_RSCH,
    IMG4_IDENTIFIER_CHMH,
    IMG4_IDENTIFIER_AMNM,
    IMG4_IDENTIFIER_EUOU,
    _IMG4_IDENTIFIER_CNT
};
enum {
    IMG4_LOG_LEVEL_ERROR,
    IMG4_LOG_LEVEL_INFO,
    IMG4_LOG_LEVEL_DEBUG,
    _IMG4_LOG_LEVEL_CNT
};

#define IMG4_RUNTIME_STRUCT_VERSION 1u
#define IMG4_BUFF_STRUCT_VERSION 0u
#define IMG4_DGST_STRUCT_VERSION 0u

typedef void (*img4_runtime_init_t)(const img4_runtime_t *);
typedef void *(*img4_runtime_alloc_t)(const img4_runtime_t *, size_t);
typedef void (*img4_runtime_dealloc_t)(const img4_runtime_t *, void *, size_t);
typedef void (*img4_runtime_log_t)(const img4_runtime_t *, void *,
                                  img4_log_level_t, const char *, ...);
typedef void *(*img4_runtime_log_handle_t)(const img4_runtime_t *);
typedef int (*img4_runtime_get_identifier_bool_t)(const img4_runtime_t *,
    const img4_chip_t *, img4_identifier_t, bool *);
typedef int (*img4_runtime_get_identifier_uint32_t)(const img4_runtime_t *,
    const img4_chip_t *, img4_identifier_t, uint32_t *);
typedef int (*img4_runtime_get_identifier_uint64_t)(const img4_runtime_t *,
    const img4_chip_t *, img4_identifier_t, uint64_t *);
typedef int (*img4_runtime_get_identifier_digest_t)(const img4_runtime_t *,
    const img4_chip_t *, img4_identifier_t, img4_dgst_t *);

struct _img4_runtime {
    img4_struct_version_t i4rt_version;
    const char *i4rt_name;
    img4_runtime_init_t i4rt_init;
    img4_runtime_alloc_t i4rt_alloc;
    img4_runtime_dealloc_t i4rt_dealloc;
    img4_runtime_log_t i4rt_log;
    img4_runtime_log_handle_t i4rt_log_handle;
    img4_runtime_get_identifier_bool_t i4rt_get_identifier_bool;
    img4_runtime_get_identifier_uint32_t i4rt_get_identifier_uint32;
    img4_runtime_get_identifier_uint64_t i4rt_get_identifier_uint64;
    img4_runtime_get_identifier_digest_t i4rt_get_identifier_digest;
    void *i4rt_context;
};

struct _img4_buff {
    img4_struct_version_t i4b_version;
    uint8_t *i4b_bytes;
    size_t i4b_len;
    img4_runtime_dealloc_t i4b_dealloc;
};


extern const img4_runtime_t _img4_runtime_default;
#define IMG4_RUNTIME_DEFAULT (&_img4_runtime_default)
#if defined(__cplusplus) && __cplusplus >= 201103L
#define IMG4_BUFF_INIT (img4_buff_t{ IMG4_BUFF_STRUCT_VERSION, NULL, 0, NULL })
#define IMG4_DGST_INIT (img4_dgst_t{ IMG4_DGST_STRUCT_VERSION, 0, { 0 } })
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define IMG4_BUFF_INIT ((img4_buff_t){ IMG4_BUFF_STRUCT_VERSION, NULL, 0, NULL })
#define IMG4_DGST_INIT ((img4_dgst_t){ IMG4_DGST_STRUCT_VERSION, 0, { 0 } })
#else
#define IMG4_BUFF_INIT { IMG4_BUFF_STRUCT_VERSION, NULL, 0, NULL }
#define IMG4_DGST_INIT { IMG4_DGST_STRUCT_VERSION, 0, { 0 } }
#endif
/* Same main-image pointer set ABI as XNU, without a kernel-only SDK header. */
#if __LP64__
#define _IMG4_RUNTIME_SET_SECTION "__DATA_CONST,__img4_rt"
#else
#define _IMG4_RUNTIME_SET_SECTION "__DATA,__img4_rt"
#endif
#define IMG4_RUNTIME_REGISTER(runtime) \
    static const img4_runtime_t *const _img4_registered_##runtime \
    __attribute__((used, section(_IMG4_RUNTIME_SET_SECTION))) = &(runtime)

/* Missing identities stay errors, never synthesized production/security state. */
void img4_buff_dealloc(img4_buff_t *buffer);

#ifdef __cplusplus
}
#endif
#endif
