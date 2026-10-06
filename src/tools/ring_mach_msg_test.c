/*
 * This file is part of Darling.
 *
 * Copyright (C) 2021 Darling developers
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

// perf#26 RING-MACH-MSG: the deterministic guest workload for the Ring mach_msg path.
//
// It exists because the boot workload is not a test: it uses whatever IPC launchd happens to perform,
// it cannot delay a sender, and it cannot be repeated. This binary drives a REAL blocking
// mach_msg_overwrite (allocate a receive right, park the receiver, send from a second thread after a
// controlled delay) so every property of the transport is observable on demand.
//
// Build: a guest Mach-O built by the ordinary Darling cross build (add_darling_executable in
// src/tools/CMakeLists.txt), NOT by anything inside the prefix -- the prefix needs no compiler.
// Run:   darling --rootless shell /bin/sh -c '/usr/bin/ring_mach_msg_test <mode> ...'
//
// Modes:
//   basic    <iters>                 immediate reply, blocking receive
//   selfdrop <iters>                 same-thread send+receive, then destroy the port (no helper thread)
//   threadnoop <iters>               create+join a thread that does NOTHING (thread lifecycle alone)
//   bsysloop <iters>                 call getpid() N times: calibration for the Darwin-syscall tracer
//   delay    <ms> <iters>            sender delayed N ms (>=5000 exercises the >3s production wait)
//   timeout  <ms> <iters>            receive with MACH_RCV_TIMEOUT and NO sender: must time out
//   ool      <iters>                 out-of-line message: drives the caller-local mmap/munmap S2C
//   lane_hold <threads>              L2: N simultaneously-live Ring lanes, SEQUENTIAL create, then 8s hold
//   pthread_live <threads> [stack_kib] dar-dles: N simultaneously-live pthreads, each parked on one release
//                                    barrier, sequential create, then release+join; NO Mach IPC (thread
//                                    lifecycle alone -- the failing create and its live-thread count are printed)
//   stress_pool  <receivers> <iters> S1: persistent receiver/sender PAIRS, high concurrency, no churn
//   stress_churn <iters>             S2: persistent receiver, a NEW sender thread per operation
//   bench_simple <iters>             P1: persistent threads/ports, per-op timing, setup outside the timer
//   bench_ool    <iters>             P2: same, with out-of-line messages (caller-S2C)
//   sem_ready    <iters>             the signal is already there: plain success, no park
//   sem_block    <ms> <iters>        the caller parks FIRST and the signal arrives while it waits
//   sem_gap      <ms> <iters>        the same with a gap far longer than any internal transport timeout
//   sem_timed    <ms> <iters>        NO signal: the SEMANTIC timeout must decide, not the transport
//   sem_wait_signal        <iters>   the *_signal variant (both halves in one call)
//   sem_timedwait_signal <ms> <iters> the timed *_signal variant, timeout decides
//
// The two stress modes are deliberately separate. `stress` used to mix concurrency with historical
// thread churn, which made a hang impossible to attribute: S1 keeps the thread count fixed so a failure
// is a concurrency/lane-lifecycle bug, S2 churns short-lived threads so a failure is a lane
// exhaustion/reuse bug.
//
// Every mode prints one machine-readable line and exits non-zero on any failure. A monitor thread turns a
// hang into a named stall dump instead of an opaquely killed process.


/* RAW LINUX IDENTITIES. The guest's emulated getpid() reports a Darling-side pid, which is NOT what an
 * outside observer sees in /proc, and that mismatch has already sent one watch loop to the wrong process.
 * A raw syscall instruction in a Darling guest is LINUX-numbered and goes straight to the host kernel, so
 * these two return the host's own values: 39 is getpid and 186 is gettid on x86_64. Diagnostic only. */
/* TEST-ONLY FAULT WITNESS (DARLING_TEST_FAULT_WITNESS=1).
 * The real failures never reach any Darling-side handler -- an install for SIGSEGV is accepted by the kernel
 * (measured: ret=0) and the process still dies with SigCgt bit 11 clear -- so this bypasses Darling entirely:
 * a RAW Linux rt_sigaction (syscall 13) is issued straight to the host kernel, and the handler writes with a
 * RAW write (syscall 1), formats with no libc and touches no Darling RPC. It prints the signal, si_code and
 * si_addr, plus a bounded raw dump of the ucontext so the faulting rip/rsp can be read off even though this
 * binary is a Darwin guest and has no Linux ucontext header. It then restores the default action and returns,
 * so the process dies faithfully of the same fault. Diagnostic instrument only. */
struct fw_sigaction {
	void (*handler)(int, void*, void*);
	unsigned long flags;
	void (*restorer)(void);
	unsigned long mask;
};

static long fw_sys3(long n, long a, long b, long c)
{
	long r;
	register long r10 __asm__("r10") = c;
	__asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(r10) : "rcx", "r11", "memory");
	return r;
}
static long fw_sys4(long n, long a, long b, long c, long d)
{
	long r;
	register long r10 __asm__("r10") = c;
	register long r8 __asm__("r8") = d;
	__asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(r10), "r"(r8) : "rcx", "r11", "memory");
	return r;
}
static long fw_write2(const char* p, unsigned long n) { return fw_sys3(1, 2, (long)p, (long)n); }
static unsigned long fw_str(char* d, const char* s) { unsigned long n = 0; while (s[n]) { d[n] = s[n]; ++n; } return n; }
static unsigned long fw_hex(char* d, unsigned long v, int width)
{
	static const char* dig = "0123456789abcdef";
	unsigned long got = 0;
	for (int i = (width - 1) * 4; i >= 0; i -= 4) { d[got++] = dig[(v >> i) & 0xf]; }
	return got;
}
static unsigned long fw_dec(char* d, long v)
{
	char tmp[24]; int n = 0; unsigned long got = 0;
	if (v == 0) { d[got++] = '0'; return got; }
	long a = (v < 0) ? -v : v;
	while (a > 0 && n < 23) { tmp[n++] = (char)('0' + (a % 10)); a /= 10; }
	if (v < 0) d[got++] = '-';
	while (n > 0) d[got++] = tmp[--n];
	return got;
}
static unsigned long fw_ull(char* d, unsigned long v) { return fw_dec(d, (long)v); }

static void fw_handler(int sig, void* info, void* uctx)
{
	char buf[1024];
	unsigned long n = 0;
	long* si = (long*)info;             /* siginfo: signo, errno, code, then the union (si_addr at word 2 of it) */
	unsigned long addr = 0;
	if (si != 0) { addr = (unsigned long)si[2 + 2]; }

	n += fw_str(buf + n, "FAULT-WITNESS sig=");
	n += fw_dec(buf + n, sig);
	n += fw_str(buf + n, " host_pid=");
	n += fw_dec(buf + n, fw_sys3(39, 0, 0, 0));
	n += fw_str(buf + n, " host_tid=");
	n += fw_dec(buf + n, fw_sys3(186, 0, 0, 0));
	n += fw_str(buf + n, " si_code=");
	n += fw_dec(buf + n, si ? si[2] : -1);
	n += fw_str(buf + n, " si_addr=0x");
	n += fw_hex(buf + n, addr, 16);
	/* bounded raw dump of the ucontext: this binary has no Linux ucontext layout, so the words are printed
	 * and the faulting register set is read off afterwards rather than guessed here. */
	if (uctx != 0 && n + 8 * 40 < sizeof(buf)) {
		n += fw_str(buf + n, " uctx=");
		for (int i = 0; i < 40; ++i) {
			n += fw_hex(buf + n, ((unsigned long*)uctx)[i], 16);
			buf[n++] = (i == 39) ? '\n' : ',';
		}
	} else {
		buf[n++] = '\n';
	}
	fw_write2(buf, n);

	/* faithful death: restore the default action and return, so the original instruction faults again */
	struct fw_sigaction dfl;
	dfl.handler = (void (*)(int, void*, void*))0; /* SIG_DFL */
	dfl.flags = 0x04000000 /* SA_RESTORER */;
	dfl.restorer = 0;
	dfl.mask = 0;
	fw_sys4(13, sig, (long)&dfl, 0, 8);
}

/* SA_RESTORER stub: the kernel jumps here when the handler returns, and rt_sigreturn (15) restores the frame. */
__attribute__((naked)) void fw_restorer(void) { __asm__ volatile("syscall" : : "a"(15L) : "rcx", "r11", "memory"); }

static void fw_install(int sig)
{
	struct fw_sigaction sa;
	extern void fw_restorer(void);
	sa.handler = fw_handler;
	sa.flags = 0x4 /* SA_SIGINFO */ | 0x04000000 /* SA_RESTORER */;
	sa.restorer = fw_restorer;
	sa.mask = 0;
	fw_sys4(13, sig, (long)&sa, 0, 8);
}

static long raw_host_getpid(void)
{
	long r;
	__asm__ volatile("syscall" : "=a"(r) : "a"(39L) : "rcx", "r11", "memory");
	return r;
}
static long raw_host_gettid(void)
{
	long r;
	__asm__ volatile("syscall" : "=a"(r) : "a"(186L) : "rcx", "r11", "memory");
	return r;
}

#include <mach/mach.h>
#include <mach/message.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
extern char** environ;
#include <time.h>
#include <errno.h>
#include <sys/mman.h>

#define MSG_ID_SIMPLE 0x1234u
#define MSG_ID_OOL    0x2000u
#define SIMPLE_MAGIC  0xC0FFEEu
#define OOL_TAG       0xBEEFu
#define OOL_SIZE      (64u * 1024u)
#define MAX_WORKERS   1024  // 2*512: the pool gates need >128 CONCURRENT receivers

// NOTE: mach_absolute_time(), NOT clock_gettime. The guest's CLOCK_MONOTONIC here is coarse enough to
// quantise by ~1s, which made a 300ms timeout measure negative while the Mach result was correct.
static unsigned long long now_ns(void) {
	static double ticks_per_second = 0;
	if (ticks_per_second == 0) {
		mach_timebase_info_data_t tb;
		mach_timebase_info(&tb);
		ticks_per_second = 1e9 / ((double)tb.numer / (double)tb.denom);
	}
	return (unsigned long long)((double)mach_absolute_time() / ticks_per_second * 1e9);
}

static double now_s(void) { return (double)now_ns() / 1e9; }

// --- worker progress telemetry -----------------------------------------------------------------------
// Workers publish an atomic (phase, op, tid, last-progress) tuple; a monitor thread reads it. The
// failure counter is _Atomic because every worker increments it: a plain int here was a data race, and
// diagnosing the transport on a test with UB is not a diagnosis.
typedef enum {
	PH_INIT = 0, PH_PORT_READY, PH_SENDER_WAIT, PH_RING_PUBLISH, PH_RECEIVER_WAIT,
	PH_SENDER_SENT, PH_RECEIVE_DONE, PH_JOIN_DONE, PH_PORT_DROP, PH_DONE
} phase_t;

