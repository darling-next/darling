/* Copyright (C) 2026 Darling contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DARLING_IMG4_CHIP_INTERNAL_H
#define DARLING_IMG4_CHIP_INTERNAL_H

#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>

#define IMG4_ORACLE_KEY(name) CFSTR("94B73556-2197-4702-82A8-3E1337DAFBFB:" name)
enum { IMG4_GENERIC_X86_CERTIFICATE_EPOCH = 1 };

/* One live property query; unlike full instantiation this does not query every
 * identity field or synthesize an instance. Errors preserve the output.
 */
__attribute__((visibility("hidden")))
int _img4_query_oracle_value(CFStringRef key, size_t width, uint64_t *value);

#endif
