/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Client protocol observed in the original-system x86_64 ServiceManagement on
 * macOS 26.6.2 (25G83), without invoking Submit/Remove or modifying services.
 * Public inputs/ownership follow the SDK; request fields are version-scoped
 * implementation evidence, not a recovered private Apple header.
 *
 * Vendored, current Darling and current Vibe implementations return fake
 * success, so they cannot supply this client. The authority remains the real
 * privileged broker: this code neither creates an authorization nor grants
 * daemon-modification rights. Missing broker/authd support must fail normally.
 */
#include "SMJobPrivate.h"
#include <CoreFoundation/CFPropertyList.h>
#include <Security/Authorization.h>
#include <xpc/private/pipe.h>
#include <xpc/private/plist.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static Boolean client_error(CFErrorRef *error, CFStringRef domain, CFIndex code)
{
	if (error)
		*error = CFErrorCreate(kCFAllocatorDefault, domain, code, NULL);
	return false;
}

static xpc_object_t request_with_authorization(AuthorizationRef auth, CFErrorRef *error)
{
	xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
	if (!request) {
		client_error(error, kCFErrorDomainPOSIX, ENOMEM);
		return NULL;
	}
	if (auth) {
		AuthorizationExternalForm external;
		OSStatus status = AuthorizationMakeExternalForm(auth, &external);
		if (status != errAuthorizationSuccess) {
			xpc_release(request);
			client_error(error, kCFErrorDomainOSStatus, status);
			return NULL;
		}
		xpc_dictionary_set_data(request, "authref", &external, sizeof(external));
	}
	/* An absent token is not a grant: the privileged server authorizes peers. */
	return request;
}

static Boolean send_request(int64_t routine, xpc_object_t request, CFErrorRef *error)
{
	xpc_pipe_t pipe = xpc_pipe_create("com.apple.xpc.smd", XPC_PIPE_PRIVILEGED);
	if (!pipe)
		return client_error(error, kSMErrorDomainIPC, ENOTCONN);

	xpc_dictionary_set_int64(request, "routine", routine);
	xpc_object_t reply = NULL;
	int status = xpc_pipe_routine(pipe, request, &reply);
	xpc_release(pipe);
	if (status != 0) {
		if (reply)
			xpc_release(reply);
		return client_error(error, kSMErrorDomainIPC, status);
	}
	if (!reply || xpc_get_type(reply) != XPC_TYPE_DICTIONARY) {
		if (reply)
			xpc_release(reply);
		return client_error(error, kSMErrorDomainIPC, EPROTO);
	}

	/* Native reads a signed error; absence denotes a successful reply. */
	xpc_object_t failure = xpc_dictionary_get_value(reply, "error");
	if (failure && xpc_get_type(failure) != XPC_TYPE_INT64) {
		xpc_release(reply);
		return client_error(error, kSMErrorDomainIPC, EPROTO);
	}
	int64_t result = failure ? xpc_int64_get_value(failure) : 0;
	xpc_release(reply);
	if (result > LONG_MAX || result < LONG_MIN)
		return client_error(error, kSMErrorDomainIPC, ERANGE);
	if (result != 0)
		return client_error(error, kSMErrorDomainLaunchd, (CFIndex)result);
	return true;
}

Boolean _SMJobSubmitSystem(CFDictionaryRef job, AuthorizationRef auth, CFErrorRef *error)
{
	if (error)
		*error = NULL;
	if (!job || CFGetTypeID(job) != CFDictionaryGetTypeID())
		return client_error(error, kSMErrorDomainFramework, kSMErrorInvalidPlist);

	/*
	 * Reuse the existing complete binary-plist decoder. The legacy direct
	 * CF/XPC bridge truncates real numbers to integers and does not unwind
	 * nested conversion failures; do not silently use it for job payloads.
	 */
	CFErrorRef conversion_error = NULL;
	CFDataRef data = CFPropertyListCreateData(kCFAllocatorDefault, job,
		kCFPropertyListBinaryFormat_v1_0, 0, &conversion_error);
	if (!data) {
		if (conversion_error) {
			if (error)
				*error = conversion_error;
			else
				CFRelease(conversion_error);
			return false;
		}
		return client_error(error, kSMErrorDomainFramework, kSMErrorInvalidPlist);
	}
	xpc_object_t payload = xpc_create_from_plist(CFDataGetBytePtr(data),
		(size_t)CFDataGetLength(data));
	CFRelease(data);
	if (!payload || xpc_get_type(payload) != XPC_TYPE_DICTIONARY) {
		if (payload)
			xpc_release(payload);
		return client_error(error, kSMErrorDomainFramework, kSMErrorInvalidPlist);
	}
	xpc_object_t request = request_with_authorization(auth, error);
	if (!request) {
		xpc_release(payload);
		return false;
	}
	xpc_dictionary_set_value(request, "job", payload);
	xpc_release(payload);
	Boolean success = send_request(1000, request, error);
	xpc_release(request);
	return success;
}

Boolean _SMJobRemoveSystem(CFStringRef label, AuthorizationRef auth, Boolean wait,
	CFErrorRef *error)
{
	if (error)
		*error = NULL;
	if (!label || CFGetTypeID(label) != CFStringGetTypeID())
		return client_error(error, kSMErrorDomainFramework, kSMErrorInvalidPlist);
	CFIndex length = CFStringGetLength(label);
	CFIndex maximum = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8);
	if (length == 0 || maximum < 0 || (uintmax_t)maximum >= SIZE_MAX)
		return client_error(error, kSMErrorDomainFramework, kSMErrorInvalidPlist);
	char *name = malloc((size_t)maximum + 1);
	if (!name)
		return client_error(error, kCFErrorDomainPOSIX, ENOMEM);
	CFIndex used = 0;
	CFIndex converted = CFStringGetBytes(label, CFRangeMake(0, length),
		kCFStringEncodingUTF8, 0, false, (UInt8 *)name, maximum, &used);
	if (converted != length || memchr(name, '\0', (size_t)used)) {
		free(name);
		return client_error(error, kSMErrorDomainFramework, kSMErrorInvalidPlist);
	}
	name[used] = '\0';
	xpc_object_t request = request_with_authorization(auth, error);
	if (!request) {
		free(name);
		return false;
	}
	xpc_dictionary_set_string(request, "label", name);
	free(name);
	xpc_dictionary_set_bool(request, "wait", wait);
	Boolean success = send_request(1001, request, error);
	xpc_release(request);
	return success;
}