static const char* phase_name(int p) {
	static const char* n[] = { "INIT", "PORT_READY", "SENDER_WAIT", "RING_PUBLISH", "RECEIVER_WAIT",
	                           "SENDER_SENT", "RECEIVE_DONE", "JOIN_DONE", "PORT_DROP", "DONE" };
	return (p >= 0 && p <= (int)PH_DONE) ? n[p] : "?";
}

// Plain storage + __atomic_* builtins, not the _Atomic qualifier: the guest's clang rejects
// __atomic_fetch_add on an _Atomic-qualified scalar ("address argument ... must be a pointer to
// integer"), and the builtins are the same discipline the guest's own ring code uses.
typedef struct {
	int                phase;
	unsigned           op;
	unsigned           tid;
	unsigned long long last_ns;
} wstate_t;

static wstate_t         g_workers[MAX_WORKERS];
static unsigned         g_workers_live;
static int              g_monitor_stop;
static unsigned         g_failures;
static volatile int     g_watchdog_seconds;

static unsigned self_tid(void) {
	uint64_t tid = 0;
	pthread_threadid_np(NULL, &tid);
	return (unsigned)tid;
}

static void wset(unsigned idx, int phase, unsigned op) {
	if (idx >= MAX_WORKERS) return;
	g_workers[idx].phase = phase;
	g_workers[idx].op = op;
	g_workers[idx].tid = self_tid();
	g_workers[idx].last_ns = now_ns();
}

// Only the slots a run actually uses may be watched. An untouched slot has last_ns == 0, so its age is
// "since boot" and the monitor would fire instantly -- that is a watchdog bug, not a stall.
static void init_worker_slots(unsigned* used, unsigned count) {
	for (unsigned i = 0; i < MAX_WORKERS; ++i) {
		g_workers[i].phase = PH_DONE;
		g_workers[i].op = 0;
		g_workers[i].tid = 0;
		g_workers[i].last_ns = now_ns();
	}
	for (unsigned i = 0; i < count; ++i) {
		g_workers[used[i]].phase = PH_INIT;
		g_workers[used[i]].last_ns = now_ns();
	}
}

static void* monitor_main(void* unused) {
	(void)unused;
	for (;;) {
		if (g_monitor_stop) return NULL;
		usleep(500 * 1000);
		unsigned live = g_workers_live;
		double worst_age = 0;
		for (unsigned i = 0; i < live && i < MAX_WORKERS; ++i) {
			if (g_workers[i].phase == PH_DONE) continue;
			double age = (double)(now_ns() - g_workers[i].last_ns) / 1e9;
			if (age > worst_age) worst_age = age;
		}
		if (g_watchdog_seconds > 0 && worst_age > (double)g_watchdog_seconds) {
			unsigned long long now = now_ns();
			printf("RING_MACH_TEST_STALL age=%.1f\n", worst_age);
			for (unsigned i = 0; i < live && i < MAX_WORKERS; ++i) {
				if (g_workers[i].phase == PH_DONE) continue;
				printf("WATCHDOG worker=%u op=%u phase=%s tid=%u age=%.1f\n", i, g_workers[i].op,
				       phase_name(g_workers[i].phase), g_workers[i].tid,
				       (double)(now - g_workers[i].last_ns) / 1e9);
			}
			fflush(stdout);
			_exit(3);
		}
	}
}

static void start_monitor(unsigned long long* monitor_thread_out) {
	(void)monitor_thread_out;
	pthread_t mon;
	pthread_create(&mon, NULL, monitor_main, NULL);
}

// --- message send/receive ----------------------------------------------------------------------------

typedef struct {
	mach_port_t q;
	int delay_ms;
	int ool;
	unsigned index;
	int send_failed;
} sender_arg_t;

typedef struct {
	mach_msg_header_t h;
	uint32_t body;
} simple_msg_t;

typedef struct {
	mach_msg_header_t h;
	mach_msg_body_t body;
	mach_msg_ool_descriptor_t ool;
	uint32_t tag;
} ool_msg_t;

// Send one message. `index` is the operation identity: it goes into the payload so the receiver can
// prove THIS message is the one it expected, which is what makes a mismatched or duplicated delivery
// visible instead of merely slow.
static int send_one(mach_port_t q, unsigned index, int ool) {
	if (!ool) {
		simple_msg_t m;
		memset(&m, 0, sizeof(m));
		m.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
		m.h.msgh_size = (mach_msg_size_t)sizeof(m);
		m.h.msgh_remote_port = q;
		m.h.msgh_local_port = MACH_PORT_NULL;
		m.h.msgh_id = MSG_ID_SIMPLE;
		m.body = SIMPLE_MAGIC + index;
		kern_return_t kr = mach_msg(&m.h, MACH_SEND_MSG, (mach_msg_size_t)sizeof(m), 0,
		                            MACH_PORT_NULL, 0, MACH_PORT_NULL);
		if (kr != KERN_SUCCESS) {
			printf("SEND_FAIL op=%u kr=%d\n", index, kr);
			return 0;
		}
		return 1;
	}
	// An out-of-line region: the receiver's copyout has to give the guest pages for it, which is exactly
	// the caller-local anonymous mmap, and releasing the previous region's pages is the caller-local
	// munmap. Those two are the S2C shapes the duplex mailbox must carry.
	void* region = malloc(OOL_SIZE);
	if (!region) return 0;
	memset(region, 0xAB, OOL_SIZE);
	ool_msg_t m;
	memset(&m, 0, sizeof(m));
	m.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0) | MACH_MSGH_BITS_COMPLEX;
	m.h.msgh_size = (mach_msg_size_t)sizeof(m);
	m.h.msgh_remote_port = q;
	m.h.msgh_local_port = MACH_PORT_NULL;
	m.h.msgh_id = MSG_ID_OOL;
	m.body.msgh_descriptor_count = 1;
	m.ool.address = region;
	m.ool.size = OOL_SIZE;
	m.ool.deallocate = 0;
	m.ool.copy = MACH_MSG_VIRTUAL_COPY;
	m.ool.type = MACH_MSG_OOL_DESCRIPTOR;
	m.tag = OOL_TAG + index;
	kern_return_t kr = mach_msg(&m.h, MACH_SEND_MSG, (mach_msg_size_t)sizeof(m), 0,
	                            MACH_PORT_NULL, 0, MACH_PORT_NULL);
	free(region);
	if (kr != KERN_SUCCESS) {
		printf("SEND_OOL_FAIL op=%u kr=%d\n", index, kr);
		return 0;
	}
	return 1;
}

static void* sender_main(void* raw) {
	sender_arg_t* a = (sender_arg_t*)raw;
	if (a->delay_ms > 0) usleep((useconds_t)a->delay_ms * 1000);
	if (!send_one(a->q, a->index, a->ool)) a->send_failed = 1;
	return NULL;
}

static int do_receive(mach_port_t q, int with_timeout, int timeout_ms, unsigned index, int ool,
                      double* out_elapsed, kern_return_t* out_kr) {
	union {
		simple_msg_t simple;
		ool_msg_t ool;
		char raw[4096];
	} rcv;
	memset(&rcv, 0, sizeof(rcv));
	int opt = MACH_RCV_MSG | (with_timeout ? MACH_RCV_TIMEOUT : 0);
	double t0 = now_s();
	kern_return_t kr = mach_msg(&rcv.simple.h, opt, 0, (mach_msg_size_t)sizeof(rcv), q,
	                            with_timeout ? (mach_msg_timeout_t)timeout_ms : 0, MACH_PORT_NULL);
	if (out_elapsed) *out_elapsed = now_s() - t0;
	if (out_kr) *out_kr = kr;
	if (with_timeout) return kr == MACH_RCV_TIMED_OUT;
	if (kr != KERN_SUCCESS) {
		printf("RCV_FAIL op=%u kr=%d\n", index, kr);
		return 0;
	}
	if (!ool) {
		if (rcv.simple.body != SIMPLE_MAGIC + index) {
			printf("RCV_BODY_MISMATCH op=%u got=%u want=%u\n", index, rcv.simple.body, SIMPLE_MAGIC + index);
			return 0;
		}
		return 1;
	}
	if (rcv.ool.tag != OOL_TAG + index) {
		printf("RCV_OOL_TAG_MISMATCH op=%u got=%u want=%u\n", index, rcv.ool.tag, OOL_TAG + index);
		return 0;
	}
	if (rcv.ool.body.msgh_descriptor_count != 1 || rcv.ool.ool.type != MACH_MSG_OOL_DESCRIPTOR) {
		printf("RCV_OOL_SHAPE_BAD op=%u count=%u type=%u\n", index, rcv.ool.body.msgh_descriptor_count, rcv.ool.ool.type);
		return 0;
	}
	// The payload must be reachable through the descriptor's address: this is the check that a truncated
	// (32-bit) mmap return value would fail, because the guest would be handed an address it cannot read.
	if (rcv.ool.ool.address == NULL || rcv.ool.ool.size != OOL_SIZE) {
		printf("RCV_OOL_DESC_BAD op=%u addr=%p size=%u\n", index, rcv.ool.ool.address, rcv.ool.ool.size);
		return 0;
	}
	unsigned char* p = (unsigned char*)rcv.ool.ool.address;
	if (p[0] != 0xAB || p[OOL_SIZE - 1] != 0xAB) {
		printf("RCV_OOL_DATA_BAD op=%u first=%u last=%u\n", index, p[0], p[OOL_SIZE - 1]);
		return 0;
	}
	return 1;
}

static void make_port(mach_port_t* q) {
	if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, q) != KERN_SUCCESS) {
		printf("ALLOC_FAIL\n");
		exit(2);
	}
	if (mach_port_insert_right(mach_task_self(), *q, *q, MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
		printf("INSERT_FAIL\n");
		exit(2);
	}
}

static void drop_port(mach_port_t q) {
	mach_port_mod_refs(mach_task_self(), q, MACH_PORT_RIGHT_RECEIVE, -1);
}

// --- S1: fixed-pool concurrency ----------------------------------------------------------------------
// N persistent receiver threads, each owning ONE port, and N persistent sender threads, one per
// receiver. The thread count does not change during the run, so any failure here is a concurrency,
// wake, or lane-lifecycle bug -- never thread churn.

typedef struct {
	unsigned    idx;      // receiver index; the sender for it registers at MAX_WORKERS/2 + idx
	mach_port_t q;
	unsigned    iters;
	unsigned    ool_every; // 0 == never; every Nth op is out-of-line
	int         random_delay; // stress: stagger senders so receivers really park. bench: 0 == none, the
	                          // delay would otherwise dominate the measurement entirely.
	double*     durations; // per-op durations, owned by the receiver (NULL when not benchmarking)
} pool_arg_t;

