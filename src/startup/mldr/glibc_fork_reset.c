#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "glibc_fork_reset.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Discovered locations inside glibc's struct rtld_global (a.k.a. _rtld_global).
// We never reference _rtld_global via `extern` (that would emit an R_X86_64_COPY
// relocation from this object and snapshot ld.so's live state into our BSS,
// desyncing it and breaking all threading). dlsym(RTLD_DEFAULT, ...) binds to the
// real object instead, with no copy relocation.
// ---------------------------------------------------------------------------
static volatile int* g_stack_cache_lock = NULL; // &GL(_dl_stack_cache_lock)
static void**        g_stack_cache_list = NULL; // &GL(_dl_stack_cache) (list head)
static void*         g_load_lock        = NULL; // &GL(_dl_load_lock).mutex
static void*         g_load_tls_lock    = NULL; // &GL(_dl_load_tls_lock).mutex

// glibc ABI: rtld recursive locks are pthread_mutex_t objects initialized with
// PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP.
#define MUTEX_SIZE sizeof(pthread_mutex_t)

static const pthread_mutex_t k_free_recursive_mutex =
	PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

// A recursive rtld lock that is currently free looks exactly like
// _RTLD_LOCK_RECURSIVE_INITIALIZER = {PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP}:
// every field zero except __kind == PTHREAD_MUTEX_RECURSIVE_NP.
static int is_free_recursive_mutex(const unsigned char* p) {
	return memcmp(p, &k_free_recursive_mutex, MUTEX_SIZE) == 0;
}

void __mldr_glibc_fork_reset_detect(void) {
	if (g_stack_cache_lock)
		return;

	char* base = (char*) dlsym(RTLD_DEFAULT, "_rtld_global");
	if (!base)
		return;

	// (1) Stack-cache region, found by the self-referential signature of the
	// thread-stack lists, which are in a known single-threaded state at startup:
	//   list_t    _dl_stack_used;          // +0   empty (head points to itself)
	//   list_t    _dl_stack_user;          // +16  exactly one element (main thread)
	//   list_t    _dl_stack_cache;         // +32  empty (head points to itself)
	//   size_t    _dl_stack_cache_actsize; // +48  0
	//   uintptr_t _dl_in_flight_stack;     // +56  0
	//   int       _dl_stack_cache_lock;    // +64  0 (unlocked)
	// BOUND THE SCAN BY THE MAPPING THAT CONTAINS THE OBJECT. MEASURED FAULT: this loop used to walk a fixed
	// window up to base+16384 inside _rtld_global whatever the object's real size is. When the object's mapping
	// ends before that -- which depends on the loader layout, and therefore on ASLR, so it happens in some runs
	// and not others -- the loop reads past the mapping into an unmapped page and the guest process dies with a
	// hardware SIGSEGV in the LOADER itself. Caught on this Bead's witness: ip in mldr
	// (__mldr_glibc_fork_reset_detect+0x8b), si_code=1 SEGV_MAPERR, si_addr page-aligned, instruction
	// `movq (%rax), %rax` reading the address just computed from base+u. The guest has no handler that can
	// report it, so the death was silent everywhere except in the deliberate witness.
	size_t window_limit = 16384;
	{
		// /proc/self/maps gives the mapped extent of the object that contains `base`; the scan stops at its end.
		int mfd = open("/proc/self/maps", O_RDONLY);
		if (mfd >= 0) {
			static char mbuf[65536];
			ssize_t got = read(mfd, mbuf, sizeof(mbuf) - 1);
			close(mfd);
			if (got > 0) {
				mbuf[got] = '\0';
				unsigned long want = (unsigned long)base;
				unsigned long best_end = 0;
				char* line = mbuf;
				while (line && *line) {
					char* nl = strchr(line, '\n');
					if (nl) *nl = '\0';
					unsigned long lo = 0, hi = 0;
					if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 && lo <= want && want < hi) {
						best_end = hi;
					}
					line = nl ? nl + 1 : NULL;
				}
				if (best_end > want) {
					size_t avail = (size_t)(best_end - want);
					if (avail < window_limit) window_limit = avail;
				} else {
					// the containing mapping is unknown: do not scan at all rather than risk a fault
					window_limit = 0;
				}
			} else {
				window_limit = 0;
			}
		} else {
			window_limit = 0;
		}
		if (window_limit < 256 + 72) return;
	}

	for (size_t u = 256; u + 72 <= window_limit; u += sizeof(void*)) {
		void** used  = (void**)(base + u);
		void** user  = (void**)(base + u + 16);
		void** cache = (void**)(base + u + 32);

		if ((char*)used[0]  != base + u      || (char*)used[1]  != base + u)      continue;
		if ((char*)cache[0] != base + u + 32 || (char*)cache[1] != base + u + 32) continue;
		if (user[0] == NULL || user[0] != user[1] || (char*)user[0] == base + u + 16) continue;
		if (*(size_t*)(base + u + 48) != 0)    continue;
		if (*(uintptr_t*)(base + u + 56) != 0) continue;
		if (*(int*)(base + u + 64) != 0)       continue;

		g_stack_cache_list = (void**)(base + u + 32);
		g_stack_cache_lock = (volatile int*)(base + u + 64);
		break;
	}

	// (2) The three consecutive loader locks _dl_load_lock, _dl_load_write_lock,
	// _dl_load_tls_lock: three back-to-back free recursive mutexes (no fields
	// between them in struct rtld_global). They sit before the stack region, and
	// nothing else in the struct places three recursive mutexes at a 40-byte
	// stride, so the first such triple is unambiguous.
	size_t cap = g_stack_cache_lock
		? (size_t)((char*)g_stack_cache_lock - base)
		: 16384;
	for (size_t o = 0; o + 3 * MUTEX_SIZE <= cap; o += sizeof(void*)) {
		if (is_free_recursive_mutex((unsigned char*)base + o)
			&& is_free_recursive_mutex((unsigned char*)base + o + MUTEX_SIZE)
			&& is_free_recursive_mutex((unsigned char*)base + o + 2 * MUTEX_SIZE)) {
			g_load_lock     = base + o;
			g_load_tls_lock = base + o + 2 * MUTEX_SIZE;
			break;
		}
	}
}

