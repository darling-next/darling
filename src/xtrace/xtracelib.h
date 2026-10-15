#ifndef _XTRACELIB_H_
#define _XTRACELIB_H_
#include <stdint.h>
#include <darling/emulation/common/simple.h>

#include "base.h"
#include "string.h"

#ifdef XTRACE_CPP
struct calldef
{
	const char* name;
	void (*print_args)(xtrace::String* log, int nr, void* args[]);
	void (*print_retval)(xtrace::String* log, int nr, uintptr_t rv);
};

void handle_generic_entry(xtrace::String* log, const struct calldef* defs, const char* type, int nr, void* args[]);
void handle_generic_exit(xtrace::String* log, const struct calldef* defs, const char* type, uintptr_t retval, int force_split);

void xtrace_set_gray_color(xtrace::String* log);
void xtrace_reset_color(xtrace::String* log);

void xtrace_start_line(xtrace::String* log, int indent);
#endif

XTRACE_DECLARATIONS_C_BEGIN

extern int xtrace_no_color;

/// The most bytes of a message's own payload this tracer will look at, and the ceiling for a single printed record.
/// A payload's DECLARED length (a Mach message's `msgh_size`, an out-of-line descriptor's `size`) is what bounds a
/// read; this ceiling only says how much of a very large declared payload is worth printing, and it is configurable
/// because 512 bytes once cut the very buffer an abort message was in. When it does cut a record, the record SAYS
/// SO (`...[truncated: printed N of M declared bytes]`) -- a silently shortened payload reads like a different one.
extern unsigned long xtrace_max_payload_bytes;

void xtrace_log(const char* format, ...) __attribute__((format(printf, 1, 2)));
void xtrace_log_v(const char* format, va_list args) __attribute__((format(printf, 1, 0)));
void xtrace_error(const char* format, ...) __attribute__((format(printf, 1, 2)));
void xtrace_error_v(const char* format, va_list args) __attribute__((format(printf, 1, 0)));

void xtrace_abort(const char* message) __attribute__((noreturn));

XTRACE_DECLARATIONS_C_END

#endif

