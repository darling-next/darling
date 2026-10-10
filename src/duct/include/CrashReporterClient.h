#ifndef CRASHREPORTERCLIENT_H_
#define CRASHREPORTERCLIENT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRASHREPORTER_ANNOTATIONS_SECTION "__crash_info"
#define CRASHREPORTER_ANNOTATIONS_VERSION 5
#define CRASH_REPORTER_CLIENT_HIDDEN __attribute__((visibility("hidden")))

/* These are conveniences for callers that cannot link the real shim: a bare `inline` in C emits an EXTERNAL
 * definition in every translation unit that includes this header, which is how CRSetCrashLogMessage ended up
 * exported as a weak definition from unrelated images (measured: libdyld.dylib and libc++abi.dylib), i.e. a
 * public binding that could resolve to a no-op. `static inline` keeps them local to their caller, so the only
 * external definition is the strong one in libsystem_duct (duct/src/CRGetCrashLogMessage.c), and ADR 0004's
 * "one implementation per public name" holds. */
static inline void CRSetCrashLogMessage(const char* text) { (void)text; }
static inline void CRSetCrashLogMessage2(const char* path) { (void)path; }
static inline const char* CRGetCrashLogMessage(void) { return 0; }

struct crashreporter_annotations_t {
    uint64_t version;
    uint64_t message;
    uint64_t signature_string;
    uint64_t backtrace;
    uint64_t message2;
    uint64_t thread;
    uint64_t dialog_mode;
    uint64_t abort_cause;
};

CRASH_REPORTER_CLIENT_HIDDEN
extern struct crashreporter_annotations_t gCRAnnotations;

#ifdef __cplusplus
}
#endif

#endif
