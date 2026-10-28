#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char** environ;

static int expect(const char* key, const char* wanted) {
	const char* actual = getenv(key);
	if (actual && strcmp(actual, wanted) == 0) return 0;
	fprintf(stderr, "FAIL xtrace environment: %s expected '%s', got '%s'\n",
		key, wanted, actual ? actual : "<absent>");
	return 1;
}

int main(int argc, char** argv) {
	setvbuf(stdout, NULL, _IONBF, 0);
	alarm(15);
	if (argc == 1) {
		/* Start the instrument only on this fixture, never on its launcher. */
		if (setenv("DYLD_INSERT_LIBRARIES", "/usr/lib/darling/libxtrace.dylib", 1) ||
			setenv("XTRACE_SPLIT_ENTRY_AND_EXIT", "1", 1) ||
			setenv("XTRACE_NO_COLOR", "1", 1) ||
			setenv("XTRACE_KPRINTF", "0", 1) ||
			setenv("XTRACE_LOG_FILE_PER_THREAD", "0", 1) ||
			setenv("XTRACE_LOG_FILE", "", 1)) {
			perror("setenv");
			return 1;
		}
		char* next[] = {argv[0], "traced", NULL};
		execve(argv[0], next, environ);
	} else if (argc == 2 && strcmp(argv[1], "traced") == 0) {
		/* Preserve runtime prerequisites, but supply none of the instrument's
		 * settings. The exec hook must reconstruct exact KEY=value entries. */
		const char* settings[] = {"DYLD_INSERT_LIBRARIES",
			"XTRACE_SPLIT_ENTRY_AND_EXIT", "XTRACE_NO_COLOR", "XTRACE_KPRINTF",
			"XTRACE_LOG_FILE_PER_THREAD", "XTRACE_LOG_FILE"};
		for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
			if (unsetenv(settings[i])) { perror("unsetenv"); return 1; }
		}
		if (setenv("XTRACE_EXEC_SENTINEL", "unchanged=tail", 1)) {
			perror("setenv");
			return 1;
		}
		char* next[] = {argv[0], "check", NULL};
		execve(argv[0], next, environ);
	} else if (argc == 2 && strcmp(argv[1], "check") == 0) {
		int failures = expect("XTRACE_EXEC_SENTINEL", "unchanged=tail");
		failures += expect("XTRACE_SPLIT_ENTRY_AND_EXIT", "1");
		failures += expect("XTRACE_NO_COLOR", "1");
		failures += expect("XTRACE_KPRINTF", "0");
		failures += expect("XTRACE_LOG_FILE_PER_THREAD", "0");
		failures += expect("XTRACE_LOG_FILE", "");
		failures += expect("DYLD_INSERT_LIBRARIES", "/usr/lib/darling/libxtrace.dylib");
		if (failures) return 1;
		puts("XTRACE_EXEC_ENVIRONMENT_OK");
		return 0;
	} else {
		fprintf(stderr, "FAIL xtrace environment: invalid fixture stage\n");
		return 1;
	}
	perror("execve");
	return 1;
}
