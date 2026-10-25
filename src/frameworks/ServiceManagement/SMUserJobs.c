/*
 * Copyright (c) 2005-2011 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

#include "SMJobPrivate.h"
#include <ServiceManagement/SMErrors.h>
#include <bootstrap.h>
#include <launch.h>
#include <launch_priv.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Representation and ownership follow Apple's CF2launch_data and dictionary
 * callback in src/launchd/support/launchctl.c:1991-2065 (license above).
 * Upstream darlinghq/darling 60ba801decee7a00782f74f6be4c8ffb013f79ff,
 * src/frameworks/ServiceManagement/ServiceManagement.c:25-33, still has only
 * success stubs; the read-only VibeDarling reference at
 * 4fbfb1d97d1df0f7881393ec710df6269c3849a4 has the same stubs and the same
 * unchecked converter in src/launchd/support/launchctl.c:2029-2105. Neither
 * supplies a complete ServiceManagement client to reuse.
 *
 * This adaptation checks lossless numeric/UTF-8 conversion, allocations and
 * ownership transfer. launch.h:174-185 has no date or null representation:
 * CFDate, CFNull and non-plist objects are rejected, not disguised as numbers.
 * Embedded NUL strings and case-insensitively colliding dictionary keys are
 * also unrepresentable by liblaunch's C-string/case-insensitive dictionaries.
 * Nothing is sent until the entire borrowed dictionary has been converted.
 */

/* An ancestor chain detects container cycles without allocating a second tree. */
struct SMPlistAncestor {
    CFTypeRef value;
    const struct SMPlistAncestor *parent;
};

static launch_data_t SMCreateLaunchData(CFTypeRef value,
    const struct SMPlistAncestor *ancestors, int *failure);

static Boolean SMUserFailure(CFErrorRef *error, CFStringRef domain,
    CFIndex code, CFStringRef description, CFStringRef underlyingDomain,
    CFIndex underlyingCode)
{
    if (error) {
        const void *keys[2] = { kCFErrorDescriptionKey, kCFErrorUnderlyingErrorKey };
        const void *values[2] = { description, NULL };
        CFErrorRef underlying = NULL;
        if (underlyingDomain) {
            underlying = CFErrorCreate(kCFAllocatorDefault, underlyingDomain,
                underlyingCode, NULL);
        }
        values[1] = underlying;
        *error = CFErrorCreateWithUserInfoKeysAndValues(kCFAllocatorDefault,
            domain, code, keys, values, underlying ? 2 : 1);
        if (underlying) {
            CFRelease(underlying);
        }
        if (!*error) {
            *error = CFErrorCreate(kCFAllocatorDefault, domain, code, NULL);
            if (!*error) {
                errno = ENOMEM;
            }
        }
    }
    return false;
}

static Boolean SMConversionFailure(CFErrorRef *error, int failure)
{
    return SMUserFailure(error, kSMErrorDomainFramework,
        failure == ENOMEM ? kSMErrorInternalFailure : kSMErrorInvalidPlist,
        CFSTR("The job or label cannot be represented in the launchd request."),
        kCFErrorDomainPOSIX, failure);
}

/* The result borrows string or *allocated; callers free only *allocated. */
static const char *SMGetUTF8(CFStringRef string, char **allocated, int *failure)
{
    *allocated = NULL;
    CFIndex length = CFStringGetLength(string);
    CFIndex bytes = 0;
    if (length < 0 || CFStringGetBytes(string, CFRangeMake(0, length),
            kCFStringEncodingUTF8, 0, false, NULL, 0, &bytes) != length || bytes < 0) {
        *failure = EILSEQ;
        return NULL;
    }
    if ((uintmax_t)bytes >= SIZE_MAX) {
        *failure = EOVERFLOW;
        return NULL;
    }
    const char *direct = CFStringGetCStringPtr(string, kCFStringEncodingUTF8);
    if (direct) {
        if (strlen(direct) != (size_t)bytes) {
            *failure = EILSEQ;
            return NULL;
        }
        return direct;
    }
    char *result = malloc((size_t)bytes + 1);
    if (!result) {
        *failure = ENOMEM;
        return NULL;
    }
    CFIndex written = 0;
    if (CFStringGetBytes(string, CFRangeMake(0, length), kCFStringEncodingUTF8,
            0, false, (UInt8 *)result, bytes, &written) != length || written != bytes ||
            memchr(result, '\0', (size_t)bytes)) {
        free(result);
        *failure = EILSEQ;
        return NULL;
    }
    result[bytes] = '\0';
    *allocated = result;
    return result;
}

struct SMDictionaryConversion {
    launch_data_t dictionary;
    const struct SMPlistAncestor *ancestors;
    int *failure;
};

