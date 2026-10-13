/*
 * Copyright (c) 2006-2010 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
#ifndef _SANDBOX_H_
#define _SANDBOX_H_

#include <sys/cdefs.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <mach/message.h>

__BEGIN_DECLS
/*
 * @function sandbox_init
 * Places the current process in a sandbox with a profile as
 * specified.  If the process is already in a sandbox, the new profile
 * is ignored and sandbox_init() returns an error.
 *
 * @param profile (input)   The Sandbox profile to be used.  The format
 * and meaning of this parameter is modified by the `flags' parameter.
 *
 * @param flags (input)   Must be SANDBOX_NAMED.  All other
 * values are reserved.
 *
 * @param errorbuf (output)   In the event of an error, sandbox_init
 * will set `*errorbuf' to a pointer to a NUL-terminated string
 * describing the error. This string may contain embedded newlines.
 * This error information is suitable for developers and is not
 * intended for end users.
 *
 * If there are no errors, `*errorbuf' will be set to NULL.  The
 * buffer `*errorbuf' should be deallocated with `sandbox_free_error'.
 *
 * @result 0 on success, -1 otherwise.
 */
int sandbox_init(const char *profile, uint64_t flags, char **errorbuf);

/*
 * @define SANDBOX_NAMED  The `profile' argument specifies a Sandbox
 * profile named by one of the kSBXProfile* string constants.
 */
#define SANDBOX_NAMED		0x0001

#ifdef __APPLE_API_PRIVATE

/* The following flags are reserved for Mac OS X.  Developers should not
 * depend on their availability.
 */

/*
 * @define SANDBOX_NAMED_BUILTIN   The `profile' argument specifies the
 * name of a builtin profile that is statically compiled into the
 * system.
 */
#define SANDBOX_NAMED_BUILTIN	0x0002

/*
 * @define SANDBOX_NAMED_EXTERNAL   The `profile' argument specifies the
 * pathname of a Sandbox profile.  The pathname may be abbreviated: If
 * the name does not start with a `/' it is treated as relative to
 * /usr/share/sandbox and a `.sb' suffix is appended.
 */
#define SANDBOX_NAMED_EXTERNAL	0x0003

/*
 * @define SANDBOX_NAMED_MASK   Mask for name types: 4 bits, 15 possible
 * name types, 3 currently defined.
 */
#define SANDBOX_NAMED_MASK	0x000f

#endif /* __APPLE_API_PRIVATE */

/*
 * Available Sandbox profiles.
 */

/* TCP/IP networking is prohibited. */
extern const char kSBXProfileNoInternet[];

/* All sockets-based networking is prohibited. */
extern const char kSBXProfileNoNetwork[];

/* File system writes are prohibited. */
extern const char kSBXProfileNoWrite[];

/* File system writes are restricted to temporary folders /var/tmp and
 * confstr(_CS_DARWIN_USER_DIR, ...).
 */
extern const char kSBXProfileNoWriteExceptTemporary[];

/* All operating system services are prohibited. */
extern const char kSBXProfilePureComputation[];

/*
 * @function sandbox_free_error
 * Deallocates an error string previously allocated by sandbox_init.
 *
 * @param errorbuf (input)   The buffer to be freed.  Must be a pointer
 * previously returned by sandbox_init in the `errorbuf' argument, or NULL.
 *
 * @result void
 */
void sandbox_free_error(char *errorbuf);


#ifdef __APPLE_API_PRIVATE

/* The following definitions are reserved for Mac OS X.  Developers should not
 * depend on their availability.
 */

int sandbox_init_with_parameters(const char *profile, uint64_t flags, const char *const parameters[], char **errorbuf);

int sandbox_init_with_extensions(const char *profile, uint64_t flags, const char *const extensions[], char **errorbuf);

enum sandbox_filter_type {
	SANDBOX_FILTER_NONE,
	SANDBOX_FILTER_PATH,
	SANDBOX_FILTER_GLOBAL_NAME,
	SANDBOX_FILTER_LOCAL_NAME,
	SANDBOX_FILTER_APPLEEVENT_DESTINATION,
	SANDBOX_FILTER_RIGHT_NAME,
	SANDBOX_FILTER_DESCRIPTOR,
};

