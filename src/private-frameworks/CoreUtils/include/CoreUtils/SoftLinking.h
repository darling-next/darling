/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DARLING_COREUTILS_SOFT_LINKING_H
#define DARLING_COREUTILS_SOFT_LINKING_H
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <stdlib.h>

/* Source-level compatibility, not a recovered private CoreUtils header.
 * The absolute framework path, RTLD_LAZY|RTLD_LOCAL and cached symbol lookup
 * reuse the Apache-2.0 Apple mDNSMacOSX/SymptomReporter.c loader pattern.
 * Missing optional providers remain absent. Handles stay open for cached calls.
 */
#define _CU_SOFT_LINK_LIBRARY_EX(PATH, FRAMEWORK) \
    static void *cu_soft_framework_##FRAMEWORK(void) { \
        static dispatch_once_t once; \
        static void *handle; \
        dispatch_once(&once, ^{ \
            handle = dlopen((PATH), RTLD_LAZY | RTLD_LOCAL); \
        }); \
        return handle; \
    }

#define SOFT_LINK_FRAMEWORK_EX(DIRECTORY, FRAMEWORK) \
    _CU_SOFT_LINK_LIBRARY_EX("/System/Library/" #DIRECTORY "/" #FRAMEWORK ".framework/" #FRAMEWORK, FRAMEWORK)

#define SOFT_LINK_FUNCTION_EX(FRAMEWORK, FUNCTION, RESULT, PARAMETERS, ARGUMENTS) \
    typedef RESULT (*cu_soft_type_##FRAMEWORK##_##FUNCTION) PARAMETERS; \
    static cu_soft_type_##FRAMEWORK##_##FUNCTION cu_soft_get_##FRAMEWORK##_##FUNCTION(void) { \
        static dispatch_once_t once; \
        static cu_soft_type_##FRAMEWORK##_##FUNCTION function; \
        dispatch_once(&once, ^{ \
            void *handle = cu_soft_framework_##FRAMEWORK(); \
            if (handle) function = (cu_soft_type_##FRAMEWORK##_##FUNCTION)dlsym(handle, #FUNCTION); \
        }); \
        return function; \
    } \
    static RESULT soft_##FUNCTION PARAMETERS { \
        cu_soft_type_##FRAMEWORK##_##FUNCTION function = cu_soft_get_##FRAMEWORK##_##FUNCTION(); \
        /* Calling an absent symbol without checking is a caller error, not success. */ \
        if (!function) abort(); \
        return function ARGUMENTS; \
    }

#define SOFT_LINK_HAS_FUNCTION(FRAMEWORK, FUNCTION) \
    (cu_soft_get_##FRAMEWORK##_##FUNCTION() != NULL)
#endif