static unsigned pool_op_id(unsigned idx, unsigned i, unsigned iters) { return idx * iters + i; }

static void* pool_receiver(void* raw) {
	pool_arg_t* a = (pool_arg_t*)raw;
	for (unsigned i = 0; i < a->iters; ++i) {
		int ool = a->ool_every && ((i % a->ool_every) == (a->ool_every - 1));
		unsigned op = pool_op_id(a->idx, i, a->iters);
		wset(a->idx, PH_RECEIVER_WAIT, op);
		double el = 0;
		kern_return_t kr = KERN_SUCCESS;
		if (!do_receive(a->q, 0, 0, op, ool, &el, &kr)) {
			__atomic_fetch_add(&g_failures, 1, __ATOMIC_RELAXED);
		}
		if (a->durations) a->durations[i] = el;
		wset(a->idx, PH_RECEIVE_DONE, op);
	}
	wset(a->idx, PH_DONE, 0);
	return NULL;
}

static void* pool_sender(void* raw) {
	pool_arg_t* a = (pool_arg_t*)raw;
	unsigned slot = MAX_WORKERS / 2 + a->idx;
	for (unsigned i = 0; i < a->iters; ++i) {
		int ool = a->ool_every && ((i % a->ool_every) == (a->ool_every - 1));
		unsigned op = pool_op_id(a->idx, i, a->iters);
		wset(slot, PH_SENDER_WAIT, op);
		// Randomized-ish, deterministic delay so the receiver genuinely parks and completions arrive out
		// of order across lanes. Off for benchmarks: it would be the measurement, not the transport.
		if (a->random_delay) usleep((useconds_t)((op * 37u) % 200u) * 1000u);
		wset(slot, PH_SENDER_SENT, op);
		if (!send_one(a->q, op, ool)) __atomic_fetch_add(&g_failures, 1, __ATOMIC_RELAXED);
	}
	wset(slot, PH_DONE, 0);
	return NULL;
}

// --- L2: lane CAPACITY gate --------------------------------------------------------------------------
// N threads that are ALL ALIVE SIMULTANEOUSLY, each holding its own Ring lane.
//
// WHY this mode exists rather than reusing stress_pool: making 2N threads CONCURRENTLY intermittently
// deadlocks Darling's pthread create/join handshake (server callnum 62 semaphore_wait) -- measured to
// reproduce on the pristine UDS path with the Ring hatch off, so it is a thread-lifecycle bug, not a
// transport one. Creation here is therefore SEQUENTIAL: the main thread waits for worker i to be up
// before creating worker i+1. That keeps N lanes LIVE at once without racing pthread_create, which is
// exactly the capacity claim -- "more than 128 simultaneous lanes", not "more than 128 concurrent
// pthread_create calls".
//
// The measurement is deliberately OUTSIDE this process: the gate prints LANE_HOLD_READY and then holds
// every thread for 8 seconds, and the harness samples the SERVER's own per-thread ring state in that
// window. Two independent sides (guest lanes held, server lanes live) agreeing is what makes it evidence.
static volatile int g_hold_ready = 0; // workers that finished phase 1 (lane attached)
static volatile int g_hold_go = 0;    // main sets once every worker is held

typedef struct {
	mach_port_t q;
	unsigned    idx;
} hold_arg_t;

static int hold_roundtrip(hold_arg_t* a, unsigned index) {
	sender_arg_t s = { a->q, 0, 0, index, 0 };
	sender_main(&s); // send to our OWN port, then receive from it: no helper thread, so the thread count
	                 // is exactly N (a helper per op would double the lane requirement and reintroduce
	                 // concurrent creation)
	if (s.send_failed) return 0;
	double el = 0;
	kern_return_t kr = KERN_SUCCESS;
	return do_receive(a->q, 0, 0, index, 0, &el, &kr);
}

static void* hold_worker(void* raw) {
	hold_arg_t* a = raw;
	wset(a->idx, PH_INIT, 0);
	if (!hold_roundtrip(a, a->idx * 2u)) __atomic_add_fetch(&g_failures, 1, __ATOMIC_RELAXED);
	__atomic_add_fetch(&g_hold_ready, 1, __ATOMIC_RELEASE);
	// park until every other lane is held (keeping our watchdog slot fresh: parking is progress, not a stall)
	while (!__atomic_load_n(&g_hold_go, __ATOMIC_ACQUIRE)) {
		wset(a->idx, PH_RECEIVER_WAIT, 1);
		usleep(50000);
	}
	wset(a->idx, PH_RING_PUBLISH, 2);
	if (!hold_roundtrip(a, a->idx * 2u + 1u)) __atomic_add_fetch(&g_failures, 1, __ATOMIC_RELAXED);
	// NO JOIN, and the worker never exits on its own: mass pthread_join/exit is the same fragile
	// thread-lifecycle path this mode exists to avoid. The worker parks (keeping its lane) and the main
	// thread ends the PROCESS once it has recorded the result, which tears every lane down at once.
	wset(a->idx, PH_DONE, 2);
	for (;;) {
		usleep(200000);
	}
	return NULL;
}

static int run_lane_hold(unsigned n) {
	if (n == 0 || n > MAX_WORKERS) {
		printf("RING_MACH_TEST mode=lane_hold pass=0 error=bad_receivers\n");
		return 2;
	}
	start_monitor(NULL);
	{
		unsigned used[MAX_WORKERS];
		for (unsigned i = 0; i < n; ++i) used[i] = i;
		init_worker_slots(used, n);
	}
	__atomic_store_n(&g_workers_live, n, __ATOMIC_RELEASE);
	pthread_t*   th = calloc(n, sizeof(*th));
	hold_arg_t* ar = calloc(n, sizeof(*ar));
	// Small stacks: the hold gate keeps every worker ALIVE, so stack VA is the one resource it scales with,
	// and a failed creation must be visible as a failure rather than as a hang.
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 256u * 1024u);
	for (unsigned i = 0; i < n; ++i) {
		make_port(&ar[i].q);
		ar[i].idx = i;
		int cr = pthread_create(&th[i], &attr, hold_worker, &ar[i]);
		if (cr != 0) {
			printf("LANE_HOLD_CREATE_FAIL index=%u err=%d\n", i, cr);
			fflush(stdout);
			break;
		}
		unsigned spins = 0;
		while (__atomic_load_n(&g_hold_ready, __ATOMIC_ACQUIRE) < (int)(i + 1)) {
			usleep(1000);
			if (++spins > 120000) break; // 120s bound: the watchdog reports the real stall
		}
		if ((i + 1) % 16 == 0 || i + 1 == n) {
			printf("LANE_HOLD_PROGRESS ready=%u of=%u created=%u\n",
			       (unsigned)__atomic_load_n(&g_hold_ready, __ATOMIC_ACQUIRE), n, i + 1);
			fflush(stdout);
		}
	}
	unsigned ready = (unsigned)__atomic_load_n(&g_hold_ready, __ATOMIC_ACQUIRE);
	unsigned failures = (unsigned)__atomic_load_n(&g_failures, __ATOMIC_RELAXED);
	printf("LANE_HOLD_READY n=%u ready=%u phase1_pass=%d\n", n, ready, failures == 0);
	fflush(stdout);
	sleep(10); // the harness samples the server's live lanes here
	__atomic_store_n(&g_hold_go, 1, __ATOMIC_RELEASE);
	// give every worker time to complete phase 2 (a real round trip on its own lane), then report
	unsigned spins = 0;
	while (spins++ < 200) {
		unsigned done = 0;
		for (unsigned i = 0; i < n; ++i) {
			if (g_workers[i].phase == PH_DONE) ++done;
		}
		if (done == n) break;
		usleep(50000);
	}
	failures = (unsigned)__atomic_load_n(&g_failures, __ATOMIC_RELAXED);
	{
		unsigned done = 0;
		for (unsigned i = 0; i < n; ++i) if (g_workers[i].phase == PH_DONE) ++done;
		printf("RING_MACH_TEST mode=lane_hold threads=%u iters=2 pass=%d failures=%u phase2_done=%u\n",
		       n, failures == 0, failures, done);
	}
	fflush(stdout);
	(void)th; (void)ar; // the process exit tears the held lanes down; no join, no drop_port (see above)
	__atomic_store_n(&g_monitor_stop, 1, __ATOMIC_RELEASE);
	return failures ? 1 : 0;
}

// --- dar-dles: simultaneously-live pthread create/start barrier -------------------------------------
// The defect (dar-dles): with ~50 or more SIMULTANEOUSLY LIVE guest threads, pthread_create stops
// returning -- it neither completes nor fails, it simply never returns. This mode holds exactly N
// threads alive, each parked on ONE explicit release barrier, and the creator records for every create
// its attempt index, its return code, and the number of workers already live. It performs NO Mach IPC
// at all, so the only thing it exercises is the pthread create/start handshake: a stall here is that
// handshake's transition, not the transport.
//
// Creation is SEQUENTIAL (the creator waits for worker i to signal started before creating i+1), so
// exactly one create is in flight at a time while N threads are simultaneously alive -- the shape the
// defect is measured on.
static volatile int g_pl_started; // workers that have signalled "started"
static volatile int g_pl_release; // creator sets once every worker must exit

static void* pl_worker(void* raw) {
	unsigned idx = *(unsigned*)raw;
	wset(idx, PH_RECEIVER_WAIT, 0);
	__atomic_add_fetch(&g_pl_started, 1, __ATOMIC_RELEASE);
	// park on the one explicit barrier until the creator releases us; parking is progress, so the
	// watchdog slot is refreshed while we wait
	while (!__atomic_load_n(&g_pl_release, __ATOMIC_ACQUIRE)) {
		wset(idx, PH_RECEIVER_WAIT, 0);
		usleep(20000);
	}
	wset(idx, PH_DONE, 0);
	return NULL;
}

