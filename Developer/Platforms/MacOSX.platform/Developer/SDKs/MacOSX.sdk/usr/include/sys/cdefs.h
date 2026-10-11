/*
 * Copyright (c) 2026 Darling project.  All rights reserved.
 *
 * The SDK's sys/cdefs.h: the platform selection xnu's sys/cdefs.h leaves to its
 * client, made once for the one platform this SDK is.  The header below is
 * xnu's, included from a path relative to this file's place in the SDK.
 *
 * xnu chooses a product's symbol-suffix rules (__DARWIN_ONLY_UNIX_CONFORMANCE,
 * __DARWIN_ONLY_64_BIT_INO_T, __DARWIN_ONLY_VERS_1050) only for a defined
 * PLATFORM_* macro.  Darling's own build passes -DPLATFORM_MacOSX, so it always
 * had a selection; a consumer of this SDK defines none, and xnu's header then
 * leaves all three undefined.  Userspace code then takes the pre-UNIX03 symbol
 * aliases, open$UNIX2003 and fcntl$UNIX2003 among them, which Darling's libc
 * does not implement, so linking fails naming exactly those symbols - the Swift
 * SDK overlays' first link did.
 *
 * Apple's SDK does not have this problem because its sys/cdefs.h is processed
 * for the SDK's platform before it is installed.  Darling's SDK is one platform
 * too, so the selection is made here rather than left to each client's command
 * line.  A client that names its own platform, or a kernel build (KERNEL), keeps
 * what it asked for.  Darling's own build defines PLATFORM_MacOSX globally and
 * is unaffected.
 */

#ifndef __DARLING_SYS_CDEFS_H_
#define __DARLING_SYS_CDEFS_H_

#if !defined(KERNEL) && !defined(PLATFORM_MacOSX) && \
    !defined(PLATFORM_iPhoneOS) && !defined(PLATFORM_iPhoneOSNano) && \
    !defined(PLATFORM_iPhoneSimulator) && !defined(PLATFORM_iPhoneNanoSimulator) && \
    !defined(PLATFORM_tvOS) && !defined(PLATFORM_AppleTVOS) && \
    !defined(PLATFORM_tvSimulator) && !defined(PLATFORM_AppleTVSimulator) && \
    !defined(PLATFORM_WatchOS) && !defined(PLATFORM_WatchSimulator) && \
    !defined(PLATFORM_BridgeOS) && !defined(PLATFORM_DriverKit)
#define PLATFORM_MacOSX 1
#endif

#include "../../../../../../../../../src/external/xnu/bsd/sys/cdefs.h"

#endif /* __DARLING_SYS_CDEFS_H_ */
