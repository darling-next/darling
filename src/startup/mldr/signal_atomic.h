#ifndef MLDR_SIGNAL_ATOMIC_H
#define MLDR_SIGNAL_ATOMIC_H

#include <pthread.h>
#include <signal.h>
#include <rtsig.h>

/* Keep server suspension and S2C delivery available, as the RPC hooks require. */
static inline void mldr_block_async_signals(sigset_t* saved) {
	sigset_t set;
	sigfillset(&set);
	sigdelset(&set, LINUX_SIGRTMIN);
	sigdelset(&set, LINUX_SIGRTMIN + 1);
	pthread_sigmask(SIG_BLOCK, &set, saved);
}

static inline void mldr_restore_signals(const sigset_t* saved) {
	pthread_sigmask(SIG_SETMASK, saved, NULL);
}

#endif
