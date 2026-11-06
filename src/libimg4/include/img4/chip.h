/*
 * Darling Image4 chip-instance ABI.
 * Layout and omission bits follow Apple's XNU EXTERNAL_HEADERS/img4/chip.h
 * (structure version 6). This header deliberately exposes only the implemented
 * generic x86 chip family and instantiation operation.
 */
#ifndef DARLING_IMG4_CHIP_H
#define DARLING_IMG4_CHIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _img4_chip img4_chip_t;
typedef uint64_t img4_chip_instance_omit_t;
#define IMG4_CHIP_INSTANCE_STRUCT_VERSION 6u
#define IMG4_DGST_MAX_LEN 48u

typedef struct _img4_dgst {
    uint16_t i4d_version;
    size_t i4d_len;
    uint8_t i4d_bytes[IMG4_DGST_MAX_LEN];
} img4_dgst_t;

enum {
    IMG4_CHIP_INSTANCE_OMIT_CEPO = 1u << 0,
    IMG4_CHIP_INSTANCE_OMIT_BORD = 1u << 1,
    IMG4_CHIP_INSTANCE_OMIT_CHIP = 1u << 2,
    IMG4_CHIP_INSTANCE_OMIT_SDOM = 1u << 3,
    IMG4_CHIP_INSTANCE_OMIT_ECID = 1u << 4,
    IMG4_CHIP_INSTANCE_OMIT_CPRO = 1u << 5,
    IMG4_CHIP_INSTANCE_OMIT_CSEC = 1u << 6,
    IMG4_CHIP_INSTANCE_OMIT_EPRO = 1u << 7,
    IMG4_CHIP_INSTANCE_OMIT_ESEC = 1u << 8,
    IMG4_CHIP_INSTANCE_OMIT_IUOU = 1u << 9,
    IMG4_CHIP_INSTANCE_OMIT_RSCH = 1u << 10,
    IMG4_CHIP_INSTANCE_OMIT_EUOU = 1u << 11,
    IMG4_CHIP_INSTANCE_OMIT_ESDM = 1u << 12,
    IMG4_CHIP_INSTANCE_OMIT_FPGT = 1u << 13,
    IMG4_CHIP_INSTANCE_OMIT_UDID = 1u << 14,
    IMG4_CHIP_INSTANCE_OMIT_FCHP = 1u << 15,
    IMG4_CHIP_INSTANCE_OMIT_TYPE = 1u << 16,
    IMG4_CHIP_INSTANCE_OMIT_STYP = 1u << 17,
    IMG4_CHIP_INSTANCE_OMIT_CLAS = 1u << 18,
};

typedef struct _img4_chip_instance {
    uint16_t chid_version;
    const img4_chip_t *chid_chip_family;
    img4_chip_instance_omit_t chid_omit;
    uint32_t chid_cepo, chid_bord, chid_chip, chid_sdom;
    uint64_t chid_ecid;
    bool chid_cpro, chid_csec, chid_epro, chid_esec;
    bool chid_iuou, chid_rsch, chid_euou;
    uint32_t chid_esdm;
    bool chid_fpgt;
    img4_dgst_t chid_udid;
    uint32_t chid_fchp, chid_type, chid_styp, chid_clas;
} img4_chip_instance_t;

extern const img4_chip_t _img4_chip_cryptex1_generic_x86;
#define IMG4_CHIP_CRYPTEX1_GENERIC_X86 (&_img4_chip_cryptex1_generic_x86)

/* Returns Darwin errno values directly. Failure leaves output unchanged.
 * This implementation accepts structure version 6 only (EINVAL otherwise).
 * Missing oracle properties are omitted, never replaced with machine identity.
 */
int img4_chip_instantiate(const img4_chip_t *chip, img4_chip_instance_t *instance);

#ifdef __cplusplus
}
#endif
#endif
