#ifndef LAUNCH_SERVICES_PRIV_H_
#define LAUNCH_SERVICES_PRIV_H_
#include <LaunchServices/LaunchServices.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFString.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef CFTypeRef LSASNRef;
typedef int LSSessionID;
typedef int LSNotificationCode;
typedef int LSNotificationID;
typedef OptionBits LSInitializeFlags;

enum
{
	kLSDefaultSessionID = -2,
};

// Used in Security
enum
{
	kLSNotificationInvalidID = -1,
	kLSNotifyApplicationDeath = 1,
};

enum
{
	/***** MASTER SERVICE COMMANDS *****/
	// IN:
	// clientversion, session
	// OUT:
	// serverversion, endpoint
	_kLSXPCHello = 0x64,

	/***** ENDPOINT COMMANDS *****/
	// OUT:
	// bool success
	// dictionary result
	_kLSXPCWeirdCheckIn = 0x1F9,
	// IN:
	// dictionary info
	// mach_port_name_t deathport
	// OUT:
	// bool success
	// dictionary result (appInfo)
	_kLSXPCCheckIn = 0x1F4,
};

extern const CFStringRef _kLSArchitecturePowerPCValue;
extern const CFStringRef _kLSArchitecturePowerPC64Value;
extern const CFStringRef _kLSArchitecturei386Value;
extern const CFStringRef _kLSArchitecturex86_64Value;
extern const CFStringRef _kLSArchitectureUnknownValue;

extern const CFStringRef _kLSArchitectureKey;

extern const CFStringRef _kLSPIDKey;
extern const CFStringRef _kLSExecutablePathKey;
extern const CFStringRef _kLSCheckInTimeKey;
extern const CFStringRef _kLSDisplayNameKey;
extern const CFStringRef _kLSArchitectureKey;
extern const CFStringRef _kLSBundlePathKey;
extern const CFStringRef _kLSApplicationTypeKey;

extern const CFStringRef _kLSASNKey;

extern const CFStringRef _kLSApplicationForegroundTypeKey;
extern const CFStringRef _kLSApplicationBackgroundOnlyTypeKey;

extern const CFStringRef _kLSExecutableFormatKey;
extern const CFStringRef _kLSExecutableFormatMachOKey;
extern const CFStringRef _kLSExecutableFormatCFMKey;

/*
 * THE LSOpen OPTIONS VOCABULARY: the keys of the options dictionary the asynchronous open takes.
 * The macOS SDK names all of them as exports of the LaunchServices sub-framework
 * (LaunchServices.tbd lines 142-167 list every __kLSOpenOption*Key -- the tbd names are the Mach-O
 * symbols, one underscore longer than the C names below, as _kLSPIDKey/__kLSPIDKey shows) and the
 * CoreServices umbrella re-exports that image, which is the field the closure's images bind them from. They
 * are DATA: each is a `const CFStringRef` VARIABLE (not a CFSTR inlined into every caller), which
 * is how every client reads them -- e.g. XCTHarness's buildLSOptions loads the ADDRESS of
 * _kLSOpenOptionArgumentsKey and dereferences it to use the value as a dictionary key.
 *
 * THE VALUES ARE THIS TREE'S OWN, and the reason is measured: the SDK ships no header for these
 * SPIs and no .tbd carries a value -- the only occurrences of the names anywhere in the Xcode 26.3
 * tree are symbol tables and those export lists, and the strings they stand for appear nowhere. The
 * strings below therefore follow the one convention this file's data already uses: the name without
 * its leading underscore(s) and without the trailing "Key" (_kLSPIDKey is CFSTR("LSPID"),
 * _kLSExecutablePathKey is CFSTR("LSExecutablePath"), _kLSArchitectureKey is
 * CFSTR("LSArchitecture")). A key's VALUE is observable only to a client that compares it against a
 * literal instead of binding the symbol, and no image of the load closure does that -- every one of
 * them binds the names below -- while the dictionary the caller builds is handed to this image's
 * own _LSOpenURLsWithCompletionHandler, which would read it back with these same constants.
 */