extern const enum sandbox_filter_type SANDBOX_CHECK_NO_REPORT __attribute__((weak_import));

enum sandbox_extension_flags {
	FS_EXT_DEFAULTS =              0,
	FS_EXT_FOR_PATH =       (1 << 0),
	FS_EXT_FOR_FILE =       (1 << 1),
	FS_EXT_READ =           (1 << 2),
	FS_EXT_WRITE =          (1 << 3),
	FS_EXT_PREFER_FILEID =  (1 << 4),
};

int sandbox_check(pid_t pid, const char *operation, enum sandbox_filter_type type, ...);
int sandbox_check_by_audit_token(audit_token_t tok, const char* operation, enum sandbox_filter_type filt, ...);

int sandbox_note(const char *note);

int sandbox_suspend(pid_t pid);
int sandbox_unsuspend(void);

int sandbox_issue_extension(const char *path, char **ext_token);
int sandbox_issue_fs_extension(const char *path, uint64_t flags, char **ext_token);
int sandbox_issue_fs_rw_extension(const char *path, char **ext_token);
int sandbox_issue_mach_extension(const char *name, char **ext_token);

/* Apple's newer extension SPI (libsystem_sandbox.dylib / <sandbox/private.h>).
 * This tree cannot issue extensions (no policy engine); the symbol exists so the
 * Xcode clients that demand it bind, and always fails with NULL + errno. */
char *sandbox_extension_issue_file_to_process(const char *extension_class, const char *path, uint32_t flags, audit_token_t token);

/* The same SPI's issue_file, consume and release, declared here with the signatures Apple's own SPI
 * header states (src/external/WTF/wtf/spi/darwin/SandboxSPI.h) and the SDK's Swift interfaces confirm.
 * All three fail with errno = ENOTSUP for the reason in sandbox.c: there is no policy engine to issue,
 * validate or record an extension, and no handle can name anything. */
char *sandbox_extension_issue_file(const char *extension_class, const char *path, uint32_t flags);
int64_t sandbox_extension_consume(const char *extension_token);
int sandbox_extension_release(int64_t extension_handle);

int sandbox_consume_extension(const char *path, const char *ext_token);
int sandbox_consume_fs_extension(const char *ext_token, char **path);
int sandbox_consume_mach_extension(const char *ext_token, char **name);

int sandbox_release_fs_extension(const char *ext_token);

int sandbox_container_path_for_pid(pid_t pid, char *buffer, size_t bufsize);

int sandbox_wakeup_daemon(char **errorbuf);

int sandbox_query_approval_policy_for_path(const char* query, const char* path, char **approval);

/* libsystem_containermanager's query SPI, answered here -- see sandbox.c for why this file owns it and
 * why the query cannot be served. Apple ships no header for it in any SDK this workspace has, so the
 * shapes below are the ones the demanding images' call sites fix (measured): the query is rdi, the
 * scalar argument rsi, the answer rax; container_class_t is Apple's typedef for the class value
 * (container_query_set_class is called with 2 = application and 4 = widgetExtension). */
typedef struct container_query_s *container_query_t;
typedef struct container_error_s *container_error_t;
typedef struct container_s *container_t;

/* The primary persona's unique string (Mach-O name _CONTAINER_PERSONA_PRIMARY: the C identifier has no
 * leading underscore of its own -- the same underscore trap dar-8twt recorded for libRosetta). */
extern char *CONTAINER_PERSONA_PRIMARY;

container_query_t container_query_create(void);
void container_query_free(container_query_t query);
bool container_query_set_class(container_query_t query, uint64_t cls);
bool container_query_set_identifiers(container_query_t query, void *identifiers);
bool container_query_operation_set_flags(container_query_t query, uint64_t flags);
bool container_query_set_persona_unique_string(container_query_t query, const char *persona_unique_string);
container_t container_query_get_single_result(container_query_t query);
container_error_t container_query_get_last_error(container_query_t query);
char *container_error_copy_unlocalized_description(container_error_t error);

const char *_amkrtemp(const char *);

#endif /* __APPLE_API_PRIVATE */

__END_DECLS
#endif /* _SANDBOX_H_ */
