/*
 * This file is part of Darling.
 *
 * Copyright (C) 2026 Darling contributors
 *
 * Darling is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Darling is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Darling.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * native_threads.c -- guest identity for host threads created by NATIVE libraries inside a guest process.
 *
 * THE DEFECT (measured 2026-10-13, bead dar-liaq, four xcodebuild cores). A guest process runs native (host ELF)
 * libraries through the loader's elfcalls table -- Mesa's libvulkan/lavapipe, the generated GL/EGL wrappers, the
 * metal/indium Vulkan bridge -- and those libraries create threads with the HOST libc's pthread_create. That call
 * never enters Darling's own thread creation (threads.c:__darling_thread_create), which is the ONLY place that
 * installs a per-thread guest TSD base (dthreads.c:__darling_dthread_set_tsd_base -> arch_prctl(ARCH_SET_GS)) and
 * the only place that fills TSD slot 3 (__TSD_MACH_THREAD_SELF) with the thread's own Mach port. A host thread
 * therefore INHERITS its creator's GS base and READS THE CREATOR'S LIVE IDENTITY: `movq %gs:24` answers with
 * another thread's mach_thread_self token. Measured: 26 of 28 threads of a trapped xcodebuild process shared one
 * gs_base -- the main thread's TSD array -- and all 26 read the main thread's token 0x103. libdispatch's ownership
 * guard is _dispatch_lock_is_locked_by(word, self) with self = slot3 & 0xfffffffc, so an in-flight dispatch_once
 * gate owned by a DIFFERENT thread reads as "mine", and libdispatch traps BY DESIGN (DISPATCH_CLIENT_CRASH(0,
 * "trying to lock recursively") in _dispatch_once_wait). xcodebuild dies from that trap.
 *
 * THE HOOK, and why this one. mldr is the guest process's main executable, so it heads the dynamic loader's global
 * symbol scope: a definition of pthread_create in mldr interposes for every object loaded into that process
 * afterwards -- whether it was dlopened by mldr (the elfcalls table), by a guest wrapper dylib, or by ANOTHER
 * native library with its own dlopen. That last path is the one that matters here: Mesa's Vulkan loader opens its
 * ICD itself (/usr/share/vulkan/icd.d/lvp_icd.json -> "library_path": "libvulkan_lvp.so"), so libvulkan_lvp.so --
 * the library whose 26 threads were trapped -- is NOT loaded through `_elfcalls->dlopen` and a hook placed in that
 * wrapper would never see it. The export is deliberate and narrow: CMakeLists.txt passes
 * -Wl,--export-dynamic-symbol=pthread_create, so exactly this one name leaves .dynsym and mldr's other symbols stay
 * private. Reachability was measured, not assumed: an executable that defines and exports pthread_create interposes
 * for a library dlopened RTLD_LOCAL from it, both on the lazy first call and after (host experiment, 2026-10-13).
 *
 * WHAT IT DOES. For a host thread that is not one of Darling's own guest threads, the thread's FIRST act is to
 * install its own guest TSD array, whose slot 3 stays ZERO -- the thread presents no identity rather than someone
 * else's. The array is a zeroed dthread block initialized with the same primitive mldr uses for its own guest
 * threads, so the guest-visible invariants hold for this thread too (slot 0 is its own pthread self, slot 1 is its
 * own errno) instead of aliasing the creator's. Host libc is unaffected by the change of GS base: on x86_64 it uses
 * FS for TLS and never GS (measured: 0 `%gs` references, 4895 `%fs` references in the host libc.so.6), which is
 * also why every Darling guest thread can already run host libc with GS pointing at its guest TSD array.
 *
 * GIVING THE THREAD A REAL MACH PORT WAS REJECTED, deliberately. The server's Thread object is removed only when
 * the guest thread tells it that it died (darlingserver src/thread.cpp:247, reached from the guest's thread
 * terminate path); a host thread created by a native library takes part in none of that life cycle. Registering one
 * through the plane (DSERVER_PROCESS_CONTROL_OP_THREAD_SELF_BOOTSTRAP, which is how the loader bootstraps the main
 * thread) would mint a Thread keyed by a Linux tid the kernel reuses later, and nothing would ever retire it. Zero
 * is the safe value for libdispatch's owner test: `(word ^ 0) & 0xfffffffc == 0` holds only for a word whose low 30
 * bits are clear (an unlocked gate or the waiters-flag states), never for a gate that carries a live owner.
 *
 * NOT COVERED. Such a thread is still not a guest thread: it has no darlingserver registration, so mach_thread_self()
 * on it answers 0 and guest code running on it must not assume a Mach thread port; it is not in the guest's pthread
 * list and does not run the guest's pthread start logic. The TSD block is allocated per thread and deliberately not
 * freed: it must stay owned and unshared for the thread's whole life, including after the thread's start routine
 * returns, because host glibc then runs that thread's TLS destructors (which may call guest code). At ~6.5 KiB per
 * native thread that is noise beside the thread's own stack (>= 16 KiB, 8 MiB by default).
 */

#include "native_threads.h"
#include "dthreads.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef int (*host_pthread_create_t)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);

/* Set while mldr creates one of its OWN guest threads (threads.c), which installs the real guest TSD base itself.
 * Per-thread, so a native library creating threads concurrently with a guest thread creation is unaffected. */
static __thread int t_in_darling_thread_create = 0;

void __darling_native_thread_create_begin(void)
{
	t_in_darling_thread_create = 1;
}

void __darling_native_thread_create_end(void)
{
	t_in_darling_thread_create = 0;
}

