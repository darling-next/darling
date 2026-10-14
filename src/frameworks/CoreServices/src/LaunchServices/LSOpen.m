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


#include <LaunchServices/LaunchServices.h>
#include <LaunchServices/LaunchServicesPriv.h>
#include <CoreFoundation/CFError.h>
#include <CarbonCore/MacErrors.h>
#include <stdio.h>

OSStatus
LSOpenURLsWithRole(
  CFArrayRef inURLs,
  LSRolesMask inRole,
  const AEKeyDesc * inAEParam,
  const LSApplicationParameters * inAppParams,
  ProcessSerialNumber * outPSNs,
  CFIndex inMaxPSNCount)
{
	puts("LSOpenURLsWithRole STUB");
	return unimpErr;
}

OSStatus
LSOpenItemsWithRole(
  const FSRef * inItems,
  CFIndex inItemCount,
  LSRolesMask inRole,
  const AEKeyDesc * inAEParam,
  const LSApplicationParameters * inAppParams,
  ProcessSerialNumber * outPSNs,
  CFIndex inMaxPSNCount)
{

	CFURLRef* urls = (CFURLRef*) malloc(inItemCount * sizeof(CFURLRef));

	for (CFIndex i = 0; i < inItemCount; i++)
	{
		char path[4096];

		OSStatus status = FSRefMakePath(&inItems[i], (UInt8*) path, sizeof(path)-1);
		if (status != noErr)
		{
			for (CFIndex j; j < i; j++)
				CFRelease(urls[j]);
			free(urls);
			return status;
		}

		CFStringRef urlStr = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
		urls[i] = CFURLCreateWithFileSystemPath(NULL, urlStr, kCFURLPOSIXPathStyle, FALSE);
		CFRelease(urlStr);
	}

	CFArrayRef urlArray = CFArrayCreate(NULL, (const void**) urls, inItemCount, &kCFTypeArrayCallBacks);
	for (CFIndex i; i < inItemCount; i++)
		CFRelease(urls[i]);
	free(urls);

	OSStatus status = LSOpenURLsWithRole(urlArray, inRole, inAEParam, inAppParams, outPSNs, inMaxPSNCount);

	CFRelease(urlArray);

	return status;
}

OSStatus LSOpenFSRef(const FSRef *inRef, FSRef *outLaunchedRef)
{
	char path[4096];

	OSStatus status = FSRefMakePath(inRef, (UInt8*) path, sizeof(path)-1);
	if (status != noErr)
		return status;

	CFStringRef urlStr = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
	CFURLRef url = CFURLCreateWithFileSystemPath(NULL, urlStr, kCFURLPOSIXPathStyle, FALSE);
	CFRelease(urlStr);
	
	CFURLRef launchedUrl;
	status = LSOpenCFURLRef(url, &launchedUrl);

	CFRelease(url);

	if (status != noErr)
		return status;

	if (outLaunchedRef)
	{
		CFStringRef path = CFURLCopyFileSystemPath(launchedUrl, kCFURLPOSIXPathStyle);
		status = FSPathMakeRef((const UInt8*) CFStringGetCStringPtr(path, kCFStringEncodingUTF8), outLaunchedRef, NULL);
		CFRelease(path);
	}
	
	CFRelease(launchedUrl);
	return status;
}

OSStatus LSOpenCFURLRef(CFURLRef inURL, CFURLRef *outLaunchedURL)
{
	OSStatus ret;
	LSLaunchURLSpec spec;

	memset(&spec, 0, sizeof(spec));

	spec.itemURLs = CFArrayCreate(kCFAllocatorDefault, (const void**)&inURL, 1, NULL);
	ret = LSOpenFromURLSpec(&spec, outLaunchedURL);
	CFRelease(spec.itemURLs);
	
	return ret;
}