static int run_pthread_live(unsigned n, unsigned stack_kib) {
	if (n == 0 || n > MAX_WORKERS) {
		printf("PTHREAD_LIVE_BAD n=%u\n", n);
		return 2;
	}
	start_monitor(NULL);
	{
		unsigned used[MAX_WORKERS];
		for (unsigned i = 0; i < n; ++i) used[i] = i;
		// Mark every slot PH_DONE; a slot becomes INIT only when its create is actually attempted, so the
		// watchdog names the exact attempt that stopped instead of an untouched planned slot.
		init_worker_slots(used, 0);
	}
	__atomic_store_n(&g_workers_live, n, __ATOMIC_RELEASE);
	pthread_t*     th  = calloc(n, sizeof(*th));
	unsigned*      ids = calloc(n, sizeof(*ids));
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, (size_t)(stack_kib ? stack_kib : 256u) * 1024u);

	unsigned created = 0;
	int first_rc = 0;
	for (unsigned i = 0; i < n; ++i) {
		unsigned live = (unsigned)__atomic_load_n(&g_pl_started, __ATOMIC_ACQUIRE);
		wset(i, PH_INIT, 0);
		printf("PTHREAD_LIVE_CREATE_BEGIN index=%u live=%u\n", i, live);
		fflush(stdout);
		ids[i] = i;
		int rc = pthread_create(&th[i], &attr, pl_worker, &ids[i]);
		printf("PTHREAD_LIVE_CREATE_DONE index=%u rc=%d live=%u created=%u\n", i, rc,
		       (unsigned)__atomic_load_n(&g_pl_started, __ATOMIC_ACQUIRE), created);
		fflush(stdout);
		if (rc != 0) { first_rc = rc; break; }
		++created;
		// wait until THIS worker signalled started: one create in flight, N alive simultaneously
		unsigned spins = 0;
		while ((unsigned)__atomic_load_n(&g_pl_started, __ATOMIC_ACQUIRE) < i + 1) {
			usleep(1000);
			if (++spins > 120000) break; // 120s bound: the watchdog reports the real stall
		}
		if ((unsigned)__atomic_load_n(&g_pl_started, __ATOMIC_ACQUIRE) < i + 1) {
			printf("PTHREAD_LIVE_START_TIMEOUT index=%u live=%u\n", i,
			       (unsigned)__atomic_load_n(&g_pl_started, __ATOMIC_ACQUIRE));
			fflush(stdout);
			break;
		}
		if ((i + 1) % 8 == 0 || i + 1 == n) {
			printf("PTHREAD_LIVE_PROGRESS created=%u live=%u of=%u\n", created,
			       (unsigned)__atomic_load_n(&g_pl_started, __ATOMIC_ACQUIRE), n);
			fflush(stdout);
		}
	}

	unsigned started = (unsigned)__atomic_load_n(&g_pl_started, __ATOMIC_ACQUIRE);
	printf("PTHREAD_LIVE_ALL n=%u created=%u started=%u\n", n, created, started);
	fflush(stdout);

	// release every worker and join them cleanly (join is part of the handshake the defect is in)
	__atomic_store_n(&g_pl_release, 1, __ATOMIC_RELEASE);
	unsigned joined = 0;
	for (unsigned i = 0; i < created; ++i) {
		pthread_join(th[i], NULL);
		++joined;
	}
	int pass = (first_rc == 0 && created == n && started == n && joined == n) ? 1 : 0;
	printf("RING_MACH_TEST mode=pthread_live n=%u stack_kib=%u created=%u started=%u joined=%u "
	       "first_err=%d pass=%d\n", n, stack_kib, created, started, joined, first_rc, pass);
	fflush(stdout);
	__atomic_store_n(&g_monitor_stop, 1, __ATOMIC_RELEASE);
	return pass ? 0 : 1;
}

static int run_pool(unsigned receivers, unsigned iters, unsigned ool_every, const char* label,
                    int bench, int random_delay) {
	if (receivers == 0 || receivers > MAX_WORKERS / 2) {
		printf("RING_MACH_TEST mode=%s pass=0 error=bad_receivers\n", label);
		return 2;
	}
	start_monitor(NULL);
	pool_arg_t* rg = calloc(receivers, sizeof(*rg));
	pool_arg_t* sg = calloc(receivers, sizeof(*sg));
	pthread_t*  rt = calloc(receivers, sizeof(pthread_t));
	pthread_t*  st = calloc(receivers, sizeof(pthread_t));
	double**    durs = bench ? calloc(receivers, sizeof(double*)) : NULL;
	for (unsigned i = 0; i < receivers; ++i) {
		mach_port_t q;
		make_port(&q); // setup, outside any measurement
		rg[i].idx = i; rg[i].q = q; rg[i].iters = iters; rg[i].ool_every = ool_every;
		if (bench) { durs[i] = calloc(iters, sizeof(double)); rg[i].durations = durs[i]; }
		sg[i].idx = i; sg[i].q = q; sg[i].iters = iters; sg[i].ool_every = ool_every;
		sg[i].random_delay = random_delay;
	}
	__atomic_store_n(&g_workers_live, MAX_WORKERS, __ATOMIC_RELEASE);
	{
		unsigned used[MAX_WORKERS]; unsigned n = 0;
		for (unsigned i = 0; i < receivers; ++i) { used[n++] = i; used[n++] = MAX_WORKERS / 2 + i; }
		init_worker_slots(used, n);
	}
	double t0 = now_s();
	for (unsigned i = 0; i < receivers; ++i) pthread_create(&rt[i], NULL, pool_receiver, &rg[i]);
	for (unsigned i = 0; i < receivers; ++i) pthread_create(&st[i], NULL, pool_sender, &sg[i]);
	for (unsigned i = 0; i < receivers; ++i) pthread_join(rt[i], NULL);
	for (unsigned i = 0; i < receivers; ++i) pthread_join(st[i], NULL);
	double elapsed = now_s() - t0;
	unsigned failures = __atomic_load_n(&g_failures, __ATOMIC_RELAXED);
	if (bench && failures == 0) {
		unsigned total = receivers * iters;
		double* all = calloc(total, sizeof(double));
		unsigned n = 0;
		for (unsigned i = 0; i < receivers; ++i)
			for (unsigned j = 0; j < iters; ++j) all[n++] = durs[i][j];
		for (unsigned i = 1; i < n; ++i) { // insertion sort: tiny n, and no libc qsort dependency guesswork
			double v = all[i]; unsigned j = i;
			while (j > 0 && all[j - 1] > v) { all[j] = all[j - 1]; --j; }
			all[j] = v;
		}
		double sum = 0;
		for (unsigned i = 0; i < n; ++i) sum += all[i];
		printf("RING_MACH_TEST mode=%s pass=1 ops=%u total_s=%.6f ns_per_op=%.1f p50_ns=%.1f p95_ns=%.1f p99_ns=%.1f min_ns=%.1f max_ns=%.1f\n",
		       label, n, elapsed, sum / (double)n * 1e9,
		       all[n / 2] * 1e9, all[(n * 95) / 100] * 1e9, all[(n * 99) / 100] * 1e9,
		       all[0] * 1e9, all[n - 1] * 1e9);
		free(all);
	} else {
		printf("RING_MACH_TEST mode=%s receivers=%u iters=%u pass=%d failures=%u total_s=%.3f\n",
		       label, receivers, iters, failures == 0, failures, elapsed);
	}
	for (unsigned i = 0; i < receivers; ++i) drop_port(rg[i].q);
	if (durs) { for (unsigned i = 0; i < receivers; ++i) free(durs[i]); free(durs); }
	free(rg); free(sg); free(rt); free(st);
	__atomic_store_n(&g_monitor_stop, 1, __ATOMIC_RELEASE);
	return failures ? 1 : 0;
}

// --- S2: historical thread churn ---------------------------------------------------------------------
// ONE persistent receiver and a NEW sender thread per operation. This is the workload that exercises
// lane allocation, lane release, Linux TID reuse and the no-lane fallback, with the concurrency held
// constant at one so a failure cannot be blamed on concurrency.

static int run_churn(unsigned iters) {
	start_monitor(NULL);
	mach_port_t q;
	make_port(&q);
	__atomic_store_n(&g_workers_live, MAX_WORKERS, __ATOMIC_RELEASE);
	{ unsigned used[2] = { 0, 1 }; init_worker_slots(used, 2); }
	for (unsigned i = 0; i < iters; ++i) {
		int ool = (i % 8) == 7;
		sender_arg_t a = { q, 0, ool, i, 0 };
		pthread_t th;
		wset(1, PH_SENDER_WAIT, i);
		pthread_create(&th, NULL, sender_main, &a);
		wset(0, PH_RECEIVER_WAIT, i);
		double el = 0;
		kern_return_t kr = KERN_SUCCESS;
		if (!do_receive(q, 0, 0, i, ool, &el, &kr) || a.send_failed) {
			__atomic_fetch_add(&g_failures, 1, __ATOMIC_RELAXED);
		}
		wset(1, PH_JOIN_DONE, i);
		pthread_join(th, NULL);
		wset(0, PH_RECEIVE_DONE, i);
	}
	wset(0, PH_DONE, 0); wset(1, PH_DONE, 0);
	drop_port(q);
	unsigned failures = __atomic_load_n(&g_failures, __ATOMIC_RELAXED);
	printf("RING_MACH_TEST mode=stress_churn iters=%u pass=%d failures=%u\n", iters, failures == 0, failures);
	__atomic_store_n(&g_monitor_stop, 1, __ATOMIC_RELEASE);
	return failures ? 1 : 0;
}

// --- BSYSLOOP: calibration for the Darwin-syscall tracer -------------------------------------------------------
// perf#30 INSTRUMENT CALIBRATION: the tracer proved it sees the dispatcher entry, but a workload's own thread showed
// only six traced calls and none of them `write` while that thread printed five marks. "The tracer did not see it"
// and "the call never happened" are therefore indistinguishable for a workload binary, so this mode issues a KNOWN
// number of a KNOWN Darwin syscall (`getpid`) and reports the count: the traced run must show at least that many.
static int run_bsysloop(unsigned iters) {
	unsigned long acc = 0;
	for (unsigned i = 0; i < iters; ++i) { acc += (unsigned long)getpid(); }
	printf("RING_MACH_TEST mode=bsysloop iters=%u acc_nonzero=%d pass=1\n", iters, acc != 0);
	return 0;
}

// --- THREADNOOP: the thread lifecycle ALONE ------------------------------------------------------------------
// perf#30 DIAGNOSIS: with the destroy skipped the workload still dies, and the last surviving mark is the second
// iteration's `make_port` -- i.e. the death is in the create/join cycle, not in the port teardown. This mode isolates
// the lifecycle: create a thread that does NOTHING (no port, no message) and join it, N times.
static void* noop_thread(void* raw) {
	(void)raw;
	// WEB: bounded, raw-ish marks: the thread's own entry and exit are the two facts that separate "the new thread
	// never started" from "the creator never returned". fflush after each, because the process dies silently and
	// buffered output is lost with it.
	printf("[noop-thread enter tid=%u]\n", self_tid()); fflush(stdout);
	printf("[noop-thread exit tid=%u]\n", self_tid()); fflush(stdout);
	return NULL;
}

