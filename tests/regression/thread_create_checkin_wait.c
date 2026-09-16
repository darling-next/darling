// Regression test for perf #1 (dar-dar6x4-perf-5dq.1):
// __darling_thread_create() must WAIT for the new thread's darlingserver checkin by
// BLOCKING (FUTEX_WAIT), NOT an unbounded sched_yield() busy-spin. The old spin burned a
// whole core per in-flight thread creation; under `make -j` link storms that starved the
// very threads (and the single-threaded darlingserver) it was waiting on.
//
// We can't link mldr's full thread machinery in a host test, so we reproduce the exact
// creator/child handshake both ways. The discriminator is the HANDSHAKE, never host timing:
//
//   * each wait model publishes its own progress: the polling model increments `polls` on
//     every loop iteration and has no way to say it stopped polling; the adaptive model
//     polls a bounded number of times and then publishes `parked` immediately before it
//     enters FUTEX_WAIT.
//   * the child models the SLOW checkin. Its delay is produced by the handshake, not by a
//     timer: it withholds the checkin until the creator has either parked (adaptive wait) or
//     burned the whole poll budget (polling-only wait). So the checkin is slow by
//     construction -- slower than the creator's bounded spin -- instead of slow because of a
//     clock.
//   * the oracle: the checkin must have been released to a creator that PARKED, i.e. that
//     stopped polling to block. A model that only polls can never publish `parked` however
//     the host schedules it, so
//       RED   (polling wait) -> deterministic failure: polls >= budget, never parked
//       GREEN (adaptive wait)-> deterministic success: parked after exactly kSpinIters polls
//
// The previous harness inferred this from the creator's sampled CPU time while it waited
// out a fixed 300ms sleep. That only holds while the host actually schedules the spinning
// thread: on a loaded host a starved spin looked exactly like a blocked wait
// (`variant=spin creator_cpu=0ms wall=309ms`) and the RED arm stopped discriminating
// (dar-developer-tooling-ux-tcwe.34). Creator CPU is still reported for the record, but the
// pass/fail decision no longer depends on it.
//
// This is a HOST test (plain glibc, no Darling runtime needed); see run-thread-create-
// checkin-wait.sh. Exit 0 = PASS (futex path); the runner also proves the RED case fails.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
#include <sys/syscall.h>
#include <linux/futex.h>

// The production wait (mldr threads.c): bounded spin, then FUTEX_WAIT. Production spins
// 4000 iterations; the harness keeps the same shape with a deliberately small bound so the
// handshake costs a couple of hundred scheduler activations instead of thousands (every
// sched_yield on a saturated runqueue forfeits the rest of the slice). The property under
// test is bounded-then-park versus unbounded polling, not the tuning constant.
static const long kSpinIters = 128;
// How much polling the checkin tolerates from a wait that never parks. Any value above
// kSpinIters works -- a polling wait overshoots it at once -- and keeping it small keeps the
// RED arm cheap on a loaded host.
// Measured in creator polls -- never in wall-clock time.
static const long kPollBudget = kSpinIters + 128;
// Hang guard: how many times the checkin re-checks a wait that has neither parked nor polled
// past the budget. Each observation costs at least one paced sleep, so the window this opens
// grows with host load instead of expiring early under it -- a starved but healthy wait keeps
// its window, while a model that does neither arm is diagnosed after ~20s instead of hanging
// the test. The guard never decides between the arms: that verdict is `parked` alone.
static const long kObserveBudget = 20000;
// Creator CPU ceiling while it waits (the original CPU oracle's limit, kept as a report).
static const long kCpuLimitMs = 30;

static _Atomic int checked_in = 0;
static _Atomic long polls = 0;	// polling iterations published by the creator
static _Atomic int parked = 0;	// published by a wait that stops polling to block
static _Atomic int checkin_gave_up = 0;	// the checkin stopped waiting for a park
static int use_spin = 0;