static void SMConvertDictionaryEntry(const void *key, const void *value, void *context)
{
    struct SMDictionaryConversion *conversion = context;
    if (*conversion->failure) {
        return;
    }
    if (!key || CFGetTypeID(key) != CFStringGetTypeID()) {
        *conversion->failure = EINVAL;
        return;
    }
    char *allocated;
    const char *name = SMGetUTF8(key, &allocated, conversion->failure);
    if (!name) {
        return;
    }
    if (launch_data_dict_lookup(conversion->dictionary, name)) {
        free(allocated);
        *conversion->failure = EINVAL;
        return;
    }
    launch_data_t child = SMCreateLaunchData(value, conversion->ancestors,
        conversion->failure);
    if (child && !launch_data_dict_insert(conversion->dictionary, child, name)) {
        launch_data_free(child);
        *conversion->failure = ENOMEM;
    }
    free(allocated);
}

static launch_data_t SMCreateLaunchData(CFTypeRef value,
    const struct SMPlistAncestor *ancestors, int *failure)
{
    if (!value) {
        *failure = EINVAL;
        return NULL;
    }
    CFTypeID type = CFGetTypeID(value);
    launch_data_t result = NULL;
    if (type == CFStringGetTypeID()) {
        char *allocated;
        const char *string = SMGetUTF8(value, &allocated, failure);
        if (!string) {
            return NULL;
        }
        result = launch_data_new_string(string);
        free(allocated);
    } else if (type == CFBooleanGetTypeID()) {
        result = launch_data_new_bool(CFBooleanGetValue(value));
    } else if (type == CFNumberGetTypeID()) {
        if (CFNumberIsFloatType(value)) {
            double number;
            if (!CFNumberGetValue(value, kCFNumberDoubleType, &number)) {
                *failure = ERANGE;
                return NULL;
            }
            result = launch_data_new_real(number);
        } else {
            long long number;
            if (!CFNumberGetValue(value, kCFNumberLongLongType, &number)) {
                *failure = ERANGE;
                return NULL;
            }
            result = launch_data_new_integer(number);
        }
    } else if (type == CFDataGetTypeID()) {
        CFIndex length = CFDataGetLength(value);
        if (length < 0 || (uintmax_t)length > SIZE_MAX) {
            *failure = EOVERFLOW;
            return NULL;
        }
        const UInt8 *bytes = CFDataGetBytePtr(value);
        if (length && !bytes) {
            *failure = EINVAL;
            return NULL;
        }
        /* A non-NULL source also accommodates empty CFData. */
        result = launch_data_new_opaque(length ? bytes : (const UInt8 *)"",
            (size_t)length);
    } else if (type == CFArrayGetTypeID() || type == CFDictionaryGetTypeID()) {
        for (const struct SMPlistAncestor *parent = ancestors; parent; parent = parent->parent) {
            if (parent->value == value) {
                *failure = EINVAL;
                return NULL;
            }
        }
        struct SMPlistAncestor current = { value, ancestors };
        CFIndex count = type == CFArrayGetTypeID() ? CFArrayGetCount(value) :
            CFDictionaryGetCount(value);
        size_t width = type == CFArrayGetTypeID() ? sizeof(launch_data_t) :
            2 * sizeof(launch_data_t);
        if (count < 0 || (uintmax_t)count > SIZE_MAX / width) {
            *failure = EOVERFLOW;
            return NULL;
        }
        result = launch_data_alloc(type == CFArrayGetTypeID() ? LAUNCH_DATA_ARRAY :
            LAUNCH_DATA_DICTIONARY);
        if (!result) {
            *failure = ENOMEM;
            return NULL;
        }
        if (type == CFArrayGetTypeID()) {
            for (CFIndex index = 0; index < count; ++index) {
                launch_data_t child = SMCreateLaunchData(CFArrayGetValueAtIndex(value, index),
                    &current, failure);
                if (!child) {
                    break;
                }
                if (!launch_data_array_set_index(result, child, (size_t)index)) {
                    launch_data_free(child);
                    *failure = ENOMEM;
                    break;
                }
            }
        } else {
            struct SMDictionaryConversion conversion = { result, &current, failure };
            CFDictionaryApplyFunction(value, SMConvertDictionaryEntry, &conversion);
        }
        if (*failure) {
            launch_data_free(result);
            return NULL;
        }
    } else {
        *failure = ENOTSUP;
        return NULL;
    }
    if (!result) {
        *failure = ENOMEM;
    }
    return result;
}

