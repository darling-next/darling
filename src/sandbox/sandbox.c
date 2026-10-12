#include "sandbox.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>

// DUMMY implementation
const enum sandbox_filter_type SANDBOX_CHECK_NO_REPORT = SANDBOX_FILTER_NONE;

int sandbox_init(const char *profile, uint64_t flags, char **errorbuf)
{
	*errorbuf = strdup("Not implemented");
	return 0;
}

const char kSBXProfileNoInternet[] = "no_internet";

const char kSBXProfileNoNetwork[] = "no_network";

const char kSBXProfileNoWrite[] = "no_write";

const char kSBXProfileNoWriteExceptTemporary[] = "no_write_except_temporary";

const char kSBXProfilePureComputation[] = "pure_computation";

const char APP_SANDBOX_IOKIT_CLIENT[] = "com.apple.app-sandbox.iokit-client";
const char APP_SANDBOX_MACH[] = "com.apple.app-sandbox.mach";
const char APP_SANDBOX_READ[] = "com.apple.app-sandbox.read";
const char APP_SANDBOX_READ_WRITE[] = "com.apple.app-sandbox.read-write";
const char IOS_SANDBOX_APPLICATION_GROUP[] = "com.apple.sandbox.application-group";
const char IOS_SANDBOX_CONTAINER[] = "com.apple.sandbox.container";

// Assuming these are 64-bit only because at least one has a value
// that can't be stored in a 32-bit integer.
#ifdef __LP64__

const uint64_t SANDBOX_EXTENSION_CANONICAL = 0x0000001000000002;
const uint64_t SANDBOX_EXTENSION_DEFAULT = 0x0000000400000000;
const uint64_t SANDBOX_EXTENSION_MAGIC = 0x0000000000000001;
const uint64_t SANDBOX_EXTENSION_NO_REPORT = 0x0000000100000010;
const uint64_t SANDBOX_EXTENSION_PREFIXMATCH = 0x0000000200000004;
const uint64_t SANDBOX_EXTENSION_UNRESOLVED = 0x0000000100000001;

#endif

void sandbox_free_error(char *errorbuf)
{
	free(errorbuf);
}

int sandbox_init_with_parameters(const char *profile, uint64_t flags, const char *const parameters[], char **errorbuf)
{
	*errorbuf = strdup("Not implemented");
	return 0;
}

int sandbox_init_with_extensions(const char *profile, uint64_t flags, const char *const extensions[], char **errorbuf)
{
	*errorbuf = strdup("Not implemented");
	return 0;
}

int sandbox_check(pid_t pid, const char *operation, enum sandbox_filter_type type, ...)
{
	return 0;
}

int sandbox_note(const char *note)
{
	printf("%s\n", note);
	return 0;
}

int sandbox_suspend(pid_t pid)
{
	return -1;
}

int sandbox_unsuspend(void)
{
	return -1;
}

int sandbox_issue_extension(const char *path, char **ext_token)
{
	return -1;
}

int sandbox_issue_fs_extension(const char *path, uint64_t flags, char **ext_token)
{
	return -1;
}

int sandbox_issue_fs_rw_extension(const char *path, char **ext_token)
{
	return -1;
}

int sandbox_issue_mach_extension(const char *name, char **ext_token)
{
	return -1;
}

/*
 * Apple exports this from libsystem_sandbox.dylib (see the SDK's
 * libsystem_sandbox.tbd). The demanding client is DVTInstrumentsFoundation, which
 * calls it as sandbox_extension_issue_file_to_process(APP_SANDBOX_READ, path,
 * SANDBOX_EXTENSION_DEFAULT, audit_token) and treats a NULL result as failure,
 * naming the path in an NSError; on success it owns the returned C string and
 * frees it. Darling has no sandbox policy engine, so no extension token can be
 * issued for any path or any process: a fabricated token would claim a sandbox
 * grant this guest cannot back. NULL with errno set is Apple's documented
 * failure contract and the truthful answer here. The caller does not read errno,
 * so ENOTSUP is for humans, not for it.
 */
char *sandbox_extension_issue_file_to_process(const char *extension_class, const char *path, uint32_t flags, audit_token_t token)
{
	(void) extension_class;
	(void) path;
	(void) flags;
	(void) token;
	errno = ENOTSUP;
	return NULL;
}