// Restore a recursive rtld lock to _RTLD_LOCK_RECURSIVE_INITIALIZER (free): all
// fields zero except __kind == PTHREAD_MUTEX_RECURSIVE_NP. This is exactly what
// glibc's __rtld_lock_initialize(GL(...)) does in the fork child.
static void reset_recursive_mutex(void* m) {
	memcpy((char*)m + sizeof(int),
		(const char*)&k_free_recursive_mutex + sizeof(int),
		MUTEX_SIZE - sizeof(int));
	__atomic_store_n((volatile int*)m, 0, __ATOMIC_SEQ_CST); // __lock: release last
}

void __mldr_glibc_fork_reset_child(void) {
	// glibc's __libc_fork child path resets these two loader locks unconditionally
	// (posix/fork.c). A raw fork skips that, so a thread that held _dl_load_lock
	// (inside dlopen/dlclose) or _dl_load_tls_lock (TLS setup) at fork time would
	// otherwise deadlock the child's first dlopen()/pthread_create().
	if (g_load_lock)     reset_recursive_mutex(g_load_lock);
	if (g_load_tls_lock) reset_recursive_mutex(g_load_tls_lock);

	// The raw fork left GL(_dl_stack_cache_lock) in whatever state the parent had.
	// Reset it (and drop the inherited stack cache, whose entries reference threads
	// that no longer exist) so the child's pthread_create cannot deadlock. Clearing
	// the cache before unlocking keeps the subsystem self-consistent if the lock was
	// held mid-mutation in the parent.
	if (g_stack_cache_lock) {
		g_stack_cache_list[0] = g_stack_cache_list;            // _dl_stack_cache.next = &head
		g_stack_cache_list[1] = g_stack_cache_list;            // _dl_stack_cache.prev = &head
		*(size_t*)((char*)g_stack_cache_lock - 16) = 0;        // _dl_stack_cache_actsize
		*(uintptr_t*)((char*)g_stack_cache_lock - 8) = 0;      // _dl_in_flight_stack
		__atomic_store_n(g_stack_cache_lock, 0, __ATOMIC_SEQ_CST); // unlock last
	}
}

#ifdef GLIBC_FORK_RESET_TEST_HOOKS
void* __mldr_test_stack_cache_lock(void) { return (void*) g_stack_cache_lock; }
void* __mldr_test_load_lock(void)        { return g_load_lock; }
void* __mldr_test_load_tls_lock(void)    { return g_load_tls_lock; }
#endif
