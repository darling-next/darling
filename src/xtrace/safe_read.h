#ifndef XTRACE_SAFE_READ
#define XTRACE_SAFE_READ

#include "base.h"
#include <stddef.h>

// Reading memory that belongs to the TRACED programme, without ever being able to kill it.
//
// MEASURED (follow-up 8): a descriptor's address was dereferenced on a formatting path and the traced arm died with
// SIGSEGV 3 s in -- RC=139, core /tmp/t4cores/core.2915140 -- so the instrument killed the subject it was measuring
// and the run was lost. A bound on the LENGTH cannot prevent that: the BASE can be stale. Every read of foreign
// memory therefore goes through this file, which never loads from the pointer itself.
//
// The `_unreadable` return is the point: a caller must print `unreadable: <reason>`, never a zero and never
// nothing. A diagnostic that silently reports nothing is the failure mode this whole directory exists to remove.
//
// This file depends on NOTHING but base.h, so `tests/run-xtrace-safe-read-contract.sh` can compile it on the host
// and hold it to that promise with a real unmapped page.

XTRACE_DECLARATIONS_C_BEGIN

/// `(size_t) -1`: the range could not be read (unmapped, or only partly mapped).
#define XTRACE_READ_UNREADABLE ((size_t) -1)

/// The longest string this reader will take from the traced process, in bytes (excluding the NUL). A path or a
/// service name fits many times over; a runaway string or a stale pointer does not run off a page.
#define XTRACE_STRING_MAX 4096

/// The per-thread scratch every reader copies into, so a caller never dereferences the foreign pointer while
/// printing and never needs a large frame on a traced thread's stack. One buffer per thread, never freed (it is a
/// `__thread` array, so a thread's exit releases it with the rest of the tracer's TLS).
char* xtrace_safe_scratch(void);
/// One byte MORE than XTRACE_STRING_MAX, so the NUL a string read appends always has room.
#define XTRACE_SCRATCH_SIZE (XTRACE_STRING_MAX + 1)

/// Copy up to `len` bytes from `src` (in the traced process's own address space) into `dst`.
/// Returns the number of bytes copied -- which may be SHORTER than `len` when the tail is unmapped -- or
/// `XTRACE_READ_UNREADABLE` when the range is not readable at all (the raw errno is then in `error_out`).
size_t xtrace_safe_copy(void* dst, const void* src, size_t len, int* error_out);

/// Read a NUL-terminated string from the traced process into `dst` (`max + 1` bytes are written; the copy is always
/// NUL-terminated). Returns the length WITHOUT the NUL, or `XTRACE_READ_UNREADABLE` when the string is not readable
/// up to its end. A return of exactly `max` means the string is longer than `max`: the caller says so, because a
/// silently shortened string reads like a different string.
size_t xtrace_safe_read_c_string(char* dst, size_t max, const char* src, int* error_out);

XTRACE_DECLARATIONS_C_END

#endif // XTRACE_SAFE_READ