static long ms_thread_cpu(void) {
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static long futex(_Atomic int* uaddr, int op, int val) {
	return syscall(SYS_futex, (int*)uaddr, op, val, NULL, NULL, 0);
}

static long polls_published(void) {
	return atomic_load_explicit(&polls, memory_order_acquire);
}

// emulates darling_thread_entry: the darlingserver checkin for the new thread.
// The checkin is deliberately slow, and its latency is modelled by the handshake instead of
// by a timer: it is withheld until the creator has shown what it does while waiting. Waiting
// on the creator's published progress (not on the clock) is what keeps this deterministic on
// a loaded host -- nothing here depends on when the scheduler runs either thread. The paced
// sleep below is PACING only, so this observer does not compete with the wait it observes:
// a second sched_yield spinner would inflate the creator's yield cost (every sched_yield on a
// non-idle runqueue forfeits the rest of a slice), which the pre-fix harness did not have to
// pay because its child was asleep. No decision is taken from elapsed time.
static void* child(void* p) {
	(void)p;
	for (long observed = 0;; ++observed) {
		if (atomic_load_explicit(&parked, memory_order_acquire) != 0)
			break;
		if (polls_published() >= kPollBudget)
			break;
		if (observed >= kObserveBudget) {
			atomic_store_explicit(&checkin_gave_up, 1, memory_order_release);
			break;
		}
		struct timespec pause = { 0, 1000 * 1000 };	// 1ms
		nanosleep(&pause, NULL);
	}

	atomic_store_explicit(&checked_in, 1, memory_order_release);
	if (!use_spin)
		futex(&checked_in, FUTEX_WAKE_PRIVATE, 1);
	return NULL;
}

int main(int argc, char** argv) {
	if (argc > 1 && strcmp(argv[1], "spin") == 0)
		use_spin = 1;

	pthread_t th;
	long cpu0 = ms_thread_cpu();
	struct timespec w0, w1;
	clock_gettime(CLOCK_MONOTONIC, &w0);

	if (pthread_create(&th, NULL, child, NULL) != 0) {
		perror("pthread_create");
		return 2;
	}

	// THE WAIT under test -- exactly the two variants from __darling_thread_create()
	long wait_polls = 0;
	if (use_spin) {
		// RED: the original unbounded busy-spin. It can only poll: it has no state that
		// tells the slow checkin it stopped consuming CPU, so it can never park.
		while (atomic_load_explicit(&checked_in, memory_order_acquire) == 0) {
			atomic_store_explicit(&polls, ++wait_polls, memory_order_release);
			sched_yield();
		}
	} else {
		// GREEN: the production ADAPTIVE wait -- bounded spin (fast path) then FUTEX_WAIT
		// so a slow checkin can never monopolise a core.
		while (atomic_load_explicit(&checked_in, memory_order_acquire) == 0) {
			if (wait_polls < kSpinIters) {
				atomic_store_explicit(&polls, ++wait_polls, memory_order_release);
				sched_yield();
				continue;
			}
			// Bounded spin exhausted: hand off to the blocking wait. This publication is
			// the observable that only a wait which stops polling can produce, and it is
			// made immediately before the FUTEX_WAIT that gives the core up.
			atomic_store_explicit(&parked, 1, memory_order_release);
			long r = futex(&checked_in, FUTEX_WAIT_PRIVATE, 0);
			if (r != 0 && errno != EAGAIN && errno != EINTR)
				sched_yield();
		}
	}

	long cpu1 = ms_thread_cpu();
	clock_gettime(CLOCK_MONOTONIC, &w1);
	pthread_join(th, NULL);

	long cpu_ms = cpu1 - cpu0;
	long wall_ms = (w1.tv_sec - w0.tv_sec) * 1000L + (w1.tv_nsec - w0.tv_nsec) / 1000000L;
	int observed_parked = atomic_load_explicit(&parked, memory_order_acquire);
	int gave_up = atomic_load_explicit(&checkin_gave_up, memory_order_acquire);

	printf("variant=%s polls=%ld parked=%d gave_up=%d creator_cpu=%ldms wall=%ldms\n",
	       use_spin ? "spin" : "futex", wait_polls, observed_parked, gave_up, cpu_ms, wall_ms);
	fflush(stdout);	// keep the diagnosis after the metrics it explains

	int rc = 0;
	if (gave_up) {
		fprintf(stderr,
			"FAIL: the checkin waited %ld observations for a wait that neither parked nor "
			"polled past the budget -- this is neither arm\n", (long)kObserveBudget);
		rc = 1;
	}
	// THE ORACLE: a slow checkin may only be released to a creator that parked, i.e. that
	// gave up polling and entered the blocking wait. Scheduling cannot fake this -- a
	// polling-only wait never publishes `parked`, however long it is starved or however
	// often it is preempted, so the RED model fails here under any host load.
	if (!observed_parked) {
		fprintf(stderr,
			"FAIL: creator burned %ld polls while waiting and never parked -- "
			"unbounded busy-spin\n", wait_polls);
		rc = 1;
	}
	if (cpu_ms > kCpuLimitMs) {
		fprintf(stderr,
			"FAIL: creator burned %ldms CPU while waiting (limit %ldms) -- busy-spin\n",
			cpu_ms, kCpuLimitMs);
		rc = 1;
	}
	// The blocking path is only exercised once the bounded spin is exhausted, which the
	// handshake above guarantees for a wait that parks; a faster handshake means the
	// checkin was not slow and the test proved nothing.
	if (wait_polls < kSpinIters) {
		fprintf(stderr,
			"FAIL: checkin completed after %ld polls -- handshake too fast to exercise "
			"the blocking wait\n", wait_polls);
		rc = 1;
	}
	if (rc != 0)
		return rc;

	printf("PASS: creator parked in the blocking wait; the checkin was released only after "
	       "the creator stopped polling (%ld polls, %ldms CPU)\n", wait_polls, cpu_ms);
	return 0;
}