/* Consumes payload on every path; successful insertion transfers its ownership. */
static Boolean SMUserRequest(const char *operation, launch_data_t payload, CFErrorRef *error)
{
    launch_data_t request = launch_data_alloc(LAUNCH_DATA_DICTIONARY);
    if (!request) {
        launch_data_free(payload);
        return SMConversionFailure(error, ENOMEM);
    }
    if (!launch_data_dict_insert(request, payload, operation)) {
        launch_data_free(payload);
        launch_data_free(request);
        return SMConversionFailure(error, ENOMEM);
    }
    launch_data_t reply = _launch_msg_user(request);
    int transportError = errno;
    launch_data_free(request);
    if (!reply) {
        /* EIO denotes a missing transport diagnostic, not a launchd result. */
        if (!transportError) {
            transportError = EIO;
        }
        return SMUserFailure(error, kSMErrorDomainIPC, transportError,
            CFSTR("The caller's user launchd request could not be delivered."),
            kCFErrorDomainPOSIX, transportError);
    }
    if (launch_data_get_type(reply) != LAUNCH_DATA_ERRNO) {
        launch_data_free(reply);
        return SMUserFailure(error, kSMErrorDomainIPC, EPROTO,
            CFSTR("The user launchd reply has an unexpected type."),
            kCFErrorDomainPOSIX, EPROTO);
    }
    int launchdError = launch_data_get_errno(reply);
    launch_data_free(reply);
    if (launchdError) {
        return SMUserFailure(error, kSMErrorDomainLaunchd, launchdError,
            CFSTR("User launchd rejected the job request."),
            kCFErrorDomainPOSIX, launchdError);
    }
    if (error) {
        *error = NULL;
    }
    return true;
}

Boolean _SMJobSubmitUser(CFDictionaryRef job, CFErrorRef *error)
{
    if (error) {
        *error = NULL;
    }
    if (!job || CFGetTypeID(job) != CFDictionaryGetTypeID()) {
        return SMConversionFailure(error, EINVAL);
    }
    int failure = 0;
    launch_data_t payload = SMCreateLaunchData(job, NULL, &failure);
    if (!payload) {
        return SMConversionFailure(error, failure);
    }
    /* ipc.c:423-430 returns one actual errno for a dictionary submission. */
    return SMUserRequest(LAUNCH_KEY_SUBMITJOB, payload, error);
}

Boolean _SMJobRemoveUser(CFStringRef label, Boolean wait, CFErrorRef *error)
{
    if (error) {
        *error = NULL;
    }
    if (!label || CFGetTypeID(label) != CFStringGetTypeID()) {
        return SMConversionFailure(error, EINVAL);
    }
    int failure = 0;
    char *allocated;
    const char *name = SMGetUTF8(label, &allocated, &failure);
    if (!name) {
        return SMConversionFailure(error, failure);
    }
    if (wait) {
        size_t length = strnlen(name, sizeof(name_t));
        if (!length || length >= sizeof(name_t)) {
            free(allocated);
            return SMConversionFailure(error, length ? ENAMETOOLONG : EINVAL);
        }
        /*
         * core.c:8059-8115 retains the MIG reply until removal completes;
         * core.c:11880-11902 sends that completion. ESRCH polling is not wait.
         * Keep bootstrap statuses separate from transport/kernel statuses and
         * never reinterpret the raw result as a POSIX errno. This is not a
         * claim of byte-exact native ServiceManagement error-domain mapping.
         */
        kern_return_t status = _launch_remove_user_wait(name);
        free(allocated);
        if (status == KERN_SUCCESS) {
            return true;
        }
        switch (status) {
        case BOOTSTRAP_NOT_PRIVILEGED:
        case BOOTSTRAP_NAME_IN_USE:
        case BOOTSTRAP_UNKNOWN_SERVICE:
        case BOOTSTRAP_SERVICE_ACTIVE:
        case BOOTSTRAP_BAD_COUNT:
        case BOOTSTRAP_NO_MEMORY:
        case BOOTSTRAP_NO_CHILDREN:
            return SMUserFailure(error, kSMErrorDomainLaunchd, status,
                CFSTR("User launchd rejected the completion-waiting removal request."),
                NULL, 0);
        default:
            return SMUserFailure(error, kSMErrorDomainIPC, status,
                CFSTR("The user launchd removal RPC failed."),
                kCFErrorDomainMach, status);
        }
    }
    launch_data_t payload = launch_data_new_string(name);
    free(allocated);
    if (!payload) {
        return SMConversionFailure(error, ENOMEM);
    }
    /* ipc.c:417-422 performs RemoveJob; success need not mean process exit. */
    return SMUserRequest(LAUNCH_KEY_REMOVEJOB, payload, error);
}
