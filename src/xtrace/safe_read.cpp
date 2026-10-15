#include "safe_read.h"

// See safe_read.h for the measured incident this file exists for. The mechanism, in one paragraph: Linux can read
// another (or the same) process's memory without touching the address -- `process_vm_readv` returns EFAULT for an
// unmapped range instead of raising a signal -- so this file issues that call as a RAW linux syscall (number 310,
// with 39 for getpid). Raw, because xtrace runs inside the traced process where libSystem may be premigrated or
// mid-emulation, and because a raw `syscall` instruction is not routed through the very hooks the tracer installs
// (a traced syscall would recurse). No signal handler is installed anywhere: the guest's own SIGSEGV/SIGBUS
// dispositions are observable (`[segvdisp ...]`, `[sigsegv-mask ...]`), and an instrument that changed them would
// perturb exactly what it measures.

#define XTRACE_SYSCALL_GETPID 39L
#define XTRACE_SYSCALL_PROCESS_VM_READV 310L

// The host's page size, not the guest's: the traced process runs on this CPU, and its mappings are the host's.
#define XTRACE_PAGE_SIZE 4096UL
#define XTRACE_PAGE_MASK (~(XTRACE_PAGE_SIZE - 1))

// `struct iovec` as the LINUX kernel lays it out. Defined here rather than taken from a header because xtrace is
// built with -nostdinc for the guest, and the Darwin `iovec` is the same two fields anyway.
struct xtrace_iovec {
	void* base;
	size_t len;
};

#if defined(__x86_64__)

XTRACE_INLINE long xtrace_raw_syscall1(long nr, long a1) {
	long ret;
	__asm__ __volatile__("syscall"
		: "=a"(ret)
		: "a"(nr), "D"(a1)
		: "rcx", "r11", "memory");
	return ret;
}

// The 4th/5th/6th arguments are moved into their registers INSIDE the asm text.
//
// MEASURED: this dylib is compiled with `-ffixed-r10`, so clang cannot allocate r10 for anything -- a register
// variable declared `register long x __asm__("r10")` fails the whole translation unit with "couldn't allocate input
// reg for constraint '{r10}'". The tree's own raw-syscall helpers therefore move the argument with `movq %N, %%r10`
// (see libsystem_kernel's mach_traps.c and dserver-ring.c), and r8/r9/r10 sit in the CLOBBER list, which is what
// keeps the compiler from allocating an input into one of the registers the moves are about to overwrite.
static long xtrace_raw_syscall6(long nr, long a1, long a2, long a3, long a4, long a5, long a6) {
	long ret;
	__asm__ __volatile__(
		"movq %[a4], %%r10\n\t"
		"movq %[a5], %%r8\n\t"
		"movq %[a6], %%r9\n\t"
		"syscall"
		: "=a"(ret)
		: "a"(nr), "D"(a1), "S"(a2), "d"(a3), [a4] "r"(a4), [a5] "r"(a5), [a6] "r"(a6)
		: "rcx", "r11", "r10", "r8", "r9", "memory");
	return ret;
}

size_t xtrace_safe_copy(void* dst, const void* src, size_t len, int* error_out)
{
	if (error_out != NULL) {
		*error_out = 0;
	}
	if (len == 0) {
		return 0;
	}
	if (dst == NULL || src == NULL) {
		if (error_out != NULL) {
			*error_out = 14; // EFAULT: a NULL base is not a readable range
		}
		return XTRACE_READ_UNREADABLE;
	}

	// getpid() per call, deliberately NOT cached: the tracer survives fork and exec (it re-injects itself), so a
	// cached pid would be the parent's in the child and every read there would answer about the wrong process.
	const long pid = xtrace_raw_syscall1(XTRACE_SYSCALL_GETPID, 0);
	if (pid <= 0) {
		if (error_out != NULL) {
			*error_out = 3; // ESRCH
		}
		return XTRACE_READ_UNREADABLE;
	}

	struct xtrace_iovec local = { dst, len };
	struct xtrace_iovec remote = { (void*) src, len };
	long got = xtrace_raw_syscall6(XTRACE_SYSCALL_PROCESS_VM_READV,
		pid, (long) &local, 1, (long) &remote, 1, 0);
	if (got < 0) {
		if (error_out != NULL) {
			*error_out = (int) -got;
		}
		return XTRACE_READ_UNREADABLE;
	}
	if ((size_t) got < len && error_out != NULL) {
		// A SHORT copy is reported as 14: the tail is not readable, and every caller here treats that the same way
		// as an unmapped base. Returning only the short count would let a caller print a truncated record as whole.
		*error_out = 14;
	}
	return (size_t) got;
}

