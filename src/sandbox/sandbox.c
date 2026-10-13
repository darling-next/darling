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

/*
 * libsystem_containermanager's query SPI -- the ten names XCTHarness binds eagerly from the libSystem
 * field. The SDK fixes them: MacOSX.sdk/usr/lib/system/libsystem_containermanager.tbd exports
 * _container_query_create, _container_query_free, _container_query_get_last_error,
 * _container_query_get_single_result, _container_query_operation_set_flags, _container_query_set_class,
 * _container_query_set_identifiers, _container_query_set_persona_unique_string,
 * _container_error_copy_unlocalized_description and the data symbol _CONTAINER_PERSONA_PRIMARY, and
 * MacOSX.sdk/usr/lib/libSystem.B.tbd's reexported-libraries list names
 * /usr/lib/system/libsystem_containermanager.dylib -- Apple's owner of the family.
 *
 * THIS TREE NEVER BUILDS THAT OWNER. Apple's own sub-library list names it (optionallibs:
 * "system_containermanager") and init.c carries the family's init hook behind
 * HAVE_SYSTEM_CONTAINERMANAGER, but no source for it exists anywhere in the checkout (a grep of the
 * whole tree finds no definition of any of the ten names). So the family has no owner here, and it
 * goes beside the container code this tree does have: the sandbox. Apple's libsystem_sandbox.tbd owns
 * the container-PATH family (_container_path_for_pid, _container_path_for_audit_token, ...) and this
 * file already answers sandbox_container_path_for_pid; the query family is the same container domain,
 * split into a dylib Darling never built. That is the route libplatform took for
 * __os_feature_enabled_impl, whose Apple owner (libsystem_featureflags) is another optionallib this
 * tree never builds.
 *
 * THE OPERATION CANNOT BE SERVED, SO IT FAILS THE FAMILY'S OWN WAY. containermanagerd -- the daemon
 * the man page describes, working in concert with sandbox(7) and secinitd(8) -- does not exist in this
 * guest, and neither does a container database (the prefix has no ~/Library/Containers). Apple's
 * container_query_create is a LOCAL allocation that does not talk to the daemon, so the honest shape is
 * a real query that fails where the family fails: get_single_result returns NULL and records an error,
 * get_last_error returns that error, and container_error_copy_unlocalized_description copies its text.
 * A NULL create would be a different statement (an allocation failure) that a client checking the
 * setters would misread, and a fabricated container_t was refused outright: the clients read a path out
 * of the result and PreviewsDeveloperTools additionally consumes a sandbox token from it
 * (container_copy_sandbox_token -> sandbox_extension_consume), so a made-up container would claim a
 * data container and a grant this guest cannot back.
 *
 * MEASURED CLIENT USE -- the only two images in the Xcode tree that reference the family; XCTHarness is
 * the only one either workload's load closure reaches (PreviewsDeveloperTools is not reached).
 *   XCTHarness, x86_64, in XCTHLocalDevice.dataContainerURL(for:bundleID:):
 *     0x1572ba create -> testq/je 0x157343: NULL skips the container lookup and copies files anyway,
 *              with NO error (the graceful path);
 *     0x1572d5 set_class(query, 2); 0x153bd8 set_identifiers(query, xpc_string(bundleID));
 *     0x1572f8 operation_set_flags(query, 0x800000000);
 *     0x15730a set_persona_unique_string(query, CONTAINER_PERSONA_PRIMARY) -- every setter's result
 *              IGNORED;
 *     0x157312 get_single_result; NULL -> 0x157379 get_last_error ->
 *              container_error_copy_unlocalized_description, and when both are non-NULL the copied text
 *              IS the thrown error's message (a NULL description falls back to bundleID +
 *              "/Library/Logs/DiagnosticReports");
 *     0x157333 / 0x157431 free(query) on every path that created one.
 *   PreviewsDeveloperTools, x86_64: create -> NULL is "CouldNotGetAppContainerError"; then the same
 *   set/get/free sequence (plus operation_set_platform, a name this entry's closure does not demand).
 * Both clients load CONTAINER_PERSONA_PRIMARY's VALUE and pass it straight into
 * set_persona_unique_string (the bind is a __got slot holding the symbol's ADDRESS, then one load of
 * the value), so the symbol must be a char * whose value is the primary persona's unique string.
 *
 * WHAT IS NOT ANSWERED. The value of CONTAINER_PERSONA_PRIMARY is not observable from any artifact in
 * this workspace -- no header in any SDK ships it and no provider binary exists (the tbd carries names
 * only) -- so it is not taken from memory as Apple's literal. It is the family's own token for the
 * primary persona ("primary"; the sibling _CONTAINER_PERSONA_CURRENT names the other sentinel), it is a
 * valid C string so no client can fault on it, and it is INERT at every measured call site: both
 * clients only pass it into a query that cannot be served. NULL was rejected as the value, because
 * Apple's own clients intend a specific persona and NULL would silently change the request to the
 * default one. The identifiers of set_identifiers are likewise not retained: the query can never
 * execute, so it holds no reference to the caller's xpc object (which the caller releases immediately
 * after the call); the setter answers true because the parameter is a valid identifier list, and the
 * failure the client observes is the query's own, at get_single_result.
 */
struct container_error_s {
	char *description; /* malloc'd copy, owned by the error */
};

struct container_query_s {
	uint64_t cls;
	uint64_t flags;
	char *persona_unique_string; /* malloc'd copy, owned by the query */
	bool has_error;
	struct container_error_s error;
};

char *CONTAINER_PERSONA_PRIMARY = "primary";

container_query_t container_query_create(void)
{
	/* A local allocation, as Apple's own create is: it does not talk to the daemon. */
	return calloc(1, sizeof(struct container_query_s));
}

void container_query_free(container_query_t query)
{
	if (query == NULL) {
		return;
	}
	free(query->persona_unique_string);
	free(query->error.description);
	free(query);
}

bool container_query_set_class(container_query_t query, uint64_t cls)
{
	if (query == NULL) {
		return false;
	}
	query->cls = cls;
	return true;
}

bool container_query_set_identifiers(container_query_t query, void *identifiers)
{
	if (query == NULL) {
		return false;
	}
	/* Not retained: the query can never execute, so it holds no reference to the caller's object
	 * (see the block comment above). */
	(void) identifiers;
	return true;
}

bool container_query_operation_set_flags(container_query_t query, uint64_t flags)
{
	if (query == NULL) {
		return false;
	}
	query->flags = flags;
	return true;
}

bool container_query_set_persona_unique_string(container_query_t query, const char *persona_unique_string)
{
	char *copy;

	if (query == NULL) {
		return false;
	}
	copy = persona_unique_string ? strdup(persona_unique_string) : NULL;
	if (persona_unique_string != NULL && copy == NULL) {
		return false;
	}
	free(query->persona_unique_string);
	query->persona_unique_string = copy;
	return true;
}

container_t container_query_get_single_result(container_query_t query)
{
	if (query == NULL) {
		return NULL;
	}
	query->has_error = true;
	free(query->error.description);
	query->error.description = strdup(
		"the container manager is not available in this guest: no container manager daemon "
		"and no container database exist, so no container can be returned");
	return NULL;
}

container_error_t container_query_get_last_error(container_query_t query)
{
	if (query == NULL || !query->has_error) {
		return NULL;
	}
	return &query->error;
}

char *container_error_copy_unlocalized_description(container_error_t error)
{
	if (error == NULL || error->description == NULL) {
		return NULL;
	}
	return strdup(error->description);
}