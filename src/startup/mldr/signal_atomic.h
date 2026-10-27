#ifndef MLDR_SIGNAL_ATOMIC_H
#define MLDR_SIGNAL_ATOMIC_H

#include <errno.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

/* libc's syscall entry is only a transport: do not use its sigset_t or its
 * cached pthread signal mask, which cannot represent native signals 32/33. */
static inline int mldr_native_signal_mask(int how, const void* set, void* saved) {
	int saved_errno = errno;
	long result = syscall(SYS_rt_sigprocmask, how, set, saved, 8);
	int status = result < 0 ? -errno : (int)result;
	errno = saved_errno;
	return status;
}

#define SIGNAL_ATOMIC_NATIVE_MASK(how, set, saved) mldr_native_signal_mask(how, set, saved)
#include <common/native_signal_atomic.h>
#undef SIGNAL_ATOMIC_NATIVE_MASK

/* These callbacks cannot report mask failures. Never enter protected loader
 * state without exclusion, or resume application code with a damaged mask. */
static inline void mldr_block_async_signals(signal_atomic_mask_t* saved) {
	if (signal_atomic_begin(saved) < 0) {
		abort();
	}
}

static inline void mldr_restore_signals(const signal_atomic_mask_t* saved) {
	if (signal_atomic_end(saved) < 0) {
		abort();
	}
}

#endif