#else // !__x86_64__

// THE 32-BIT IMAGE KEEPS THE PREVIOUS BEHAVIOUR, and says so. `process_vm_readv` has a different ABI there and the
// register the x86_64 form needs does not exist, so the guard cannot be built in this pass; the copy is therefore a
// plain bounded one. This is the same reading the 32-bit slice did before this file existed, and the callers that
// would DEREFERENCE foreign memory for the first time are compiled out for it (see print_mach_msg's OOL arm) --
// a fault-safety that only exists on one architecture must not read as if it existed on both.
size_t xtrace_safe_copy(void* dst, const void* src, size_t len, int* error_out)
{
	if (error_out != NULL) {
		*error_out = 0;
	}
	if (len == 0) {
		return 0;
	}
	if (dst == NULL || src == NULL) {
		return XTRACE_READ_UNREADABLE;
	}
	__builtin_memcpy(dst, src, len);
	return len;
}

#endif // __x86_64__

size_t xtrace_safe_read_c_string(char* dst, size_t max, const char* src, int* error_out)
{
	if (error_out != NULL) {
		*error_out = 0;
	}
	if (dst == NULL || src == NULL) {
		if (error_out != NULL) {
			*error_out = 14;
		}
		return XTRACE_READ_UNREADABLE;
	}
	if (max == 0) {
		dst[0] = '\0';
		return 0;
	}

	size_t got = 0;
	while (got < max) {
		// One chunk per PAGE, never across a page boundary: a string may end on a page whose successor is not
		// mapped, and a chunk that crossed it would be refused as a whole (the string would read as unreadable).
		const size_t off = (size_t) ((const char*) src + got);
		const size_t page_left = XTRACE_PAGE_SIZE - (off & (XTRACE_PAGE_SIZE - 1));
		size_t chunk = max - got;
		if (chunk > page_left) {
			chunk = page_left;
		}

		int err = 0;
		const size_t n = xtrace_safe_copy(dst + got, (const char*) src + got, chunk, &err);
		if (n == XTRACE_READ_UNREADABLE) {
			if (error_out != NULL) {
				*error_out = err;
			}
			return XTRACE_READ_UNREADABLE;
		}

		for (size_t i = 0; i < n; i++) {
			if (dst[got + i] == '\0') {
				return got + i;
			}
		}
		if (n < chunk) {
			// The chunk was cut short and no NUL was in what did arrive: the rest is not mapped, so this is NOT a
			// string we may report a length for.
			if (error_out != NULL) {
				*error_out = err != 0 ? err : 14;
			}
			return XTRACE_READ_UNREADABLE;
		}
		got += n;
	}

	dst[max] = '\0';
	// The bound was reached with no NUL. Which it is -- a string longer than `max`, or a string whose end is not
	// mapped -- is decided by ONE byte past the bound: if that byte cannot be read, the honest answer is
	// `unreadable`, because returning `max` would have the caller print a truncation note for a range it could not
	// read at all. (MEASURED in the contract's own arms: a page filled with 'A' followed by an unmapped page used to
	// come back as "longer than the bound".)
	{
		char probe = 0;
		int probe_err = 0;
		if (xtrace_safe_copy(&probe, (const char*) src + max, 1, &probe_err) == XTRACE_READ_UNREADABLE) {
			if (error_out != NULL) {
				*error_out = probe_err;
			}
			return XTRACE_READ_UNREADABLE;
		}
	}
	return max; // exactly `max`: the caller reports that the string was longer than the bound
}

// A per-thread scratch, so a reader never needs a large frame on a traced thread's stack (the tracer runs on the
// traced thread, whose stack may be small or nearly full) and never dereferences the foreign pointer while
// printing. `static __thread` is what the rest of this directory already uses on both architectures (see
// bsd_trace.cpp's read-payload stash); `xtrace_tls` is not used because it would drag mach/port.h in and make this
// file untestable on the host, and because a reader has no destructor to run.
static __thread char xtrace_scratch_buffer[XTRACE_SCRATCH_SIZE];

char* xtrace_safe_scratch(void)
{
	return xtrace_scratch_buffer;
}