// fsview: WHAT THE GUEST PROCESS ITSELF SEES. MEASURED NEED: probes run through the harness's --cmd were served by the
// HOST's /bin/sh -- their /proc/self/root was "/" and mountinfo listed the host's sysfs/devtmpfs -- so they could not
// answer where a guest-visible fixture lives, and two cycles were spent on conclusions drawn from them. This mode
// reports the answer from inside the guest: the root the loader handed the process, the entries of "/", and the state
// of the candidate fixture paths.
static int run_fsview(void) {
	const char* root = getenv("DYLD_ROOT_PATH");
	const char* priv = getenv("__mldr_DYLD_ROOT_PATH");
	// Deliberately NOT prefixed with "RING_MACH_TEST mode=": a verdict matcher reads the workload's mode line, and
	// two lines sharing that prefix made it read this header instead of the pass=1 result (MEASURED).
	printf("FSVIEW-ENV dyld_root_path=%s mldr_root_path=%s\n",
	       root ? root : "(unset)", priv ? priv : "(unset)");
	DIR* d = opendir("/");
	if (d) {
		struct dirent* e;
		unsigned n = 0;
		printf("FSVIEW root-entries:");
		while ((e = readdir(d)) != NULL && n++ < 40) printf(" %s", e->d_name);
		printf("\n");
		closedir(d);
	} else {
		printf("FSVIEW root-entries: opendir failed\n");
	}
	const char* cands[] = {
		"/usr/bin/ring_mach_msg_test", "/libexec/darling/usr/bin/ring_mach_msg_test",
		"/private/var/tmp/ring_mach_msg_test", "/bin/sh", "/usr/lib/dyld", NULL
	};
	for (int i = 0; cands[i] != NULL; i++) {
		struct stat st;
		printf("FSVIEW stat %s => %d\n", cands[i], stat(cands[i], &st));
	}
	printf("RING_MACH_TEST mode=fsview pass=1\n");
	return 0;
}

static int run_threadnoop(unsigned iters) {
	for (unsigned i = 0; i < iters; ++i) {
		pthread_t th;
		/* ORDINAL AND TIME, NOT A LAST MARK. MEASURED: this defect is a race -- the iteration that stalls moves
		 * between runs (3, then 16, 20, 24, 28, 32, 37 across batches), so "the last mark" answers a different
		 * question every time. Each iteration therefore reports its own index and the two durations that decide
		 * whether the create or the join is the slow one, and a stalled iteration is visible as a large number
		 * rather than as an absence. */
		struct timespec t0, t1, t2;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		printf("[noop %u pre-create]\n", i); fflush(stdout);
		if (pthread_create(&th, NULL, noop_thread, NULL) != 0) {
			printf("RING_MACH_TEST mode=threadnoop iters=%u pass=0 error=create\n", iters); return 1;
		}
		clock_gettime(CLOCK_MONOTONIC, &t1);
		printf("[noop %u post-create]\n", i); fflush(stdout);
		pthread_join(th, NULL);
		clock_gettime(CLOCK_MONOTONIC, &t2);
		double create_ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 + (double)(t1.tv_nsec - t0.tv_nsec) / 1000000.0;
		double join_ms = (double)(t2.tv_sec - t1.tv_sec) * 1000.0 + (double)(t2.tv_nsec - t1.tv_nsec) / 1000000.0;
		printf("[noop %u post-join create_ms=%.2f join_ms=%.2f]\n", i, create_ms, join_ms);
		fflush(stdout);
	}
	printf("RING_MACH_TEST mode=threadnoop iters=%u pass=1\n", iters);
	return 0;
}

// --- SELF-DROP: the destroy AFTER a same-thread round trip, no helper thread ----------------------------------
// perf#30 DIAGNOSIS: `stress_churn`/`basic` die at the receive right's destruction while `timeout` (receive with no
// sender at all) passes, so the ingredient is the round trip -- and the next question is whether it is the MESSAGE
// or the THREAD. This mode sends and receives on the SAME thread (no helper), then destroys the port: it passes
// only if the thread lifecycle is the ingredient.
static int run_selfdrop(unsigned iters) {
	for (unsigned i = 0; i < iters; ++i) {
		mach_port_t q;
		make_port(&q);
		sender_arg_t s = { q, 0, 0, i, 0 };
		sender_main(&s); // send to our OWN port from this thread
		if (s.send_failed) { printf("RING_MACH_TEST mode=selfdrop iters=%u pass=0 error=send\n", iters); return 1; }
		double el = 0;
		kern_return_t kr = KERN_SUCCESS;
		if (!do_receive(q, 0, 0, i, 0, &el, &kr)) { printf("RING_MACH_TEST mode=selfdrop iters=%u pass=0 error=receive\n", iters); return 1; }
		drop_port(q);
	}
	printf("RING_MACH_TEST mode=selfdrop iters=%u pass=1\n", iters);
	return 0;
}

// --- MIXED: the workload that used to hang ------------------------------------------------------------
// Persistent worker threads (concurrency N) where EACH worker allocates a fresh port and creates a fresh
// sender thread per iteration. S1 (concurrency, no churn) and S2 (churn, no concurrency) both pass, so
// this mode exists purely to reproduce and localize the combination that did not.
typedef struct { unsigned idx; unsigned iters; } mixed_arg_t;

static void* mixed_worker(void* raw) {
	mixed_arg_t* a = (mixed_arg_t*)raw;
	for (unsigned i = 0; i < a->iters; ++i) {
		unsigned op = a->idx * a->iters + i;
		mach_port_t q;
		make_port(&q);
		wset(a->idx, PH_PORT_READY, op);
		int ool = (i % 4) == 3;
		sender_arg_t sa = { q, (int)((i % 7) * 3), ool, op, 0 };
		pthread_t th;
		wset(a->idx, PH_SENDER_WAIT, op);
		pthread_create(&th, NULL, sender_main, &sa);
		wset(a->idx, PH_RECEIVER_WAIT, op);
		double el = 0;
		kern_return_t kr = KERN_SUCCESS;
		if (!do_receive(q, 0, 0, op, ool, &el, &kr) || sa.send_failed) {
			__atomic_fetch_add(&g_failures, 1, __ATOMIC_RELAXED);
		}
		wset(a->idx, PH_JOIN_DONE, op);
		pthread_join(th, NULL);
		drop_port(q);
	}
	wset(a->idx, PH_DONE, 0);
	return NULL;
}

static int run_mixed(unsigned workers, unsigned iters) {
	if (workers == 0 || workers > MAX_WORKERS) { printf("RING_MACH_TEST mode=stress_mixed pass=0 error=bad_workers\n"); return 2; }
	start_monitor(NULL);
	mixed_arg_t* args = calloc(workers, sizeof(*args));
	pthread_t* ths = calloc(workers, sizeof(pthread_t));
	unsigned used[MAX_WORKERS];
	for (unsigned i = 0; i < workers; ++i) { args[i].idx = i; args[i].iters = iters; used[i] = i; }
	__atomic_store_n(&g_workers_live, MAX_WORKERS, __ATOMIC_RELEASE);
	init_worker_slots(used, workers);
	double t0 = now_s();
	for (unsigned i = 0; i < workers; ++i) pthread_create(&ths[i], NULL, mixed_worker, &args[i]);
	for (unsigned i = 0; i < workers; ++i) pthread_join(ths[i], NULL);
	unsigned failures = __atomic_load_n(&g_failures, __ATOMIC_RELAXED);
	printf("RING_MACH_TEST mode=stress_mixed workers=%u iters=%u pass=%d failures=%u total_s=%.3f\n",
	       workers, iters, failures == 0, failures, now_s() - t0);
	free(args); free(ths);
	__atomic_store_n(&g_monitor_stop, 1, __ATOMIC_RELEASE);
	return failures ? 1 : 0;
}


// --- perf#30 (directive sections 11-15, doc section 207): the SEMAPHORE family ----------------------
//
// WHY THESE EXIST. The blocking semaphore family (semaphore_wait, semaphore_timedwait,
// semaphore_wait_signal, semaphore_timedwait_signal) moved from the per-thread datagram socket to the
// thread's own Ring lane, and the hard socket-disable oracle is what named them. A migration proven only
// by "the denial went away" would say nothing about the SEMANTICS -- the transport must deliver a final
// semantic reply and must not invent a result or a deadline of its own. These modes exercise exactly
// that: a signal that is already there, a signal that arrives while the caller is parked, a gap far
// longer than any internal transport timeout, a semantic timeout with no signal at all, and the two
// *_signal variants. Every mode prints one machine-readable line with the result AND the elapsed time,
// because for a blocking call the elapsed time is part of the contract.

#include <mach/semaphore.h>
#include <signal.h>
#include <dlfcn.h>
#define _XOPEN_SOURCE 600  // Darwin guards the deprecated ucontext routines behind it
#include <ucontext.h>

// perf#30 DIAGNOSIS (doc 253): A GUEST CRASH MUST DESCRIBE ITSELF. MEASURED: `basic 2`/`basic 3` died of
// SIGSEGV at the second iteration's port teardown, and the only evidence available was the ABSENCE of a mark --
// which is byte-identical to a deadlock and sent the whole investigation after a lock that never existed. The
// host cannot symbolise that death either: the guest runs in its own namespace and its crash never reaches the
// harness log. So the workload reports its own fault: signal, fault address, fault PC, the returning frames and
// the image+symbol each frame belongs to. One line, raw `write`, then the default disposition so the shell still
// sees the true status (128+signal).
static void rmmt_emit(const char* line) {
	// Raw write: a crash handler must not take a stdio lock the faulting thread may already hold.
	size_t n = strlen(line);
	ssize_t w = write(2, line, n);
	(void)w;
}

static void rmmt_crash_line(const char* label, uintptr_t pc) {
	Dl_info di;
	const char* img = "?";
	const char* sym = "?";
	uintptr_t off = 0;
	if (pc != 0 && dladdr((void*)pc, &di) != 0) {
		if (di.dli_fname) { img = di.dli_fname; }
		if (di.dli_sname) { sym = di.dli_sname; }
		off = (uintptr_t)pc - (uintptr_t)di.dli_saddr;
	}
	char buf[512];
	snprintf(buf, sizeof(buf), "%s pc=%p image=%s sym=%s off=%p\n",
		label, (void*)pc, img, sym, (void*)off);
	rmmt_emit(buf);
}

