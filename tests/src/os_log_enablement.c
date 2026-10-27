/* Native macOS 26.6.2 ARM64/x86_64 reference:
 * DEBUG < INFO < DEFAULT < ERROR < FAULT, returning rank(lhs)-rank(rhs).
 * The opaque hook record's type is byte 1, including unknown type bytes. */
#include <assert.h>
#include <Block.h>
#include <dlfcn.h>
#include <os/log.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef int (*compare_fn)(os_log_type_t, os_log_type_t);
typedef os_log_type_t (*get_type_fn)(const void *);
typedef void (^hook_fn)(os_log_type_t, const void *);
typedef hook_fn (*set_hook_fn)(os_log_type_t, hook_fn);
typedef char *(*copy_message_fn)(const void *);

static void release_previous(hook_fn previous)
{
	if (previous != NULL)
		Block_release(previous);
}

static void check_hooks(get_type_fn get_type, set_hook_fn set_hook,
	copy_message_fn copy_message)
{
	static const char immutable_message[] = "immutable-main-image";
	static volatile char writable_message[] = "writable-main-image";
	char mutable_public[] = "before-value";
	writable_message[0] = 'w';
	struct {
		unsigned int calls;
		pthread_t emitter;
		char *messages[5];
		char *mutable_public;
	} state = {
		.emitter = pthread_self(),
		.mutable_public = mutable_public,
	}, *observed = &state;
	hook_fn quiet = ^(os_log_type_t type, const void *message) {
		(void)type;
		(void)message;
	};
	/* DEFAULT resets the native historical-minimum sentinel. */
	release_previous(set_hook(OS_LOG_TYPE_DEFAULT, quiet));
	release_previous(set_hook(OS_LOG_TYPE_ERROR,
		^(os_log_type_t type, const void *message) {
			assert(pthread_equal(pthread_self(), observed->emitter));
			assert(type == get_type(message));
			assert(observed->calls < 5);
			unsigned int index = observed->calls++;
			observed->messages[index] = copy_message(message);
			if (index == 4) {
				strcpy(observed->mutable_public, "after-value");
				char *again = copy_message(message);
				assert(strcmp(again, "before-value") == 0);
				free(again);
			}
			/* A recursive emitter must not reenter this callback. */
			os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR,
				"%{public}s", "recursive-message");
		}));
	os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_INFO, "%{public}s", "filtered");
	assert(state.calls == 0);
	os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR, "%{public}s", "public-value");
	assert(state.calls == 1);
	assert(strcmp(state.messages[0], "public-value") == 0);

	/* Raising the requested threshold preserves the historical lower rank. */
	hook_fn active = set_hook(OS_LOG_TYPE_FAULT, quiet);
	release_previous(set_hook(OS_LOG_TYPE_FAULT, active));
	Block_release(active);
	os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR, "%{private}s", "secret");
	assert(state.calls == 2);
	assert(strcmp(state.messages[1], "<private>") == 0);
	/* Default privacy follows dyld immutability, not image membership alone. */
	os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_FAULT, "%s", immutable_message);
	assert(state.calls == 3);
	assert(strcmp(state.messages[2], immutable_message) == 0);
	os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_FAULT, "%s",
		(const char *)writable_message);
	assert(state.calls == 4);
	assert(strcmp(state.messages[3], "<private>") == 0);
	/* The record snapshots arguments before calling client code. */
	os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR, "%{public}s", mutable_public);
	assert(state.calls == 5);
	assert(strcmp(mutable_public, "after-value") == 0);
	assert(strcmp(state.messages[4], "before-value") == 0);
	/* Every returned string remains caller-owned after later emissions. */
	assert(strcmp(state.messages[0], "public-value") == 0);
	for (unsigned int index = 0; index < 5; ++index)
		free(state.messages[index]);

	release_previous(set_hook(OS_LOG_TYPE_DEFAULT, quiet));
	unsigned int replacements = 0, *replacement_count = &replacements;
	hook_fn replacement_hook = Block_copy(^(os_log_type_t type, const void *message) {
			++*replacement_count;
			assert(type == OS_LOG_TYPE_FAULT);
			/* Keep a caller-owned reference while releasing the displaced
			 * registration; native does not retain a snapshot for us. */
			release_previous(set_hook(OS_LOG_TYPE_FAULT, quiet));
			assert(get_type(message) == type);
			char *copy = copy_message(message);
			assert(strcmp(copy, "replacement-still-alive") == 0);
			free(copy);
		});
	release_previous(set_hook(OS_LOG_TYPE_FAULT, replacement_hook));
	os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR, "%{public}s", "filtered-again");
	assert(replacements == 0);
	os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_FAULT,
		"%{public}s", "replacement-still-alive");
	assert(replacements == 1);
	Block_release(replacement_hook);
	os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_FAULT, "%{public}s", "quiet-now");
	assert(replacements == 1);

	pid_t child = fork();
	assert(child >= 0);
	if (child == 0) {
		set_hook(OS_LOG_TYPE_ERROR, NULL);
		_exit(99);
	}
	int status = 0;
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status));
	assert(WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGTRAP);
}

static void check_invalid_type(compare_fn compare, os_log_type_t left,
	os_log_type_t right)
{
	pid_t child = fork();
	assert(child >= 0);
	if (child == 0) {
		compare(left, right);
		_exit(99);
	}
	int status = 0;
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status));
	assert(WTERMSIG(status) == SIGILL);
}

int main(void)
{
	compare_fn compare = (compare_fn)dlsym(RTLD_DEFAULT,
		"os_log_compare_enablement");
	get_type_fn get_type = (get_type_fn)dlsym(RTLD_DEFAULT, "os_log_get_type");
	set_hook_fn set_hook = (set_hook_fn)dlsym(RTLD_DEFAULT, "os_log_set_hook");
	copy_message_fn copy_message = (copy_message_fn)dlsym(RTLD_DEFAULT,
		"os_log_copy_message_string");
	assert(compare != NULL);
	assert(get_type != NULL);
	assert(set_hook != NULL);
	assert(copy_message != NULL);

	const os_log_type_t ordered_types[] = {
		OS_LOG_TYPE_DEBUG, OS_LOG_TYPE_INFO, OS_LOG_TYPE_DEFAULT,
		OS_LOG_TYPE_ERROR, OS_LOG_TYPE_FAULT,
	};
	for (int left = 0; left < 5; ++left)
		for (int right = 0; right < 5; ++right)
			assert(compare(ordered_types[left], ordered_types[right]) == left - right);

	unsigned char record[] = {0xa5, 0, 0x5a};
	for (unsigned int type = 0; type <= UINT8_MAX; ++type) {
		record[1] = (unsigned char)type;
		assert(get_type(record) == (os_log_type_t)type);
		assert(record[0] == 0xa5 && record[1] == type && record[2] == 0x5a);
	}

	/* The x86_64 reference's invalid-type path is ud2, not a value fallback. */
	check_invalid_type(compare, (os_log_type_t)3, OS_LOG_TYPE_DEFAULT);
	check_invalid_type(compare, OS_LOG_TYPE_DEFAULT, (os_log_type_t)UINT8_MAX);
	check_hooks(get_type, set_hook, copy_message);
	puts("DARLING_OS_LOG_ENABLEMENT_OK");
	return 0;
}