/*
 * THE ASYNCHRONOUS OPEN DIRECTION: Apple's private __LSOpenURLsWithCompletionHandler, the SPI the
 * Xcode load closure's XCTHarness binds from the CoreServices field. The macOS SDK names it among
 * the LaunchServices sub-framework's exports (LaunchServices.tbd line 80) and the CoreServices
 * umbrella re-exports exactly that image, so it belongs in this image beside the LSOpen* entry
 * points above -- not in the umbrella binary.
 *
 * WHAT THE ONE MEASURED CALLER PASSES (Xcode 26.3's XCTHarness, x86_64 slice, its
 * LC_DYLD_CHAINED_FIXUPS imports decoded by hand -- llvm-objdump's --dyld-info mislabels these
 * neighbouring slots):
 *
 *   import 224: {name: __LSOpenURLsWithCompletionHandler, lib_ordinal: 3, weak: false, eager: true}
 *   ordinal 3 = /System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices,
 *   so the demand is STRONG and EAGER (this is a hard stop for the loader).
 *   ONE call site, 0x152fb9 inside XCTHLocalAppLauncher_macOS.doTheLaunch(), reached through the
 *   symbol stub at 0x16dce2 (the image's indirect-symbol table puts the GOT slot at 0x1ce888):
 *
 *     152fa5: movq -0x30(%rbp), %r14  ; arg 1: the NSArray of URLs to open -- built at 0x152d95
 *                                     ;   from launchParameters.openURL, or the empty Swift array
 *                                     ;   bridged at 0x152c89 when the launch has none
 *     152fa9: movq %r14, %rdi
 *     152fac: movq %r15, %rsi         ; arg 2: the NSURL from launchParameters.launchableFileURL,
 *                                     ;   bridged at 0x152dce (the application to launch)
 *     152faf: movq -0x60(%rbp), %r12
 *     152fb3: movq %r12, %rdx         ; arg 3: the CFDictionaryRef buildLSOptions(fifoOutputURL:)
 *                                     ;   returned at 0x152b45 -- its keys are the __kLSOpenOption*Key
 *                                     ;   vocabulary, and the log line just above the call reads
 *                                     ;   "Launching application at path %{public}@ with options: %{public}@"
 *     152fb6: movq %rbx, %rcx         ; arg 4: a Block_copy'd block, Block_release'd right after the
 *                                     ;   call (0x152fd4) -- the completion handler
 *     152fb9: callq 0x16dce2          ; __LSOpenURLsWithCompletionHandler
 *     152fbe: ...                     ; the RESULT (%rax) IS NOT READ; the call falls through to
 *                                     ;   releasing the objects it was given
 *
 * THE COMPLETION HANDLER'S CONTRACT, measured from the block itself. The block's invoke (0x15352d)
 * is
 *   $sSo8LSASNRefaSgSbSo10CFErrorRefaSgIeggyg_AC6Darwin0D7BooleanVAFIeyByyy_TR
 *   = "thunk for @escaping @callee_guaranteed (LSASNRef?, Bool, CFErrorRef?) -> ()"
 * and it maps the block call's registers (rsi, edx, rcx) onto that closure's parameters, so the
 * C-level signature this image must satisfy is
 *   void (^handler)(LSASNRef asn, Boolean launched, CFErrorRef error);
 * The closure is doTheLaunch's own continuation channel. It tests
 *   asn != NULL && launched          (0x15301e..0x153031)
 * before its SUCCESS path, which reads the launched application's pid with
 * _LSCopyApplicationInformationItem(kLSDefaultSessionID, asn, _kLSPIDKey), starts the launch session
 * with that pid and observes the process's termination. Its FAILURE path (0x153244) logs
 *   "Failed to launch application at path %{public}@: %{public}@"
 * with the error and RESUME-THROWS it into the same continuation (0x1533d4..0x1533fc), so the launch
 * request fails with a real NSError instead of hanging.
 *
 * THEREFORE THE HANDLER IS ALWAYS CALLED. Nothing else resumes that continuation: an implementation
 * that answered by return value alone, or that stored the handler and never invoked it, would leave
 * doTheLaunch unable to complete -- the caller would HANG where it today fails. Delivering the
 * failure through the handler is what keeps the caller's own failure path running, and it is called
 * synchronously because there is nothing to wait for in this guest (see below) -- the caller copies
 * the block before the call and releases it after, so a synchronous call is exactly the lifetime it
 * expects.
 *
 * WHY THE FAILURE, AND WHY IT IS THIS FAMILY'S OWN. Opening URLs is LaunchServices' operation, and
 * this guest has no LaunchServices to perform it:
 *   * launchservicesd is not deployed in the prefix and there is no /private/var/db/launchservices.db
 *     (measured by the registration rung), so there is no server process to ask and nothing that
 *     assigns an ASN;
 *   * the running-application registry does not exist and LSRunning.m refuses to fabricate one
 *     ("exactly the fabricated record this must not produce"), and _LSCopyApplicationInformationItem
 *     (LaunchServices.cpp) answers NULL -- so a "launched = true" plus an ASN would be contradicted
 *     by the caller's VERY NEXT call, which would then take its failure path anyway, having launched
 *     something it was told it could use;
 *   * the tree's fork/exec path (_LSLaunchApplication) honours none of the options the caller
 *     passes -- not the stdin/stdout/stderr paths, not the arguments, not the environment -- so it
 *     could not honestly stand in for the open either.
 * The honest answer is therefore Apple's own code for "the LaunchServices server cannot be
 * reached", kLSServerCommunicationErr, delivered as (asn = NULL, launched = false, error = that
 * status in kCFErrorDomainOSStatus -- the shape LSInfo.m uses for its own failures). That is a
 * documented LaunchServices failure, and it is the answer the caller's failure path already
 * handles.
 *
 * THE FOUR ARGUMENTS ARE ACCEPTED AND NOT INTERPRETED: the operation cannot be served whatever they
 * say, so none is validated into a different answer and the options dictionary is not translated
 * (its __kLSOpenOption*Key vocabulary is defined in constants.c for the callers that build it).
 * Equally, this does not touch LSOpenURLsWithRole above: that entry point is a different name the
 * loader has not stopped on.
 */