static void rmmt_crash_handler(int sig, siginfo_t* si, void* uc_) {
	ucontext_t* uc = (ucontext_t*)uc_;
	uintptr_t pc = 0, bp = 0, sp = 0;
	uint64_t tid = 0;
#if defined(__x86_64__)
	pc = (uintptr_t)uc->uc_mcontext->__ss.__rip;
	bp = (uintptr_t)uc->uc_mcontext->__ss.__rbp;
	sp = (uintptr_t)uc->uc_mcontext->__ss.__rsp;
#endif
	(void)pthread_threadid_np(NULL, &tid);
	char buf[512];
	snprintf(buf, sizeof(buf),
		"RING_MACH_TEST_CRASH sig=%d addr=%p pc=%p bp=%p sp=%p tid=%llu\n",
		sig, si ? si->si_addr : (void*)0, (void*)pc, (void*)bp, (void*)sp,
		(unsigned long long)tid);
	rmmt_emit(buf);
	rmmt_crash_line("RING_MACH_TEST_CRASH_FRAME", pc);
	if (bp) {
		for (int i = 0; i < 8; ++i) {
			uintptr_t* frame = (uintptr_t*)bp;
			if (!frame) { break; }
			uintptr_t next = frame[0];
			uintptr_t ret = frame[1];
			if (ret) { rmmt_crash_line("RING_MACH_TEST_CRASH_FRAME", ret); }
			if (next <= bp) { break; }
			bp = next;
		}
	}
	// MEASURED: `signal(sig, SIG_DFL); raise(sig);` produced a SECOND crash line and a wrong final status
	// (`rc=132` for a SIGSEGV), because `raise` is a pthread_kill in this guest and the hard-socket hatch DENIES
	// it (`first-denial=pthread_kill`): the handler returned, the faulting instruction re-ran and the process died
	// of the follow-on fault. The reporter therefore terminates directly with the true status, once.
	_exit(128 + sig);
}

typedef struct {
	semaphore_t sem;
	int delay_ms;
	int done;
} sem_signal_arg_t;

static void* sem_signal_after_delay(void* p) {
	sem_signal_arg_t* a = (sem_signal_arg_t*)p;
	struct timespec ts;
	ts.tv_sec = a->delay_ms / 1000;
	ts.tv_nsec = (long)(a->delay_ms % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) != 0) { /* resume after EINTR */ }
	__atomic_store_n(&a->done, 1, __ATOMIC_RELEASE);
	semaphore_signal(a->sem);
	return NULL;
}

static int sem_report(const char* mode, kern_return_t kr, double elapsed, int pass) {
	printf("RING_MACH_TEST mode=%s pass=%d kr=%d elapsed=%.3f\n", mode, pass ? 1 : 0, (int)kr, elapsed);
	fflush(stdout);
	return pass ? 0 : 1;
}

// sem_ready: the signal is already there when the caller asks. No blocking, no park, plain success.
static int run_sem_ready(unsigned iters) {
	semaphore_t sem;
	if (semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS) {
		printf("RING_MACH_TEST mode=sem_ready pass=0 error=create\n"); return 2;
	}
	double t0 = now_s();
	for (unsigned i = 0; i < iters; ++i) {
		semaphore_signal(sem);
		kern_return_t kr = semaphore_wait(sem);
		if (kr != KERN_SUCCESS) { semaphore_destroy(mach_task_self(), sem); return sem_report("sem_ready", kr, now_s() - t0, 0); }
	}
	double el = now_s() - t0;
	semaphore_destroy(mach_task_self(), sem);
	return sem_report("sem_ready", KERN_SUCCESS, el, 1);
}

// sem_block <ms>: the caller reaches the wait FIRST and the signal arrives while it is parked. The
// elapsed time is the proof that the wait really parked: a transport that returned early would report a
// fraction of the delay, and one that timed out internally would report a failure.
static int run_sem_block(const char* label, unsigned delay_ms, unsigned iters) {
	semaphore_t sem;
	if (semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS) {
		printf("RING_MACH_TEST mode=sem_block pass=0 error=create\n"); return 2;
	}
	double worst_kr_ok = 1;
	double total = 0;
	for (unsigned i = 0; i < iters; ++i) {
		sem_signal_arg_t arg = { sem, (int)delay_ms, 0 };
		pthread_t th;
		if (pthread_create(&th, NULL, sem_signal_after_delay, &arg) != 0) {
			semaphore_destroy(mach_task_self(), sem);
			printf("RING_MACH_TEST mode=sem_block pass=0 error=thread\n"); return 2;
		}
		double t0 = now_s();
		kern_return_t kr = semaphore_wait(sem);
		double el = now_s() - t0;
		pthread_join(th, NULL);
		total += el;
		if (kr != KERN_SUCCESS || el < (double)delay_ms / 1000.0 * 0.8) {
			semaphore_destroy(mach_task_self(), sem);
			return sem_report(label, kr, el, 0);
		}
	}
	semaphore_destroy(mach_task_self(), sem);
	(void)worst_kr_ok;
	// MEASURED (oracle defect, 2026-09-27): this printed the IMPLEMENTATION name ("sem_block") while the caller
	// asked for "sem_gap", so `dwdiag verdict --mode sem_gap` saw a WORKING semantic 5.001 s wait as
	// `EXIT rc=0 :: <no result line>` -- a false acceptance failure. The workload's own machine-readable line must
	// name the mode the caller asked for.
	return sem_report(label, KERN_SUCCESS, total / (double)(iters ? iters : 1), 1);
}

// sem_timed <ms>: NO signal ever arrives, so the SEMANTIC timeout of the call is the only thing that can
// end the wait. The transport must not invent one: the elapsed time has to match what was requested, and
// the result has to be the Mach timeout result, not a transport failure.
static int run_sem_timed(unsigned timeout_ms, unsigned iters) {
	semaphore_t sem;
	if (semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS) {
		printf("RING_MACH_TEST mode=sem_timed pass=0 error=create\n"); return 2;
	}
	int pass = 1;
	kern_return_t last = KERN_SUCCESS;
	double el = 0;
	for (unsigned i = 0; i < iters; ++i) {
		mach_timespec_t wt;
		wt.tv_sec = timeout_ms / 1000;
		wt.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
		double t0 = now_s();
		last = semaphore_timedwait(sem, wt);
		el = now_s() - t0;
		// The requested timeout must have been honoured: not a transport failure, and not an early return.
		if (el < (double)timeout_ms / 1000.0 * 0.6) { pass = 0; break; }
	}
	semaphore_destroy(mach_task_self(), sem);
	return sem_report("sem_timed", last, el, pass);
}