struct native_thread_arg
{
	void* (*start_routine)(void*);
	void* arg;
};

static host_pthread_create_t t_real_pthread_create = NULL;

/* The loader's rule on this path: no stdio and no libc locks. One raw write, formatted by hand. */
static void native_thread_trace(const char* tag, unsigned long a, unsigned long b)
{
	static int enabled = -1;
	if (enabled < 0) {
		const char* v = getenv("DARLING_NATIVE_THREAD_TRACE");
		enabled = (v != NULL && v[0] == '1') ? 1 : 0;
	}
	if (!enabled) {
		return;
	}

	static const char hex[] = "0123456789abcdef";
	char buf[128];
	int at = 0;

	for (const char* p = "[native-thread-identity "; *p != '\0' && at < 100; ++p) {
		buf[at++] = *p;
	}
	for (const char* p = tag; *p != '\0' && at < 100; ++p) {
		buf[at++] = *p;
	}
	for (int value = 0; value < 2; ++value) {
		unsigned long n = (value == 0) ? a : b;
		buf[at++] = (value == 0) ? ' ' : ',';
		for (int i = 60; i >= 0; i -= 4) {
			buf[at++] = hex[(n >> i) & 0xf];
		}
	}
	buf[at++] = ']';
	buf[at++] = '\n';
	(void)!write(2, buf, (size_t)at);
}

/* The guest TSD base this thread gets: a zeroed dthread block, initialized by the SAME primitive mldr uses for its
 * own guest threads so that TSD slot 0 (pthread self) and slot 1 (errno) are this thread's own. Slot 3
 * (__TSD_MACH_THREAD_SELF) is left at zero: no identity of its own, and never another thread's live token. */
static dthread_t native_thread_tsd_create(void)
{
	dthread_t dthread = calloc(1, sizeof(struct _dthread));
	if (dthread == NULL) {
		return NULL;
	}

	__darling_dthread_initialize(dthread, 0, NULL, 0, NULL, 0);
	return dthread;
}

void __darling_native_thread_install_tsd(void)
{
	dthread_t dthread = native_thread_tsd_create();
	if (dthread == NULL) {
		/* Loud, never silent: without the block this thread keeps the CREATOR'S GS base, which is the defect. */
		native_thread_trace("tsd-allocation-FAILED", 0, 0);
#if defined(__i386__)
		/* Do not enter native code with the creator's guest identity. */
		static const char message[] = "Cannot allocate i386 native-thread TSD\n";
		(void)!write(2, message, sizeof(message) - 1);
		abort();
#endif
		return;
	}

	int rc = __darling_dthread_set_tsd_base(&dthread->tsd[0]);
	unsigned long slot3 = 0;
#if defined(__x86_64__)
	/* The guest ABI's TSD register on x86_64 is GS; on i386 it is FS (dthreads.c installs it with
	 * set_thread_area), so the proof-of-install read is x86_64-only rather than a wrong read elsewhere. */
	__asm__ volatile("movq %%gs:24, %0" : "=r"(slot3));
#endif
	native_thread_trace((rc == 0) ? "tsd-installed" : "tsd-install-FAILED", (unsigned long)rc, slot3);
#if defined(__i386__)
	if (rc != 0) {
		static const char message[] = "Cannot install i386 native-thread TSD\n";
		(void)!write(2, message, sizeof(message) - 1);
		abort();
	}
#endif
}

static void* native_thread_entry(void* p)
{
	struct native_thread_arg box = *(struct native_thread_arg*)p;
	free(p);

	__darling_native_thread_install_tsd();

	return box.start_routine(box.arg);
}

static host_pthread_create_t resolve_host_pthread_create(void)
{
	void* symbol = dlsym(RTLD_NEXT, "pthread_create");
	if (symbol == NULL) {
		/* No definition after mldr in the global scope: ask libc explicitly rather than failing. */
		void* libc = dlopen("libc.so.6", RTLD_NOW | RTLD_NOLOAD);
		if (libc == NULL) {
			libc = dlopen("libc.so.6", RTLD_NOW);
		}
		if (libc != NULL) {
			symbol = dlsym(libc, "pthread_create");
		}
	}
	return (host_pthread_create_t)symbol;
}

__attribute__((constructor))
static void native_thread_interpose_init(void)
{
	/* Resolved eagerly so the interposer never depends on the loader being finished with anything but itself. */
	t_real_pthread_create = resolve_host_pthread_create();
	if (t_real_pthread_create == NULL) {
		native_thread_trace("host-pthread_create-UNRESOLVED", 0, 0);
	}
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine)(void*), void* arg)
{
	if (t_real_pthread_create == NULL) {
		t_real_pthread_create = resolve_host_pthread_create();
		if (t_real_pthread_create == NULL) {
			native_thread_trace("host-pthread_create-UNRESOLVED", 0, 0);
			return EAGAIN;
		}
	}

	if (t_in_darling_thread_create) {
		return t_real_pthread_create(thread, attr, start_routine, arg);
	}

	struct native_thread_arg* box = malloc(sizeof(*box));
	if (box == NULL) {
		/* This wrapper exists for identity, not to gate thread creation: under memory pressure create the thread
		 * the old way (it inherits the creator's identity) rather than failing the caller, and say so. */
		native_thread_trace("wrapper-allocation-FAILED", 0, 0);
		return t_real_pthread_create(thread, attr, start_routine, arg);
	}

	box->start_routine = start_routine;
	box->arg = arg;

	int rc = t_real_pthread_create(thread, attr, native_thread_entry, box);
	if (rc != 0) {
		free(box);
	}
	return rc;
}
