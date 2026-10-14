/*
This file is part of Darling.

Copyright (C) 2020 Lubos Dolezel

Darling is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Darling is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Darling.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <xpc/xpc.h>
#include <xpc/private.h>
#include <LaunchServices/LaunchServices.h>
#include <LaunchServices/LaunchServicesPriv.h>
#include <CoreFoundation/CFPriv.h>
#include <CoreFoundation/CFBundlePriv.h>
#include <CoreFoundation/CFDate.h>
#include <CoreFoundation/CFXPCBridge.h>
#include <CarbonCore/MacErrors.h>
#include <dispatch/dispatch.h>
#include <stdlib.h>
#include <stdio.h>
#include <Block.h>

static xpc_connection_t g_lsdConnection, g_endpointConnection;

static xpc_connection_t getLSDConnection();
static xpc_object_t createCommandDictionary(uint64_t command);
static mach_port_name_t deathPort();
static CFTypeRef g_ourASN;

static CFStringRef g_applicationType = NULL;
static LSServerConnectionAllowedBlock g_connectionAllowedBlock = NULL;

CFDictionaryRef _LSApplicationCheckIn(int sessionID, CFDictionaryRef applicationInfo)
{
	if (g_connectionAllowedBlock && !g_connectionAllowedBlock(applicationInfo))
		return NULL;

	CFMutableDictionaryRef modAppInfo = CFDictionaryCreateMutableCopy(NULL, 0, applicationInfo);

	if (!CFDictionaryContainsValue(modAppInfo, _kLSArchitectureKey))
	{
#if defined(__x86_64__)
		CFDictionaryAddValue(modAppInfo, _kLSArchitectureKey, _kLSArchitecturex86_64Value);
#elif defined(__i386__)
		CFDictionaryAddValue(modAppInfo, _kLSArchitectureKey, _kLSArchitecturei386Value);
#elif defined(__ppc__)
		CFDictionaryAddValue(modAppInfo, _kLSArchitectureKey, _kLSArchitecturePowerPCValue);
#elif defined(__ppc64__)
		CFDictionaryAddValue(modAppInfo, _kLSArchitectureKey, _kLSArchitecturePowerPC64Value);
#else
		CFDictionaryAddValue(modAppInfo, _kLSArchitectureKey, _kLSArchitectureUnknownValue);
#endif
	}

	if (!CFDictionaryContainsValue(modAppInfo, _kLSExecutablePathKey))
	{
		CFStringRef path = CFStringCreateWithFileSystemRepresentation(NULL, _CFProcessPath());
		CFDictionaryAddValue(modAppInfo, _kLSExecutablePathKey, path);
		CFRelease(path);
	}

	if (!CFDictionaryContainsValue(modAppInfo, _kLSPIDKey))
	{
		// Are we supposed to set this one if not present?
		long pid = getpid();
		CFNumberRef num = CFNumberCreate(NULL, kCFNumberLongType, &pid);
		CFDictionaryAddValue(modAppInfo, _kLSPIDKey, num);
		CFRelease(num);
	}

	if (!CFDictionaryContainsValue(modAppInfo, _kLSDisplayNameKey) &&
		!CFDictionaryContainsValue(modAppInfo, _kCFBundleDisplayNameKey) &&
		!CFDictionaryContainsValue(modAppInfo, kCFBundleNameKey))
	{
		// TODO: Generate app name from bundle path / process path etc.
	}

	if (!CFDictionaryContainsValue(modAppInfo, _kLSCheckInTimeKey))
	{
		CFDateRef now = CFDateCreate(NULL, CFAbsoluteTimeGetCurrent());
		CFDictionaryAddValue(modAppInfo, _kLSCheckInTimeKey, now);
		CFRelease(now);
	}

	if (!CFDictionaryContainsValue(modAppInfo, _kLSApplicationTypeKey))
	{
		if (!g_applicationType)
			g_applicationType = _kLSApplicationForegroundTypeKey;
		CFDictionaryAddValue(modAppInfo, _kLSApplicationTypeKey, g_applicationType);
	}

	xpc_object_t dict = createCommandDictionary(_kLSXPCCheckIn);

	xpc_object_t info = _CFXPCCreateXPCObjectFromCFObject(modAppInfo);
	CFRelease(modAppInfo);

	xpc_dictionary_set_value(dict, "info", info);
	xpc_release(info);

	xpc_dictionary_set_mach_send(dict, "deathport", deathPort());
	xpc_object_t reply = xpc_connection_send_message_with_reply_sync(getLSDConnection(), dict);
	xpc_release(dict);

	if (reply && reply != XPC_ERROR_CONNECTION_INTERRUPTED && reply != XPC_ERROR_CONNECTION_INVALID)
	{
		if (!xpc_dictionary_get_bool(reply, "success"))
		{
			fprintf(stderr, "LaunchServices: check-in failed\n");
			return NULL;
		}

		CFDictionaryRef result = (CFDictionaryRef) _CFXPCCreateCFObjectFromXPCObject(xpc_dictionary_get_value(reply, "result"));
		g_ourASN = CFDictionaryGetValue(result, _kLSASNKey);
		CFRetain(g_ourASN);

		// Is this really what should be returned?
		return result;
	}
	else
	{
		fprintf(stderr, "LaunchServices: check-in operation didn't execute\n");
	}

	return NULL;
}

CFTypeRef _LSGetCurrentApplicationASN(void)
{
	return g_ourASN;
}

OSStatus _LSSetApplicationInformationItem(int sessionID, LSASNRef asn, CFStringRef key, CFStringRef value, CFDictionaryRef *info)
{
	// TODO: This talks to lsd to change the property of *any* app
	return unimpErr;
}

void _LSSetApplicationLaunchServicesServerConnectionStatus(uint64_t flags, LSServerConnectionAllowedBlock block)
{
	// TODO
	if (flags & 2) // Called from ApplicationServices/HIServices - this disables connecting to lsd???
		g_applicationType = _kLSApplicationBackgroundOnlyTypeKey;
	
	if (g_connectionAllowedBlock)
		Block_release(g_connectionAllowedBlock);
	
	g_connectionAllowedBlock = NULL;
	if (block)
		g_connectionAllowedBlock = Block_copy(block);
}

LSASNRef _LSASNCreateWithPid(CFAllocatorRef allocator, pid_t pid)
{
	long lpid = pid;
	return CFNumberCreate(NULL, kCFNumberLongType, &lpid);
}

/*
 * _LSASNCreate -- the LSASN factory behind a synchronous application launch, and the second C
 * CoreServices import Xcode 26.3's iOS plug-in binds and this image did not carry.
 *
 * WHAT IS ESTABLISHED.  The ONLY image in Xcode that imports it is IDEiOSSupportCore (x86_64 slice
 * sha256 3d941fe8f7fec44db944850aebe67d93a3a389559b7794162ea450c33b78fd5e); import entry 40, field
 * ordinal 6 (the CoreServices umbrella), weak false and EAGER, so the symbol must exist for the
 * plug-in to load at all.  Its one call site, 0x51d7f, is reached ONLY from the success branch of
 * _LSOpenApplicationURL (0x51cd8 je 0x51d77) and passes THREE arguments -- a NULL CFAllocatorRef in
 * %rdi and, in %esi/%edx, the two 32-bit words the caller read out of that open's 8-byte out value
 * (0x51d77 movl -0x30(%rbp),%esi ; 0x51d7a movl -0x2c(%rbp),%edx):
 *
 *   51d77: movl  -0x30(%rbp), %esi      ; word0
 *   51d7a: movl  -0x2c(%rbp), %edx      ; word1
 *   51d7d: xorl  %edi, %edi             ; CFAllocatorRef = NULL
 *   51d7f: callq 0x8c5c2                ; _LSASNCreate
 *   51d84: movq  %rax, %r15             ; the ASN the rest of the method uses
 *   51d99: callq 0x8c5c8                ; _LSCopyApplicationInformationItem(-2, asn, _kLSPIDKey)
 *   51db3: callq *objc_msgSend          ; [that, intValue] -> the launched app's pid
 *
 * So the ARITY and the register classes are measured, which is all the loader needs.
 *
 * WHAT IS *NOT* ESTABLISHED, and is therefore not invented.  Apple declares _LSASNCreate in its
 * internal LaunchServices header, which the SDK does not ship (the SDK's LaunchServices.tbd lists
 * the export and nothing more), so the meaning of the two 32-bit words -- whether they are an ASN's
 * own halves, or a pid plus something else, or a ProcessSerialNumber's two halves -- and the CF
 * representation Apple wraps them in are UNKNOWN here.  An ASN built from a guess would be a
 * fabricated record: the caller's next act is to resolve it back to a pid through
 * _LSCopyApplicationInformationItem, which this file answers with NULL because the guest keeps no
 * running-application registry (see _LSCopyMatchingApplicationsWithItems below for the same
 * refusal).  A made-up ASN would therefore be contradicted one call later, exactly as a
 * fabricated "launched = true" would be in _LSOpenURLsWithCompletionHandler.
 *
 * AND IT CANNOT BE REACHED IN THIS GUEST AT ALL: its only caller arrives here after
 * _LSOpenApplicationURL returns noErr, and that entry point answers kLSServerCommunicationErr
 * because there is no LaunchServices server to perform the open.  Absence -- NULL, the answer this
 * file's other running-application queries give -- is the truthful result of a factory whose input
 * cannot be named, and it drives the caller's own failure path instead of a fabricated success.
 */