// sem_wait_signal: the *_signal variant. The semaphore being WAITED ON is already signalled, and the one
// being SIGNALLED starts empty, so a successful return proves both halves ran and the transport carried
// the multi-operation shape.
static int run_sem_wait_signal(unsigned iters) {
	semaphore_t wait_sem, sig_sem;
	if (semaphore_create(mach_task_self(), &wait_sem, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS ||
	    semaphore_create(mach_task_self(), &sig_sem, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS) {
		printf("RING_MACH_TEST mode=sem_wait_signal pass=0 error=create\n"); return 2;
	}
	int pass = 1;
	kern_return_t last = KERN_SUCCESS;
	for (unsigned i = 0; i < iters; ++i) {
		semaphore_signal(wait_sem);
		last = semaphore_wait_signal(wait_sem, sig_sem);
		if (last != KERN_SUCCESS) { pass = 0; break; }
		if (semaphore_wait(sig_sem) != KERN_SUCCESS) { pass = 0; break; }
	}
	semaphore_destroy(mach_task_self(), wait_sem);
	semaphore_destroy(mach_task_self(), sig_sem);
	return sem_report("sem_wait_signal", last, 0.0, pass);
}

// sem_timedwait_signal <ms>: the timed variant of the same shape, with no signal available, so the
// SEMANTIC timeout decides again.
static int run_sem_timedwait_signal(unsigned timeout_ms, unsigned iters) {
	semaphore_t wait_sem, sig_sem;
	if (semaphore_create(mach_task_self(), &wait_sem, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS ||
	    semaphore_create(mach_task_self(), &sig_sem, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS) {
		printf("RING_MACH_TEST mode=sem_timedwait_signal pass=0 error=create\n"); return 2;
	}
	int pass = 1;
	kern_return_t last = KERN_SUCCESS;
	double el = 0;
	for (unsigned i = 0; i < iters; ++i) {
		mach_timespec_t wt;
		wt.tv_sec = timeout_ms / 1000;
		wt.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
		double t0 = now_s();
		last = semaphore_timedwait_signal(wait_sem, sig_sem, wt);
		el = now_s() - t0;
		if (el < (double)timeout_ms / 1000.0 * 0.6) { pass = 0; break; }
	}
	semaphore_destroy(mach_task_self(), wait_sem);
	semaphore_destroy(mach_task_self(), sig_sem);
	return sem_report("sem_timedwait_signal", last, el, pass);
}

int main_semaphore_mode(const char* mode, int argc, char** argv) {
	unsigned a2 = (argc > 2) ? (unsigned)atoi(argv[2]) : 1;
	unsigned a3 = (argc > 3) ? (unsigned)atoi(argv[3]) : 1;
	if (!strcmp(mode, "sem_ready")) return run_sem_ready(a2 ? a2 : 1);
	if (!strcmp(mode, "sem_block")) return run_sem_block("sem_block", a2 ? a2 : 100, a3 ? a3 : 1);
	if (!strcmp(mode, "sem_gap"))   return run_sem_block("sem_gap", a2 ? a2 : 5000, a3 ? a3 : 1);
	if (!strcmp(mode, "sem_timed")) return run_sem_timed(a2 ? a2 : 300, a3 ? a3 : 1);
	if (!strcmp(mode, "sem_wait_signal")) return run_sem_wait_signal(a2 ? a2 : 4);
	if (!strcmp(mode, "sem_timedwait_signal")) return run_sem_timedwait_signal(a2 ? a2 : 300, a3 ? a3 : 1);
	return -1;
}

int main(int argc, char** argv) {
	/* TEST-ONLY: arm the fault witness before anything else, so a fault anywhere in this run reports its
	 * signal and faulting address instead of killing the process in silence. */
	if (getenv("DARLING_TEST_FAULT_WITNESS") != NULL) {
		/* ANNOUNCE the arming: a witness that stays silent must be distinguishable from a witness that was
		 * never armed, which is the difference between "the fault is elsewhere" and "the environment never
		 * arrived". Printed with the raw write, same as the witness itself. */
		char arm[96];
		unsigned long an = 0;
		an += fw_str(arm + an, "FAULT-WITNESS armed host_pid=");
		an += fw_dec(arm + an, raw_host_getpid());
		an += fw_str(arm + an, " host_tid=");
		an += fw_dec(arm + an, raw_host_gettid());
		an += fw_str(arm + an, "\n");
		fw_write2(arm, an);
		fw_install(11 /* SIGSEGV */);
		fw_install(7 /* SIGBUS */);
	}
	const char* mode = (argc > 1) ? argv[1] : "basic";
	int failures = 0;
	double t0 = now_s();
	double min_elapsed = 0, max_elapsed = 0;
	int have_elapsed = 0;

	// perf#30 FD-INVENTORY: name the process and its worker count AT START. MEASURED need: a host-side
	// sampler cannot use the exit-time lane-stats pid to sample a LIVE process, and "newest matching
	// cmdline" picked the wrong process twice.
	// MEASURED friction: this line called argv[2] "workers", but for basic/delay argv[2] is the ITERATION COUNT,
	// so a log reader could not tell a 2-worker run from a 2-iteration one. Name the field for the mode.
	fprintf(stderr, "[rmmt] start pid=%d host_pid=%ld host_tid=%ld mode=%s arg=%s\n", (int)getpid(),
		raw_host_getpid(), raw_host_gettid(), mode,
		(argc > 2) ? argv[2] : "-");
	fflush(stderr);
	{
		// Install the crash reporter before any transport work: the only way to tell "died of a fault" from
		// "parked forever" without a host-side sampler that cannot see into the guest namespace.
		static char rmmt_altstack[SIGSTKSZ * 4];
		stack_t ss;
		memset(&ss, 0, sizeof(ss));
		ss.ss_sp = rmmt_altstack;
		ss.ss_size = sizeof(rmmt_altstack);
		// SA_ONSTACK: MEASURED need -- the first version of this reporter produced NO output at all, which is
		// exactly what a stack-overflow fault looks like: the handler needs stack it does not have, faults again
		// and the process dies silently. The reporter must survive the fault it exists to report.
		sigaltstack(&ss, NULL);
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = rmmt_crash_handler;
		sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
		sigaction(SIGSEGV, &sa, NULL);
		sigaction(SIGBUS, &sa, NULL);
		sigaction(SIGILL, &sa, NULL);
		sigaction(SIGABRT, &sa, NULL);
	}

	// A mode that crashes ON PURPOSE: the crash reporter is an instrument, and an instrument is verified by
	// running the shape it exists for (a silent reporter is indistinguishable from no reporter).
	/* TEST-ONLY SECOND ARMING, deliberately LAST: a raw install made before libSystem's own sigaction calls is
	 * replaced by Darling's delivery handler, which is why the first placement announced itself and then never
	 * fired. Installing here, after startup, leaves the raw witness as the process's actual disposition. */
	if (getenv("DARLING_TEST_FAULT_WITNESS") != NULL) {
		fw_install(11 /* SIGSEGV */);
		fw_install(7 /* SIGBUS */);
	}
	if (!strcmp(mode, "crash_test")) {
		volatile int* nullp = (volatile int*)(uintptr_t)0;
		*nullp = 1;
		return 0;
	}

	if (!strcmp(mode, "bsysloop")) {
		return run_bsysloop((argc > 2) ? (unsigned)atoi(argv[2]) : 10);
	}

	if (!strcmp(mode, "fsview")) { return run_fsview(); }

	if (!strcmp(mode, "threadnoop")) {
		return run_threadnoop((argc > 2) ? (unsigned)atoi(argv[2]) : 1);
	}

	if (!strcmp(mode, "selfdrop")) {
		return run_selfdrop((argc > 2) ? (unsigned)atoi(argv[2]) : 1);
	}

	{
		int sem_rc = main_semaphore_mode(mode, argc, argv);
		if (sem_rc >= 0) { return sem_rc; }
	}

	const char* wd = getenv("RING_MACH_WATCHDOG_SECONDS");
	g_watchdog_seconds = wd ? atoi(wd) : 0;

	if (!strcmp(mode, "basic") || !strcmp(mode, "delay")) {
		int delay_ms = (!strcmp(mode, "delay") && argc > 2) ? atoi(argv[2]) : 0;
		// MEASURED (oracle defect, 2026-09-27): the iteration count was read from `argv[delay_ms ? 3 : 2]`, so
		// `delay 0 N` took N from argv[2] -- the DELAY -- and ran ZERO iterations while printing `pass=1`. Four
		// `delay 0 1` runs were read as "the destroy is safe with a delay" when the workload had not run at all.
		// The index is now a property of the MODE, and a run with no iterations is reported as a no-op failure
		// instead of a pass: an oracle must not report success for a workload that did not execute.
		unsigned iters;
		if (!strcmp(mode, "delay")) {
			iters = (unsigned)((argc > 3) ? atoi(argv[3]) : 1);
		} else {
			iters = (unsigned)((argc > 2) ? atoi(argv[2]) : 100);
		}
		if (iters == 0) {
			printf("RING_MACH_TEST mode=%s delay_ms=%d iters=0 pass=0 error=no-op\n", mode, delay_ms);
			return 2;
		}
		for (unsigned i = 0; i < iters; ++i) {
			mach_port_t q;
			// perf#30 (doc section 238): ITERATION PROGRESS. MEASURED need: `basic 1` passes in 283 microseconds and
			// `basic 2` hangs, so the whole path works once and the second iteration stops somewhere in
			// make_port -> create -> send/receive -> join -> drop_port. Nothing could say WHICH, because the workload
			// prints only its final line. Four bounded marks per iteration answer it, read ONCE from the environment
			// so the run's cost is unchanged when the hatch is off (an instrument must not be the reason a run is
			// slow, and the verdict is never taken from a run that has it on).
			static int itrace = -1;
			if (itrace < 0) { const char* t = getenv("RING_MACH_TEST_ITER_TRACE"); itrace = (t && t[0] == '1') ? 1 : 0; }
			// perf#30 (doc section 252): the SERVER's thread identity, not only the TCB. The stall dump names a parked
			// thread by its namespace tid (`pid=1 tid=1282542 ... active=38`), and a mark that carries the SAME number is
			// what turns "a thread is parked in mach_msg" into "THIS mark's thread is parked there". `self_tid()` is
			// `pthread_threadid_np`, i.e. exactly that namespace identity.
			if (itrace && i < 4) { printf("ITER %u tid=%u make_port\n", i, self_tid()); fflush(stdout); }
			make_port(&q);
			if (itrace && i < 4) { printf("ITER %u tid=%u port=%u create\n", i, self_tid(), (unsigned)q); fflush(stdout); }
			sender_arg_t a = { q, delay_ms, 0, i, 0 };
			pthread_t th;
			pthread_create(&th, NULL, sender_main, &a);
			if (itrace && i < 4) { printf("ITER %u tid=%u created\n", i, self_tid()); fflush(stdout); }
			double el = 0;
			kern_return_t kr = KERN_SUCCESS;
			if (!do_receive(q, 0, 0, i, 0, &el, &kr) || a.send_failed) failures++;
			if (itrace && i < 4) { printf("ITER %u tid=%u received kr=%d send_failed=%d\n", i, self_tid(), (int)kr, a.send_failed); fflush(stdout); }
			pthread_join(th, NULL);
			if (itrace && i < 4) { printf("ITER %u tid=%u joined\n", i, self_tid()); fflush(stdout); }
			// perf#30 DIAGNOSIS: the destroy is the statement that dies, and it dies only when it follows the
			// message/thread teardown immediately (measured: `basic 1` crashes, while the same shape with a 300 ms
			// delay before the SEND passes). This hatch delays the DESTROY ALONE -- the sender still sends at once --
			// so "the destroy must not follow the teardown too closely" is separable from "the message must be
			// delayed". Read once per run; the sleep is outside the trace hatch so the hatch is not part of the
			// measurement.
			{
				static int drop_delay_ms = -1;
				if (drop_delay_ms < 0) {
					const char* dd = getenv("RING_MACH_TEST_DROP_DELAY_MS");
					drop_delay_ms = dd ? atoi(dd) : 0;
				}
				if (drop_delay_ms > 0) {
					struct timespec dts;
					dts.tv_sec = drop_delay_ms / 1000;
					dts.tv_nsec = (long)(drop_delay_ms % 1000) * 1000000L;
					while (nanosleep(&dts, &dts) != 0) { /* resume after EINTR */ }
				}
			}
			// perf#30 DIAGNOSIS: `basic 2` stops between this mark and the next one; naming the ENTRY to the
			// teardown separates "the workload never called it" from "the call ran and never returned".
			if (itrace && i < 4) {
				// perf#30 DIAGNOSIS: a synchronously generated fatal signal that is BLOCKED kills the process at the
				// next return to user mode with the DEFAULT action and WITHOUT running a handler -- indistinguishable
				// from "no handler installed". MEASURED need: this workload's SIGSEGV reporter (and the emulation's own
				// fatal line) both stay silent here while they report normally for a deliberate fault, so the mask and
				// the disposition at the faulting call are the two facts that separate the cases.
				// MEASURED: this probe's first form killed the workload it was measuring -- `SIG_SETMASK` with a
				// NULL set is undefined and the process never reached `make_port`. The query is therefore the DEFINED
				// form (block an empty set and take the previous mask) and each step is bracketed by its own mark, so
				// a probe that still breaks the subject is visible as a missing mark instead of as a missing crash.
				printf("ITER %u tid=%u drop_enter\n", i, self_tid()); fflush(stdout);
				sigset_t empty, cur; sigemptyset(&empty); sigemptyset(&cur);
				sigprocmask(SIG_BLOCK, &empty, &cur);
				printf("ITER %u drop_mask blocked_SEGV=%d blocked_TERM=%d\n", i,
					sigismember(&cur, SIGSEGV) ? 1 : 0, sigismember(&cur, SIGTERM) ? 1 : 0); fflush(stdout);
				// perf#30 DIAGNOSIS: are the COMPILED-IN handlers still effective at the instant the real death
				// happens? The real death prints nothing from either handler, and "the handler is broken at this point"
				// and "the handler is fine and this fault somehow bypasses it" are otherwise the same observation.
				// One hatch, one deliberate fault at exactly this statement: if the reporter answers here, the signal
				// machinery is healthy at this point and the real fault's silence is the anomaly to explain.
				{
					static int fault_here = -1;
					if (fault_here < 0) { const char* f = getenv("RING_MACH_TEST_FAULT_BEFORE_DROP"); fault_here = (f && f[0] == '1') ? 1 : 0; }
					if (fault_here) { volatile int* pz = (volatile int*)(uintptr_t)0; *pz = 1; }
				}
				// MEASURED: the disposition QUERY (`sigaction(sig, NULL, &old)`) did not return in this guest -- the
				// mark after it never appeared and the workload died there. The query is therefore not used as a
				// probe; the mask line (a defined query, proven to return) is the surviving instrument.
			}
			// perf#30 DIAGNOSIS: is the fatal statement the DESTROY or the THREAD LIFECYCLE that precedes it?
			// MEASURED setting: `basic 1` (create+join, then destroy) crashes while `selfdrop 1` (same round trip, no
			// helper thread, then destroy) passes -- two different statements change together, so they are separated
			// here: this hatch skips ONLY the destroy and leaves the thread lifecycle untouched.
			{
				static int no_drop = -1;
				if (no_drop < 0) { const char* nd = getenv("RING_MACH_TEST_NO_DROP"); no_drop = (nd && nd[0] == '1') ? 1 : 0; }
				if (!no_drop) { drop_port(q); }
			}
			if (itrace && i < 4) { printf("ITER %u dropped\n", i); fflush(stdout); }
			if (!have_elapsed || el < min_elapsed) min_elapsed = el;
			if (!have_elapsed || el > max_elapsed) max_elapsed = el;
			have_elapsed = 1;
		}
		printf("RING_MACH_TEST mode=%s delay_ms=%d iters=%u pass=%d min_elapsed=%.6f max_elapsed=%.6f\n",
		       mode, delay_ms, iters, failures == 0, min_elapsed, max_elapsed);
	} else if (!strcmp(mode, "timeout")) {
		int timeout_ms = (argc > 2) ? atoi(argv[2]) : 200;
		unsigned iters = (argc > 3) ? (unsigned)atoi(argv[3]) : 5;
		for (unsigned i = 0; i < iters; ++i) {
			mach_port_t q;
			make_port(&q);
			double el = 0;
			kern_return_t kr = KERN_SUCCESS;
			// No sender: the only way out is the guest's own MACH_RCV_TIMEOUT.
			if (!do_receive(q, 1, timeout_ms, i, 0, &el, &kr)) {
				printf("TIMEOUT_MODE_BAD kr=%d elapsed=%.3f\n", kr, el);
				failures++;
			}
			if (el < (double)timeout_ms / 1000.0 * 0.5) {
				printf("TIMEOUT_TOO_EARLY elapsed=%.3f want>=%.3f\n", el, (double)timeout_ms / 1000.0 * 0.5);
				failures++;
			}
			drop_port(q);
			if (!have_elapsed || el < min_elapsed) min_elapsed = el;
			if (!have_elapsed || el > max_elapsed) max_elapsed = el;
			have_elapsed = 1;
		}
		printf("RING_MACH_TEST mode=timeout timeout_ms=%d iters=%u pass=%d min_elapsed=%.6f max_elapsed=%.6f\n",
		       timeout_ms, iters, failures == 0, min_elapsed, max_elapsed);
	} else if (!strcmp(mode, "ool")) {
		unsigned iters = (argc > 2) ? (unsigned)atoi(argv[2]) : 20;
		for (unsigned i = 0; i < iters; ++i) {
			mach_port_t q;
			make_port(&q);
			sender_arg_t a = { q, 0, 1, i, 0 };
			pthread_t th;
			pthread_create(&th, NULL, sender_main, &a);
			double el = 0;
			kern_return_t kr = KERN_SUCCESS;
			if (!do_receive(q, 0, 0, i, 1, &el, &kr) || a.send_failed) failures++;
			pthread_join(th, NULL);
			drop_port(q);
		}
		printf("RING_MACH_TEST mode=ool iters=%u pass=%d\n", iters, failures == 0);
	} else if (!strcmp(mode, "stress_pool")) {
		unsigned receivers = (argc > 2) ? (unsigned)atoi(argv[2]) : 32;
		unsigned iters = (argc > 3) ? (unsigned)atoi(argv[3]) : 20;
		return run_pool(receivers, iters, 4, "stress_pool", 0, 1);
	} else if (!strcmp(mode, "stress_mixed")) {
		unsigned workers = (argc > 2) ? (unsigned)atoi(argv[2]) : 32;
		unsigned iters = (argc > 3) ? (unsigned)atoi(argv[3]) : 8;
		return run_mixed(workers, iters);
	} else if (!strcmp(mode, "stress_churn")) {
		unsigned iters = (argc > 2) ? (unsigned)atoi(argv[2]) : 256;
		return run_churn(iters);
	} else if (!strcmp(mode, "ool_delay")) {
		// R1: one blocking Ring parent parks for `delay_ms` and then performs an out-of-line round trip, so
		// the caller-S2C happens on a Call that has been suspended far longer than any spin/park heuristic.
		// The reply must arrive on the SAME lane and the SAME seq (the harness verifies seq, and the trace
		// carries the lane identity), i.e. the Call-owned transport context survived the suspension.
		int delay_ms = (argc > 2) ? atoi(argv[2]) : 5000;
		unsigned iters = (argc > 3) ? (unsigned)atoi(argv[3]) : 3;
		double mn = 0, mx = 0; int have = 0;
		for (unsigned i = 0; i < iters; ++i) {
			mach_port_t q;
			make_port(&q);
			sender_arg_t a = { q, delay_ms, 1 /* ool */, i, 0 };
			pthread_t th;
			pthread_create(&th, NULL, sender_main, &a);
			double el = 0;
			kern_return_t kr = KERN_SUCCESS;
			if (!do_receive(q, 0, 0, i, 1 /* ool */, &el, &kr) || a.send_failed) failures++;
			pthread_join(th, NULL);
			drop_port(q);
			if (!have || el < mn) mn = el;
			if (!have || el > mx) mx = el;
			have = 1;
		}
		printf("RING_MACH_TEST mode=ool_delay delay_ms=%d iters=%u pass=%d min_elapsed=%.6f max_elapsed=%.6f\n",
		       delay_ms, iters, failures == 0, mn, mx);
	} else if (!strcmp(mode, "r2")) {
		// R2: cross-call interference. Worker A parks on a Ring receive whose message arrives after 3s;
		// meanwhile THIS thread does `iters` out-of-line Ring round trips on its OWN lane. Neither reply may
		// be delivered to the other's destination, and both are verified by their own payload/seq checks.
		unsigned iters = (argc > 2) ? (unsigned)atoi(argv[2]) : 200;
		mach_port_t qa;
		make_port(&qa);
		sender_arg_t a = { qa, 3000, 1, 7, 0 };
		pthread_t th;
		pthread_create(&th, NULL, sender_main, &a);
		double el = 0;
		kern_return_t kr = KERN_SUCCESS;
		int a_ok = 0;
		unsigned b_fail = 0;
		// B: busy traffic while A is parked
		for (unsigned i = 0; i < iters; ++i) {
			mach_port_t qb;
			make_port(&qb);
			sender_arg_t b = { qb, 0, 1, 100000 + i, 0 };
			sender_main(&b);
			double eb = 0;
			kern_return_t krb = KERN_SUCCESS;
			if (b.send_failed || !do_receive(qb, 0, 0, 100000 + i, 1, &eb, &krb)) b_fail++;
			drop_port(qb);
		}
		if (do_receive(qa, 0, 0, 7, 1, &el, &kr)) a_ok = 1;
		pthread_join(th, NULL);
		drop_port(qa);
		failures += (unsigned)b_fail + (a_ok ? 0u : 1u);
		printf("RING_MACH_TEST mode=r2 iters=%u pass=%d parked_parent_ok=%d concurrent_failures=%u "
		       "parked_elapsed=%.3f\n", iters, failures == 0, a_ok, b_fail, el);
	} else if (!strcmp(mode, "mmap_fail")) {
		// perf#27 #9: force an anonymous mmap that MUST fail, through the same emulated path the guest
		// uses for any mmap (server allocatePages -> caller-S2C). Comparing hatch OFF (legacy UDS S2C) with
		// hatch ON (duplex mailbox) is only meaningful on the OBSERVABLE result, so the mode prints the
		// return value and errno and nothing else.
		unsigned iters = (argc > 2) ? (unsigned)atoi(argv[2]) : 3;
		for (unsigned i = 0; i < iters; ++i) {
			errno = 0;
			// 2^62 bytes: no address space can hold it, so the kernel must refuse with ENOMEM
			void* r = mmap(NULL, (size_t)1u << 62, PROT_READ | PROT_WRITE,
			               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			printf("MMAP_FAIL i=%u ret=%p errno=%d\n", i, r, errno);
			fflush(stdout);
		}
		printf("RING_MACH_TEST mode=mmap_fail iters=%u pass=1\n", iters);
	} else if (!strcmp(mode, "lane_hold")) {
		unsigned n = (argc > 2) ? (unsigned)atoi(argv[2]) : 128;
		return run_lane_hold(n);
	} else if (!strcmp(mode, "pthread_live")) {
		// dar-dles: hold N guest threads simultaneously alive (no Mach IPC), each parked on one explicit
		// release barrier, so a create/start stall is attributable to the pthread lifecycle handshake.
		unsigned n = (argc > 2) ? (unsigned)atoi(argv[2]) : 64;
		unsigned stack_kib = (argc > 3) ? (unsigned)atoi(argv[3]) : 256;
		return run_pthread_live(n, stack_kib);
	} else if (!strcmp(mode, "bench_simple")) {
		unsigned iters = (argc > 2) ? (unsigned)atoi(argv[2]) : 2000;
		return run_pool(1, iters, 0, "bench_simple", 1, 0);
	} else if (!strcmp(mode, "forkexec")) {
		/* FORK + EXEC, MEASURED BY THE WORKLOAD ITSELF (dar-4cp9). The tool advertises this mode and the workload
		 * did not implement it, so 'the fork+exec gate' had no vehicle: the run only reported unknown_mode. The
		 * child execs THIS binary with a small mode that uses the RPC transport, so a successful child proves the
		 * transport survives both fork and exec, and the parent reports the count rather than assuming it. */
		const char* self = (argc > 0 && argv[0] != NULL) ? argv[0] : "/private/var/tmp/ring_mach_msg_test";
		unsigned fe_iters = (argc > 2) ? (unsigned)atoi(argv[2]) : 1;
		unsigned fe_ok = 0, fe_bad = 0;
		for (unsigned i = 0; i < fe_iters; ++i) {
			pid_t p = fork();
			if (p == 0) {
				char* childArgv[4];
				childArgv[0] = (char*)self;
				childArgv[1] = (char*)"sem_ready";
				childArgv[2] = (char*)"2";
				childArgv[3] = NULL;
				execve(self, childArgv, environ);
				_exit(127); /* exec failed: the parent counts this as bad */
			}
			if (p < 0) { ++fe_bad; continue; }
			int st = 0;
			if (waitpid(p, &st, 0) != p) { ++fe_bad; continue; }
			if (WIFEXITED(st) && WEXITSTATUS(st) == 0) { ++fe_ok; } else { ++fe_bad; }
		}
		printf("RING_MACH_TEST mode=forkexec iters=%u child_ok=%u child_bad=%u pass=%d\n",
			fe_iters, fe_ok, fe_bad, (fe_ok == fe_iters && fe_bad == 0) ? 1 : 0);
		failures += (fe_ok == fe_iters && fe_bad == 0) ? 0 : 1;
	} else if (!strcmp(mode, "bench_ool")) {
		unsigned iters = (argc > 2) ? (unsigned)atoi(argv[2]) : 500;
		return run_pool(1, iters, 1, "bench_ool", 1, 0);
	} else {
		printf("RING_MACH_TEST mode=%s pass=0 error=unknown_mode\n", mode);
		return 2;
	}

	printf("RING_MACH_TEST_DURATION mode=%s seconds=%.3f\n", mode, now_s() - t0);
	return failures ? 1 : 0;
}
