/* Guest CLT fixture. Start an idle prefix with the existing host-side
 * DSERVER_LOG_LEVEL=info before its server starts; do not change product defaults.
 * The same fixture must run against the old and new complete installed images. */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef void (*kprintf_fn)(const char*, ...);

static void expect_record(FILE* log, const char* payload)
{
	char line[4096];
	unsigned int matches = 0;
	while (fgets(line, sizeof(line), log) != NULL) {
		const char* body = strstr(line, payload);
		if (body == NULL)
			continue;
		assert(body[strlen(payload)] == '\n');
		assert(body[strlen(payload) + 1] == '\0');
		++matches;
	}
	assert(!ferror(log));
	assert(matches == 1);
	clearerr(log);
}

int main(void)
{
	void* kernel = dlopen("/usr/lib/system/libsystem_kernel.dylib", RTLD_NOW);
	assert(kernel != NULL);
	kprintf_fn kprintf = (kprintf_fn)dlsym(kernel, "__simple_kprintf");
	assert(kprintf != NULL);
	FILE* log = fopen("/var/log/dserver.log", "r");
	assert(log != NULL);
	assert(fseek(log, 0, SEEK_END) == 0);

	char message[128];
	assert(snprintf(message, sizeof(message), "DARLING_KPRINTF_PAYLOAD pid=%d value=73",
		(int)getpid()) > 0);
	kprintf("%s \t\r\n", message);
	/* No polling: synchronous completion must mean the payload is persisted.
	 * Checking the complete suffix also proves server-side whitespace trimming. */
	expect_record(log, message);

	assert(fclose(log) == 0);
	assert(dlclose(kernel) == 0);
	puts("DARLING_KPRINTF_LOGGING_OK");
	return 0;
}