LSASNRef _LSASNCreate(CFAllocatorRef allocator, uint32_t word0, uint32_t word1)
{
	(void) allocator;
	(void) word0;
	(void) word1;

	return NULL;
}

/*
 * The application that matches a set of application-information items. The items are the running
 * application's own vocabulary -- the same _kLSPIDKey/_kLSExecutablePathKey/... families this file
 * keeps for the check-in path -- so the set being matched is LaunchServices' RUNNING applications,
 * and the only caller in the Xcode load closure passes exactly one pair, {_kLSPIDKey: pid}:
 *
 *   DVTInstrumentsFoundation 0xac40 (4 arguments: session kLSDefaultSessionID, count 1,
 *   keys -> the address of a stack CFStringRef holding _kLSPIDKey, values -> the address of a stack
 *   CFNumberRef holding the pid). It tests the result for NULL at 0xac48, and otherwise accepts only
 *   a one-element array: it takes element 0 (0xac72) to ask _LSCopyApplicationInformation for
 *   _kCFBundleIdentifierKey, and marks the process as an application when that happens; any other
 *   count falls through to "no application" at 0xacf9.
 *
 * THIS GUEST HAS NO SUCH SET, so it cannot answer the query and says so the way this family does:
 * NULL -- what the sibling _LSCopyApplicationInformation below answers as well. Reaching the running
 * set means reaching a check-in registry, and the guest has none: _LSApplicationCheckIn (this file)
 * only records the calling process's OWN ASN in g_ourASN, lsd's scanner registers bundles on disk
 * and no process (launchservicesd/LSBundle.m has no table of running applications, and its message
 * handler replies to everything without looking at a registry), and nothing else in the tree maps a
 * pid to a bundle. Answering from the application DATABASE instead would report a pid match -- and
 * so an "is an application" verdict at the caller's 0xace4 -- for a process that was never
 * registered, which is exactly the fabricated record this must not produce; an empty array would
 * claim the same match set was computed and came out empty. A NULL is also the answer while the
 * database holds nothing at all, which is this prefix's state.
 */