/*
 * The rest of libsystem_sandbox's extension SPI (the SDK's libsystem_sandbox.tbd names all three in
 * one group: _sandbox_extension_consume, _sandbox_extension_issue_file, ..., _sandbox_extension_release).
 *
 * An extension token is a grant the kernel's policy engine made and recorded; CONSUMING the token is
 * what turns the grant into a handle the process may use, and RELEASING the handle gives it back.
 * Darling has no policy engine and no extension table: nothing here can issue a token (see the sibling
 * above), nothing can validate one, and no handle can name anything. All three therefore answer with
 * Apple's documented failure, and the failure is load bearing rather than decoration: a fabricated
 * handle would tell the client it holds a grant this guest cannot enforce.
 *
 * Measured clients (Xcode 26.3, this platform's XCTDaemonControl/XCTestCore/IDEFoundation slice):
 *   consume    XCTDaemonControl's +[XCTDaemonControlSession requestSessionWithDTXConnectionProvider:
 *              logDelegate:requestDaemonDataContainerSandboxExtension:completion:] _block_invoke_3 calls
 *              sandbox_extension_consume([token UTF8String]) and stores the result UNCHECKED with
 *              -setDaemonSandboxExtensionToken: (it is released in -dealloc, return ignored), so -1
 *              simply becomes the session's handle value. XCTestCore's -[XCTMetricDiagnosticHelper
 *              attachTraceFileCollectedForInstrument:] compares the result with -1 and, on equality,
 *              calls [[NSAssertionHandler currentHandler] handleFailureInMethod:object:
 *              file:@"XCTMetricDiagnosticHelper.m" lineNumber:203 description:@"Failed to access
 *              performance trace file on target device. Unable to attach trace to test report."] --
 *              a failed consume is an unrecoverable condition there, which is why the answer below is
 *              the documented failure and not a value that would let the client attach an attachment
 *              for a grant that does not exist.
 *   release    -[XCTDaemonControlSession dealloc] and the XCTMetricDiagnosticHelper path above; both
 *              ignore the result. Since no consume can succeed, no handle is ever valid, and -1 says
 *              that; 0 would report a release that never happened.
 *   issue_file IDEFoundation's _IDEExtensionIssueSandboxExtensionForPath reads errno through __error()
 *              on NULL and hands it to the caller as an NSPOSIXErrorDomain error; on non-NULL it wraps
 *              the C string in an NSString and frees it. The tree's own caller,
 *              src/external/security/OSX/libsecurity_keychain/xpc/main.c (line 223), also treats NULL as
 *              "can't get sandbox fs extension". ENOTSUP therefore reaches those callers honestly.
 */
int64_t sandbox_extension_consume(const char *extension_token)
{
	(void) extension_token;
	errno = ENOTSUP;
	return -1;
}

int sandbox_extension_release(int64_t extension_handle)
{
	(void) extension_handle;
	errno = ENOTSUP;
	return -1;
}

char *sandbox_extension_issue_file(const char *extension_class, const char *path, uint32_t flags)
{
	(void) extension_class;
	(void) path;
	(void) flags;
	errno = ENOTSUP;
	return NULL;
}

int sandbox_consume_extension(const char *path, const char *ext_token)
{
	return -1;
}

int sandbox_consume_fs_extension(const char *ext_token, char **path)
{
	return -1;
}

int sandbox_consume_mach_extension(const char *ext_token, char **name)
{
	return -1;
}

int sandbox_release_fs_extension(const char *ext_token)
{
	return -1;
}

int sandbox_container_path_for_pid(pid_t pid, char *buffer, size_t bufsize)
{
	return -1;
}

int sandbox_wakeup_daemon(char **errorbuf)
{
	*errorbuf = strdup("Not implemented");
	return -1;
}

const char *_amkrtemp(const char *path)
{
	size_t len = strlen(path);
	const char suffix[] = ".amkrtempXXXXXX";
	char *template = malloc(len + sizeof(suffix));
	memcpy(template, path, len);
	memcpy(template + len, suffix, sizeof(suffix));
	return mktemp(template);
}

int rootless_allows_task_for_pid(pid_t pid) {
	return 1;
}

int sandbox_check_by_audit_token(audit_token_t tok, const char* operation, enum sandbox_filter_type filt, ...) {
	// technically unimplemented
	// just return 0 as an indiciation that whatever operation the caller asked about is allowed
	return 0;
};

int rootless_check_trusted(const char* path) {
	// also unimplemented
	// Darling doesn't have rootless anyways so just return a value indicating trust
	return 0;
};

int rootless_restricted_environment() {
	// also unimplemented
	// Darling doesn't have rootless anyways, so just return a value indicating an unrestricted environment
	return 0;
};

int rootless_check_datavault_flag(const char* path, const char* storage_class) {
	// also unimplemented
	// not quite sure what a "data vault" is here, but its usage in `JSScript.mm` in JavaScriptCore seems to indicate that we should return `0`
	return 0;
};

int sandbox_query_approval_policy_for_path(const char* query, const char* path, char **approval) {
	return -1;
}