extern const CFStringRef _kLSOpenOptionActivateKey;
extern const CFStringRef _kLSOpenOptionArchitectureKey;
extern const CFStringRef _kLSOpenOptionArgumentsKey;
extern const CFStringRef _kLSOpenOptionEnvironmentVariablesKey;
extern const CFStringRef _kLSOpenOptionIncludeCurrentEnvironmentValuesKey;
extern const CFStringRef _kLSOpenOptionLaunchStdErrPathKey;
extern const CFStringRef _kLSOpenOptionLaunchStdInPathKey;
extern const CFStringRef _kLSOpenOptionLaunchStdOutPathKey;
extern const CFStringRef _kLSOpenOptionPreferRunningInstanceKey;
extern const CFStringRef _kLSOpenOptionStopProcessKey;
extern const CFStringRef _kLSOpenOptionWaitForApplicationToCheckInKey;

// Example call:
// _LSApplicationCheckIn(kLSDefaultSessionID, CFBundleGetInfoDictionary(CFBundleGetMainBundle()))
// Normally to be called from -[NSApplication init]
CFDictionaryRef _LSApplicationCheckIn(LSSessionID sessionID, CFDictionaryRef applicationInfo);

CFTypeRef _LSGetCurrentApplicationASN(void);
OSStatus _LSSetApplicationInformationItem(LSSessionID sessionID, LSASNRef asn, CFStringRef key, CFStringRef value, CFDictionaryRef *info);

typedef bool (^LSServerConnectionAllowedBlock) ( CFDictionaryRef optionsRef );
void _LSSetApplicationLaunchServicesServerConnectionStatus(uint64_t flags, LSServerConnectionAllowedBlock block);

CFTypeRef _LSCopyApplicationInformationItem(LSSessionID sessionID, CFTypeRef asn, CFStringRef what);

CFDictionaryRef _LSCopyApplicationInformation(LSSessionID sessionID, CFTypeRef asn, int);

LSASNRef _LSASNCreateWithPid(CFAllocatorRef allocator, pid_t pid);

// The running applications whose application-information items (the _kLS*Key families above) match
// `count` key/value pairs. NULL when there are none to match against: this guest registers no
// running application, as LSRunning.m explains.
CFArrayRef _LSCopyMatchingApplicationsWithItems(LSSessionID sessionID, CFIndex count, const CFStringRef *keys, const CFTypeRef *values);

OSStatus _LSLaunchApplication(CFURLRef appPath);

// The registration direction: register the bundle the URL names into lsd's LaunchServices database
// (/private/var/db/launchservices.db) by running the same registration code lsd's scan runs
// (LSBundleRegistration.m). The options word is the caller's -- the one measured caller,
// DVTFoundation, passes 2 -- and this tree's registration does not interpret it, because its one
// behaviour (insert, or update when the Info.plist changed) is lsd's own test. Answers noErr, or
// paramErr / fnfErr / kLSNotAnApplicationErr / kLSUnknownErr; see LSRegister.m.
OSStatus _LSRegisterURLWithOptions(CFURLRef inURL, UInt32 inOptions);

// The completion handler of the asynchronous open. Apple's private signature, measured from the one
// caller in the load closure: XCTHarness's doTheLaunch passes a block whose invoke is the
// reabstraction thunk for (LSASNRef?, Bool, CFErrorRef?) -> () -- asn and `launched` are what its
// success path tests, the error is what its failure path turns into an NSError.
typedef void (^LSOpenURLsCompletionHandler)(LSASNRef asn, Boolean launched, CFErrorRef error);

// The asynchronous open direction: open `inURLs` with the application at `inApplicationURL`,
// according to `inOptions` (the _kLSOpenOption*Key vocabulary above), and report the outcome to
// `inCompletionHandler` -- which is the ONLY completion channel the caller has and is therefore
// always called. This guest has no LaunchServices server to ask, so the answer is always the
// family's own kLSServerCommunicationErr, delivered as (NULL, false, that error); see LSOpen.m for
// the measured call site and why no other answer is honest here.
OSStatus _LSOpenURLsWithCompletionHandler(CFArrayRef inURLs, CFURLRef inApplicationURL,
	CFDictionaryRef inOptions, LSOpenURLsCompletionHandler inCompletionHandler);

// Assumed
#define kLSDownloadRiskCategoryKey CFSTR("LSDownloadRiskCategory")

#define kLSRiskCategorySafe "LSRiskCategorySafe"
#define kLSRiskCategoryNeutral "LSRiskCategoryNeutral"
#define kLSRiskCategoryUnknown "LSRiskCategoryUnknown"
#define kLSRiskCategoryMayContainUnsafeExecutable "LSRiskCategoryMayContainUnsafeExecutable"

#ifdef __cplusplus
}
#endif

#endif