CFArrayRef _LSCopyMatchingApplicationsWithItems(LSSessionID sessionID, CFIndex count,
	const CFStringRef* keys, const CFTypeRef* values)
{
	return NULL;
}

static mach_port_name_t deathPort()
{
	static dispatch_once_t onceDeathPort;
	static mach_port_name_t port;
	dispatch_once(&onceDeathPort, ^{
		kern_return_t kr;
		kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);

		if (kr == KERN_SUCCESS)
			mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND);
	});

	return port;
}

static xpc_connection_t getLSDConnection()
{
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		const char* env = getenv("SCDontUseServer");
		if (env != NULL && atoi(env) != 0)
			return;

		const char* serviceName = "com.apple.coreservices.launchservicesd";
		const char* altName = getenv("__LSXPCSERVICENAME");

		if (altName != NULL)
			serviceName = altName;

		xpc_connection_t master = xpc_connection_create_mach_service(serviceName,
			dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), 0);

		xpc_connection_set_event_handler(master, ^(xpc_object_t peer) {
			if (peer == 0)
			{
				// TODO:
				fprintf(stderr, "LaunchServices: Cannot connect to lsd\n");
				return;
			}
			g_lsdConnection = (xpc_connection_t) xpc_retain(peer);

			xpc_object_t dict = createCommandDictionary(_kLSXPCHello);
			xpc_object_t reply = xpc_connection_send_message_with_reply_sync(g_lsdConnection, dict);
			xpc_release(dict);

			if (reply == XPC_ERROR_CONNECTION_INTERRUPTED || reply == XPC_ERROR_CONNECTION_INVALID)
			{
				fprintf(stderr, "LaunchServices: lsd connection broken\n");
				return;
			}
			if (!xpc_dictionary_get_bool(reply, "success"))
			{
				fprintf(stderr, "LaunchServices: lsd hello failed\n");
				return;
			}
			xpc_object_t result = xpc_dictionary_get_value(reply, "result");
			xpc_object_t ep = xpc_dictionary_get_value(result, "endpoint");

			if (xpc_get_type(ep) == XPC_TYPE_ENDPOINT)
			{
				xpc_connection_t conn = g_endpointConnection = xpc_connection_create_from_endpoint((xpc_endpoint_t) ep);
				xpc_connection_set_target_queue(conn, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
				xpc_connection_set_event_handler(conn, ^(xpc_object_t conn) {
					// TODO?
				});
				xpc_connection_resume(conn);
			}

			xpc_release(reply);
		});
		xpc_connection_resume(master);
	});

	return g_endpointConnection;
}

static xpc_object_t createCommandDictionary(uint64_t command)
{
	xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_int64(dict, "command", command);
	return dict;
}