OSStatus
_LSOpenURLsWithCompletionHandler(
	CFArrayRef inURLs,
	CFURLRef inApplicationURL,
	CFDictionaryRef inOptions,
	LSOpenURLsCompletionHandler inCompletionHandler)
{
	(void) inURLs;
	(void) inApplicationURL;
	(void) inOptions;

	OSStatus status = kLSServerCommunicationErr;

	if (inCompletionHandler != NULL)
	{
		CFErrorRef error = CFErrorCreate(NULL, kCFErrorDomainOSStatus, status, NULL);

		/* NULL asn and launched = 0: no application was launched, so no application can be named
		 * and the caller's success path must not be taken. `0` (not a Boolean variable) keeps the
		 * whole register zeroed for the block's invoke, which tests it 32 bits wide. */
		inCompletionHandler(NULL, 0, error);

		CFRelease(error);
	}

	return status;
}

/*
 * THE SYNCHRONOUS SINGLE-APPLICATION OPEN: Apple's private _LSOpenApplicationURL, the one C
 * CoreServices import Xcode 26.3's iOS plug-in binds that this image did not carry. It belongs
 * beside LSOpenURLsWithCompletionHandler above and the deprecated public LSOpenApplication below:
 * it is the same operation with the application named by URL instead of by parameters alone, and
 * this file's siblings already end in a ProcessSerialNumber * out-parameter (LSOpenURLsWithRole,
 * LSOpenApplication).
 *
 * THE ONE MEASURED CALLER (Xcode 26.3's IDEiOSSupportCore, x86_64 slice sha256
 * 3d941fe8f7fec44db944850aebe67d93a3a389559b7794162ea450c33b78fd5e): its import entry 43 binds this
 * name from dependency field ordinal 6 (/System/Library/Frameworks/CoreServices.framework/Versions/A/
 * CoreServices -- the umbrella this sub-framework is re-exported through), weak false, eager true,
 * i.e. a HARD STOP for the loader.  ONE call site, 0x51cba inside
 * -[DVTSimulatorApp launchWithError:andReadyCallback:], reached through the symbol stub at 0x8c5d4:
 *
 *   51c34: movl   $0x10200, -0x78(%rbp)     ; the caller's own parameter block is built first
 *   ...    (the block spans ~0x34 bytes, a pointer to the app record at +0x14, zeroed elsewhere)
 *   51c54: movq   <lazy-init>, %rax ; 0x14(%r12)
 *   51c78: movq   $0, -0x30(%rbp)           ; the 8-byte out value, zeroed
 *   51c7c: movq   <selref>, %rsi ; objc_msgSend
 *   51c8a: movq   %r15, %rdi                ; self (DVTSimulatorApp)
 *   51c8d: callq  *objc_msgSend             ; -[DVTSimulatorApp ...] -> app record
 *   51c9a: movq   <selref>, %rsi
 *   51ca4: callq  *objc_msgSend             ; app URL
 *   51cb1: movq   %rax, %rdi                ; arg 1: the application's CFURLRef
 *   51cb4: movq   %r12, %rsi                ; arg 2: the parameter block at -0x78(%rbp)
 *   51cb7: movq   %r13, %rdx                ; arg 3: &the 8-byte out value
 *   51cba: callq  0x8c5d4                   ; _LSOpenApplicationURL
 *   51cbf: movl   %eax, %r12d               ; the RESULT IS READ AS AN OSStatus
 *   51cd5: testl  %r12d, %r12d
 *   51cd8: je     0x51d77                   ; status == 0 -> the SUCCESS path below
 *   51cde: ...                              ; status != 0 -> *outError = [NSError
 *                                           ;   errorWithDomain:_NSOSStatusErrorDomain code:status]
 *                                           ;   and the method returns -1
 *   51d77: movl   -0x30(%rbp), %esi         ; the SUCCESS path reads the out value as two 32-bit
 *   51d7a: movl   -0x2c(%rbp), %edx         ;   words and calls _LSASNCreate(NULL, word0, word1)
 *   51d7d: xorl   %edi, %edi
 *   51d7f: callq  0x8c5c2                   ; _LSASNCreate
 *   51d99: callq  0x8c5c8                   ; _LSCopyApplicationInformationItem(-2, asn, _kLSPIDKey)
 *
 * So the C-level shape is (CFURLRef, <params>, <8-byte out>) -> OSStatus, and the caller's FAILURE
 * path is already written: any non-zero status becomes an NSError in *outError and the method
 * returns -1.
 *
 * WHY THE FAILURE, AND WHY IT IS THIS FAMILY'S OWN.  The same three measurements that decided
 * _LSOpenURLsWithCompletionHandler above apply to the synchronous open: launchservicesd is not
 * deployed and there is no /private/var/db/launchservices.db, the running-application registry does
 * not exist (LSRunning.m refuses to fabricate one), and the tree's fork/exec launcher
 * (_LSLaunchApplication) honours none of the options a caller of this family passes.  Apple's own
 * code for "the LaunchServices server cannot be reached" is kLSServerCommunicationErr, which is
 * exactly what the caller's failure path already handles -- so this is that status, and the out
 * value is left untouched (only the success path reads it).
 *
 * THE ARGUMENTS ARE ACCEPTED AND NOT INTERPRETED: the operation cannot be served whatever they say,
 * so the parameter block is not decoded field by field and nothing is validated into a different
 * answer.
 */
OSStatus
_LSOpenApplicationURL(
	CFURLRef inApplicationURL,
	const LSApplicationParameters *inAppParams,
	ProcessSerialNumber *outPSN)
{
	(void) inApplicationURL;
	(void) inAppParams;
	(void) outPSN;

	return kLSServerCommunicationErr;
}

/*
OSStatus LSOpenApplication(const LSApplicationParameters *appParams, ProcessSerialNumber *outPSN)
{

}
*/

/*
OSStatus
LSOpenFromURLSpec(
  const LSLaunchURLSpec * inLaunchSpec,
  _Nullable CFURLRef *_Nullable outLaunchedURL)
{
}
*/
