/*
 * Real-guest lifecycle oracle for the urgent signal plane and pthread
 * cancellation, using only public Darwin APIs:
 *
 *  0. Five nested SA_NODEFER handlers on the sole signal-eligible thread
 *     must unwind exactly. Live interrupt contexts may outlast and outnumber
 *     their transport reservations; no transport capacity is increased.
 *
 *  1. Four threads enter a cancelable read path after leaving their readiness
 *     barrier with cancellation disabled.  Each cancellation occurs while a
 *     real socket transaction awaits its reply and an unaffected peer is
 *     parked inside a real SIGUSR1 application handler.  Every canceled
 *     worker must join with exactly PTHREAD_CANCELED; every unaffected peer
 *     must return its own sentinel.
 *
 *  2. The process then becomes its own debugger: it registers a task
 *     SOFTWARE and BAD_INSTRUCTION exception port and calls ptrace(PT_SIGEXC)
 *     on itself. A debugger thread receives the genuine exception
 *     and deliberately withholds the reply. While it is withheld
 *     the signal must not reach the guest handler and the interrupted thread
 *     must not continue: SIGPROCESS completes only at the debugger's reply.
 *     The debugger then uses PT_THUPDATE to suppress the signal (0), replace
 *     it with SIGUSR2, or leave it untouched (the original SIGUSR1 must still
 *     be delivered), replies, and the exact guest handler outcome is checked.
 *     A fourth scenario raises SIGILL from user space with pthread_kill.
 *     Darwin reports that as a Unix signal, so the debugger must receive
 *     EXC_SOFTWARE/{EXC_SOFT_SIGNAL, SIGILL}; a successful reply without
 *     THUPDATE must restore and deliver the original SIGILL, not consume it.
 *
 *  3. The fifth scenario is a real CPU fault rather than an asynchronous
 *     signal. Inside the same no-C-call live-register window the target
 *     publishes labeled trap and resume addresses, arms itself, and executes
 *     `ud2` (encoding 0F 0B). The debugger must receive
 *     EXC_BAD_INSTRUCTION/{EXC_I386_INVOP}, read the interrupted thread's
 *     integer state through the received Mach thread port, require the saved
 *     PC to equal the published trap label, change ONLY that PC to the resume
 *     label (trap + 2) with thread_set_state, and reply KERN_SUCCESS without
 *     PT_THUPDATE. The thread must continue at the resume label with its live
 *     GPR/XMM patterns intact and SIGILL must not reach the application
 *     handler. The saved PC must be advanced because x86 UD2 leaves the
 *     instruction pointer on the faulting instruction; replying alone would
 *     fault again.
 *
 * Worker wakes are serialized, not a burst of three SIGPROCESS requests:
 * ENTER and EXIT consume the same four-slot pool as SIGPROCESS.  Application
 * handler markers alone do not prove that asynchronous EXIT has retired.
 * The fixture does not claim an arbitrary-admission guarantee.  A hang fails
 * through a bounded watch thread instead of being interpreted as success.
 *
 * Prerequisites and distinctions:
 *  - ptrace(PT_SIGEXC) on the current process is Darling's self-debugger
 *    path.  It is deliberately Darling-internal and is NOT a claim of native
 *    portability: native Darwin establishes tracing through a legitimate
 *    tracer/tracee relationship (PT_ATTACH, or PT_TRACE_ME in the tracee) and
 *    exercises its own Mach reply/state-update paths, which this fixture does
 *    not attempt.  A native run of this self-debugger arrangement must not be
 *    read as passing this contract.
 *  - The fixture does not call thread_suspend.  duct-tape's thread_suspend
 *    waits for the target to reach dtape_thread_wait_while_user_suspended,
 *    which is reached only after the exception reply; suspending before the
 *    reply would deadlock on every image, and suspending after it races the
 *    completion.  The withheld-reply window is the deterministic public gate
 *    that SIGPROCESS must not cross before the debugger resumes the signal.
 *
 * Runner contract (registration is owned by the integration parent):
 *   guest-c fixture, compile flags -std=gnu11 -Wall -Wextra -Werror,
 *   ok-marker URGENT_SIGNAL_LIFECYCLE_PASS=1, per-shell-stage budget 120 s
 *   (including cold startup/compilation), internal watchdog 10 s unchanged;
 *   no extra privileges or installed dserver tools.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <mach/exc.h>
#include <mach/exception_types.h>
#include <mach/i386/exception.h>
#include <mach/i386/thread_status.h>
#include <mach/mach.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/ndr.h>
#include <mach/task.h>
#include <mach/thread_act.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Well below the workload's 30 s catalog bound; every phase is rendezvous
 * driven, so this only turns a real hang into a named failure. */
#define WATCHDOG_SECONDS 10

#define CANCEL_WORKERS 4
#define NESTED_SIGNAL_DEPTH 5
#define IPC_PEERS 2
#define PHASE1_THREADS (CANCEL_WORKERS + 1 + 1 + IPC_PEERS)

#define DEBUG_SCENARIOS 5
#define PROGRESS_ROUNDS 32
#define PROGRESS_STOP 0xff

/* Handler-side markers; neither can collide with SIGUSR1/SIGUSR2 numbers. */
#define HOLDER_ENTERED 0xaa
#define HOLDER_RELEASED 0xee
#define WORKER_READY 0xbb
#define HOLDER_FAILED 0xdd

/* exception_raise is the first routine of the exc subsystem (see the
 * subsystem map in <mach/exc.h>); EXCEPTION_DEFAULT without
 * MACH_EXCEPTION_CODES selects it. */
#define EXC_RAISE_MESSAGE_ID 2401

#define WORKER_UNEXPECTED_DATA ((void *)(uintptr_t)0x57554441u)
#define WORKER_READ_FAILED ((void *)(uintptr_t)0x57524641u)
#define PEER_READER_SENTINEL ((void *)(uintptr_t)0x52454144u)
#define PEER_READER_ERROR ((void *)(uintptr_t)0x52454552u)
#define PEER_HOLDER_SENTINEL ((void *)(uintptr_t)0x484f4c44u)
#define PEER_IPC_A_SENTINEL ((void *)(uintptr_t)0x49504341u)
#define PEER_IPC_B_SENTINEL ((void *)(uintptr_t)0x49504342u)
#define IPC_ERROR ((void *)(uintptr_t)0x49504345u)
#define TARGET_SENTINEL ((void *)(uintptr_t)0x54415247u)
#define PROGRESS_SENTINEL ((void *)(uintptr_t)0x50524f47u)

enum debug_action {
	DEBUG_ACTION_NO_UPDATE = 0,
	DEBUG_ACTION_SUPPRESS = 1,
	DEBUG_ACTION_REPLACE = 2,
	DEBUG_ACTION_RESUME_PC = 3
};

static void fail(const char *reason)
{
	printf("URGENT_SIGNAL_LIFECYCLE_FAIL=%s\n", reason);
	fflush(stdout);
	_exit(1);
}

static void write_byte_or_fail(int fd, unsigned char value);
static int write_byte(int fd, unsigned char value);

static int write_byte(int fd, unsigned char value)
{
	for (;;) {
		ssize_t written = write(fd, &value, 1);

		if (written == 1)
			return 0;
		if (written < 0 && errno == EINTR)
			continue;
		return -1;
	}
}

static int read_byte(int fd, unsigned char *value)
{
	for (;;) {
		ssize_t count = read(fd, value, 1);

		if (count == 1)
			return 0;
		if (count == 0)
			return -1;
		if (count < 0 && errno == EINTR)
			continue;
		return -1;
	}
}

static void write_byte_or_fail(int fd, unsigned char value)
{
	if (write_byte(fd, value) != 0)
		fail("byte-write-failed");
}

/* ------------------------------------------------------------------------- */
/* Signal pipe and application handlers                                      */
/* ------------------------------------------------------------------------- */

static int s_signal_pipe[2] = {-1, -1};

/*
 * The holder deliberately parks inside a real application handler on a pipe
 * gate; main releases it only after the cancellations were issued.  The hold
 * flag is set only after the reader's handler entry was observed; worker
 * wakes use a different handler.  Only async-signal-safe calls here.
 */
static volatile sig_atomic_t s_handler_hold;
static int s_hold_gate_fd = -1;

static void signal_marker(int signum)
{
	unsigned char byte;
	__asm__ volatile("pxor %%xmm0, %%xmm0\n\tpxor %%xmm1, %%xmm1"
		: : : "xmm0", "xmm1");

	if (s_handler_hold) {
		(void)write_byte(s_signal_pipe[1], HOLDER_ENTERED);
		for (;;) {
			ssize_t count = read(s_hold_gate_fd, &byte, 1);

			if (count == 1) {
				(void)write_byte(s_signal_pipe[1], HOLDER_RELEASED);
				return;
			}
			if (count < 0 && errno == EINTR)
				continue;
			(void)write_byte(s_signal_pipe[1], HOLDER_FAILED);
			return;
		}
	}

	(void)write_byte(s_signal_pipe[1], (unsigned char)signum);
}

/*
 * Phase-1 wake handler: pthread_cancel only arms the cancel bit, so a thread
 * blocked in read has to be interrupted before it can observe it.  This
 * handler deliberately writes nothing, because the hold flag used by
 * signal_marker must not capture a worker wake.
 */
static void signal_wake(int signum)
{
	(void)signum;
}

static void install_signal_handlers(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0; /* no SA_RESTART: the interrupted call must surface EINTR */

	action.sa_handler = signal_marker;
	if (sigaction(SIGUSR1, &action, NULL) != 0)
		fail("signal-handler-install-failed");
	action.sa_handler = signal_wake;
	if (sigaction(SIGUSR2, &action, NULL) != 0)
		fail("signal-handler-install-failed");
}

static pid_t s_nested_process;
static volatile sig_atomic_t s_nested_depth;
static volatile sig_atomic_t s_nested_entered;
static volatile sig_atomic_t s_nested_left;
static volatile sig_atomic_t s_nested_error;

static void nested_signal_handler(int signum)
{
	sig_atomic_t depth = ++s_nested_depth;
	(void)signum;
	s_nested_entered++;
	if (depth < NESTED_SIGNAL_DEPTH && kill(s_nested_process, SIGUSR1) != 0)
		s_nested_error = errno;
	if (s_nested_depth != depth)
		s_nested_error = EINVAL;
	s_nested_left++;
	s_nested_depth--;
}

static void nested_signal_phase(void)
{
	struct sigaction action;
	struct sigaction previous;
	sigset_t signal_set;
	sigset_t previous_mask;

	s_nested_process = getpid();
	memset(&action, 0, sizeof(action));
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_NODEFER;
	action.sa_handler = nested_signal_handler;
	if (sigaction(SIGUSR1, &action, &previous) != 0)
		fail("nested-handler-install-failed");
	sigemptyset(&signal_set);
	sigaddset(&signal_set, SIGUSR1);
	if (pthread_sigmask(SIG_UNBLOCK, &signal_set, &previous_mask) != 0)
		fail("nested-mask-unblock-failed");
	if (kill(s_nested_process, SIGUSR1) != 0)
		s_nested_error = errno;
	if (pthread_sigmask(SIG_SETMASK, &previous_mask, NULL) != 0 ||
			sigaction(SIGUSR1, &previous, NULL) != 0)
		fail("nested-handler-restore-failed");
	if (s_nested_error != 0) {
		printf("URGENT_SIGNAL_LIFECYCLE_NESTED_ERRNO=%d\n", (int)s_nested_error);
		fail("nested-signal-rejected");
	}
	if (s_nested_depth != 0 || s_nested_entered != NESTED_SIGNAL_DEPTH ||
			s_nested_left != NESTED_SIGNAL_DEPTH)
		fail("nested-context-unwind-wrong");
	printf("URGENT_SIGNAL_LIFECYCLE_NESTED=%d\n", NESTED_SIGNAL_DEPTH);
}

static struct sigaction s_previous_sigill_action;

static void install_replacement_handler(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = signal_marker;
	if (sigaction(SIGUSR2, &action, NULL) != 0)
		fail("signal-handler-install-failed");
	if (sigaction(SIGILL, &action, &s_previous_sigill_action) != 0)
		fail("signal-handler-install-failed");
}

static void expect_signal_byte(unsigned char expected, const char *reason)
{
	unsigned char byte;
	ssize_t count;

	do {
		count = read(s_signal_pipe[0], &byte, 1);
	} while (count < 0 && errno == EINTR);
	if (count != 1 || byte != expected)
		fail(reason);
}

static void make_read_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);

	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		fail("nonblocking-failed");
}

static size_t read_handler_bytes(unsigned char *buffer, size_t capacity)
{
	size_t total = 0;

	for (;;) {
		ssize_t count;

		if (total == capacity)
			break;
		count = read(s_signal_pipe[0], buffer + total, capacity - total);
		if (count > 0) {
			total += (size_t)count;
			continue;
		}
		if (count == 0)
			break;
		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			break;
		fail("handler-bytes-read-failed");
	}
	return total;
}

/* ------------------------------------------------------------------------- */
/* Bounded failure watchdog (no signal is used, so has_sigexc cannot see it) */
/* ------------------------------------------------------------------------- */

static pthread_mutex_t s_watchdog_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_watchdog_cond = PTHREAD_COND_INITIALIZER;
static int s_watchdog_done;

static void *watchdog_thread(void *argument)
{
	struct timespec deadline;

	(void)argument;
	if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
		fail("watchdog-clock-unavailable");
	deadline.tv_sec += WATCHDOG_SECONDS;

	pthread_mutex_lock(&s_watchdog_lock);
	while (!s_watchdog_done) {
		int status = pthread_cond_timedwait(&s_watchdog_cond, &s_watchdog_lock, &deadline);

		if (status == ETIMEDOUT && !s_watchdog_done) {
			pthread_mutex_unlock(&s_watchdog_lock);
			fail("watchdog-deadline");
		}
		if (status != 0 && status != ETIMEDOUT) {
			pthread_mutex_unlock(&s_watchdog_lock);
			fail("watchdog-wait-failed");
		}
	}
	pthread_mutex_unlock(&s_watchdog_lock);
	return NULL;
}

static void finish_watchdog(pthread_t watchdog)
{
	pthread_mutex_lock(&s_watchdog_lock);
	s_watchdog_done = 1;
	pthread_cond_broadcast(&s_watchdog_cond);
	pthread_mutex_unlock(&s_watchdog_lock);
	if (pthread_join(watchdog, NULL) != 0)
		fail("watchdog-join-failed");
}

/* ------------------------------------------------------------------------- */
/* Phase 1: cancellation under concurrent signal and IPC traffic             */
/* ------------------------------------------------------------------------- */

static pthread_mutex_t s_barrier_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_barrier_cond = PTHREAD_COND_INITIALIZER;
static unsigned s_barrier_ready;
static int s_barrier_release;

static void barrier_wait(void)
{
	pthread_mutex_lock(&s_barrier_lock);
	s_barrier_ready++;
	pthread_cond_broadcast(&s_barrier_cond);
	while (!s_barrier_release)
		pthread_cond_wait(&s_barrier_cond, &s_barrier_lock);
	pthread_mutex_unlock(&s_barrier_lock);
}

static void barrier_release(unsigned expected)
{
	pthread_mutex_lock(&s_barrier_lock);
	while (s_barrier_ready != expected)
		pthread_cond_wait(&s_barrier_cond, &s_barrier_lock);
	s_barrier_release = 1;
	pthread_cond_broadcast(&s_barrier_cond);
	pthread_mutex_unlock(&s_barrier_lock);
}

static void *cancel_worker(void *argument)
{
	int fd = (int)(intptr_t)argument;
	char byte;

	if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0)
		fail("worker-cancel-state-failed");
	if (pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL) != 0)
		fail("worker-cancel-type-failed");

	barrier_wait();
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
		fail("worker-cancel-state-failed");
	write_byte_or_fail(s_signal_pipe[1], WORKER_READY);
	for (;;) {
		ssize_t count = read(fd, &byte, 1);

		if (count > 0)
			return WORKER_UNEXPECTED_DATA;
		if (count < 0 && errno == EINTR)
			continue;
		return WORKER_READ_FAILED;
	}
}

static void *read_peer(void *argument)
{
	int fd = (int)(intptr_t)argument;
	char byte;

	barrier_wait();
	for (;;) {
		ssize_t count = read(fd, &byte, 1);

		if (count == 1)
			return PEER_READER_SENTINEL;
		if (count < 0 && errno == EINTR)
			continue;
		return PEER_READER_ERROR;
	}
}

static void *signal_holder(void *argument)
{
	sigset_t block;
	sigset_t empty;

	(void)argument;
	sigemptyset(&empty);
	sigemptyset(&block);
	sigaddset(&block, SIGUSR1);
	if (pthread_sigmask(SIG_BLOCK, &block, NULL) != 0)
		fail("holder-mask-failed");

	barrier_wait();
	/* A pending SIGUSR1 is delivered on entry to sigsuspend; the handler
	 * parks there until main opens the gate. */
	if (sigsuspend(&empty) != -1 || errno != EINTR)
		fail("holder-wait-failed");
	return PEER_HOLDER_SENTINEL;
}

struct ipc_peer_context {
	int fd;
	int initiator;
	int control_fd;
	void *sentinel;
};

static void *ipc_peer(void *argument)
{
	struct ipc_peer_context *context = argument;
	unsigned char value;
	unsigned char reply;
	unsigned int round;

	barrier_wait();
	for (round = 0; round < CANCEL_WORKERS; round++) {
		if (context->initiator) {
			if (read_byte(context->control_fd, &value) != 0 ||
					value != (unsigned char)(round + 1u))
				return IPC_ERROR;
			if (write_byte(context->fd, value) != 0 ||
					read_byte(context->fd, &reply) != 0 ||
					reply != (unsigned char)(value ^ 0x5au) ||
					write_byte(context->control_fd, value) != 0)
				return IPC_ERROR;
		} else {
			if (read_byte(context->fd, &value) != 0 ||
					value != (unsigned char)(round + 1u) ||
					write_byte(context->control_fd, value) != 0 ||
					read_byte(context->control_fd, &reply) != 0 ||
					reply != value ||
					write_byte(context->fd, (unsigned char)(value ^ 0x5au)) != 0)
				return IPC_ERROR;
		}
	}
	return context->sentinel;
}

static void cancellation_phase(void)
{
	int worker_pipes[CANCEL_WORKERS][2];
	int reader_pipe[2];
	int hold_gate[2];
	int ipc_sockets[2];
	int ipc_control[IPC_PEERS][2];
	struct ipc_peer_context ipc_context[IPC_PEERS];
	pthread_t workers[CANCEL_WORKERS];
	pthread_t reader;
	pthread_t holder;
	pthread_t ipc_threads[IPC_PEERS];
	unsigned int i;
	void *result;

	for (i = 0; i < CANCEL_WORKERS; i++) {
		if (pipe(worker_pipes[i]) != 0)
			fail("worker-pipe-unavailable");
	}
	if (pipe(reader_pipe) != 0)
		fail("reader-pipe-unavailable");
	if (pipe(hold_gate) != 0)
		fail("hold-gate-unavailable");
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, ipc_sockets) != 0)
		fail("ipc-socketpair-unavailable");
	for (i = 0; i < IPC_PEERS; i++) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, ipc_control[i]) != 0)
			fail("ipc-control-unavailable");
	}
	s_hold_gate_fd = hold_gate[0];

	for (i = 0; i < CANCEL_WORKERS; i++) {
		if (pthread_create(&workers[i], NULL, cancel_worker,
				(void *)(intptr_t)worker_pipes[i][0]) != 0)
			fail("worker-create-failed");
	}
	if (pthread_create(&reader, NULL, read_peer,
			(void *)(intptr_t)reader_pipe[0]) != 0)
		fail("reader-create-failed");
	if (pthread_create(&holder, NULL, signal_holder, NULL) != 0)
		fail("holder-create-failed");

	ipc_context[0].fd = ipc_sockets[0];
	ipc_context[0].initiator = 1;
	ipc_context[0].sentinel = PEER_IPC_A_SENTINEL;
	ipc_context[0].control_fd = ipc_control[0][1];
	ipc_context[1].fd = ipc_sockets[1];
	ipc_context[1].initiator = 0;
	ipc_context[1].sentinel = PEER_IPC_B_SENTINEL;
	ipc_context[1].control_fd = ipc_control[1][1];
	for (i = 0; i < IPC_PEERS; i++) {
		if (pthread_create(&ipc_threads[i], NULL, ipc_peer, &ipc_context[i]) != 0)
			fail("ipc-peer-create-failed");
	}

	barrier_release(PHASE1_THREADS);

	/* Readiness is after the barrier mutex is unlocked and cancellation is
	 * enabled.  It establishes the read path, not a kernel-blocked instant. */
	for (i = 0; i < CANCEL_WORKERS; i++)
		expect_signal_byte(WORKER_READY, "worker-readiness-wrong");
	if (pthread_kill(reader, SIGUSR1) != 0)
		fail("reader-signal-failed");
	expect_signal_byte((unsigned char)SIGUSR1, "signal-marker-wrong");

	/* Park one real application handler, then cancel while it is active. */
	s_handler_hold = 1;
	if (pthread_kill(holder, SIGUSR1) != 0)
		fail("holder-signal-failed");
	expect_signal_byte(HOLDER_ENTERED, "holder-handler-not-entered");

	for (i = 0; i < CANCEL_WORKERS; i++) {
		unsigned char value = (unsigned char)(i + 1u);
		unsigned char observed;
		int wake;

		write_byte_or_fail(ipc_control[0][0], value);
		if (read_byte(ipc_control[1][0], &observed) != 0 || observed != value)
			fail("ipc-not-pending");
		/* The responder has received the request and cannot reply until
		 * this thread opens its gate: IPC is live during cancellation. */
		if (pthread_cancel(workers[i]) != 0)
			fail("worker-cancel-failed");
		wake = pthread_kill(workers[i], SIGUSR2);
		if (wake != 0 && wake != ESRCH)
			fail("worker-wake-failed");
		write_byte_or_fail(ipc_control[1][0], value);
		if (read_byte(ipc_control[0][0], &observed) != 0 || observed != value)
			fail("ipc-not-completed");
		if (pthread_join(workers[i], &result) != 0)
			fail("worker-join-failed");
		if (result != PTHREAD_CANCELED)
			fail("worker-not-canceled");
	}

	write_byte_or_fail(hold_gate[1], 0);
	expect_signal_byte(HOLDER_RELEASED, "holder-handler-not-released");
	s_handler_hold = 0;

	/* The reader peer is not canceled: one data byte ends its read. */
	write_byte_or_fail(reader_pipe[1], 0);

	if (pthread_join(reader, &result) != 0)
		fail("reader-join-failed");
	if (result != PEER_READER_SENTINEL)
		fail("reader-lost");
	if (pthread_join(holder, &result) != 0)
		fail("holder-join-failed");
	if (result != PEER_HOLDER_SENTINEL)
		fail("holder-lost");
	for (i = 0; i < IPC_PEERS; i++) {
		if (pthread_join(ipc_threads[i], &result) != 0)
			fail("ipc-peer-join-failed");
		if (result != ipc_context[i].sentinel)
			fail("ipc-peer-lost");
	}
	for (i = 0; i < IPC_PEERS; i++) {
		(void)close(ipc_control[i][0]);
		(void)close(ipc_control[i][1]);
	}

	for (i = 0; i < CANCEL_WORKERS; i++) {
		(void)close(worker_pipes[i][0]);
		(void)close(worker_pipes[i][1]);
	}
	(void)close(reader_pipe[0]);
	(void)close(reader_pipe[1]);
	(void)close(hold_gate[0]);
	(void)close(hold_gate[1]);
	(void)close(ipc_sockets[0]);
	(void)close(ipc_sockets[1]);
	s_hold_gate_fd = -1;

	printf("URGENT_SIGNAL_LIFECYCLE_CANCELED=%d\n", CANCEL_WORKERS);
	printf("URGENT_SIGNAL_LIFECYCLE_PEERS=%d\n", 1 + 1 + IPC_PEERS);
	printf("URGENT_SIGNAL_LIFECYCLE_SIGNALS=2\n");
}

/* ------------------------------------------------------------------------- */
/* Phase 2: a real debugger defers, suppresses, replaces and resumes          */
/* ------------------------------------------------------------------------- */

struct debug_scenario {
	/* Signal sent with pthread_kill, or 0 when the target traps itself. */
	int signal_number;
	int action;
	/* Expected application-handler byte; 0 means no handler may run. */
	int expected_signal;
	int exception;
	/* Expected exception code pair, checked against the real message. */
	int expected_code0;
	int expected_code1;
};

static const struct debug_scenario s_debug_scenarios[DEBUG_SCENARIOS] = {
	{SIGUSR1, DEBUG_ACTION_SUPPRESS, 0, EXC_SOFTWARE, EXC_SOFT_SIGNAL, SIGUSR1},
	{SIGUSR1, DEBUG_ACTION_REPLACE, SIGUSR2, EXC_SOFTWARE, EXC_SOFT_SIGNAL, SIGUSR1},
	{SIGUSR1, DEBUG_ACTION_NO_UPDATE, SIGUSR1, EXC_SOFTWARE, EXC_SOFT_SIGNAL, SIGUSR1},
	/* Asynchronous Unix SIGILL: still software, original restored on reply. */
	{SIGILL, DEBUG_ACTION_NO_UPDATE, SIGILL, EXC_SOFTWARE, EXC_SOFT_SIGNAL, SIGILL},
	/* Real user #UD: hardware fault with the saved PC on the ud2 itself. */
	{0, DEBUG_ACTION_RESUME_PC, 0, EXC_BAD_INSTRUCTION, EXC_I386_INVOP, 0},
};

static pthread_mutex_t s_debug_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_debug_cond = PTHREAD_COND_INITIALIZER;
static struct {
	int exception_received;
	int decision_ready;
	int action;
	int replied;
	int thupdate_rc;
	int thupdate_called;
	int reply_rc;
	int exception;
	int code0;
	int code1;
} s_debug;

static pthread_mutex_t s_target_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_target_cond = PTHREAD_COND_INITIALIZER;
static int s_target_go;
static int s_target_continued;
static pthread_t s_target_thread;

struct register_probe {
	_Atomic unsigned ready;
	_Atomic unsigned arm;
	_Atomic unsigned hardware;
	_Atomic unsigned observe;
	_Atomic unsigned resumed;
	_Atomic unsigned release;
	_Atomic uintptr_t trap_pc;
	_Atomic uintptr_t resume_pc;
	uintptr_t integer_input[2];
	uint64_t float_input[4] __attribute__((aligned(16)));
	uintptr_t integer_output[2];
	uint64_t float_output[4] __attribute__((aligned(16)));
};

static struct register_probe s_register_probe;
_Static_assert(sizeof(_Atomic unsigned) == 4, "probe flag instruction width");
_Static_assert(sizeof(_Atomic uintptr_t) == sizeof(uintptr_t), "probe address store width");

/* No C call crosses the live-register window.  Readiness is published only
 * after loading the patterns and publishing the trap/resume label addresses;
 * snapshots precede all subsequent C calls.  x86 aligned mov stores implement
 * the flags' atomic release publication.
 *
 * The signal scenarios (hardware == 0) skip the ud2 gate entirely.  The real
 * fault scenario (hardware == 1) waits, using only assembly, until main arms
 * it, then executes the labeled ud2 (0F 0B).  The resume label is the next
 * instruction, exactly two bytes later; the debugger moves the saved PC there
 * before replying.  Both labels and the fault are inside this single asm
 * block, so no compiler-generated code can intervene. */
static void hold_register_state(struct register_probe *probe)
{
	__asm__ volatile(
#if defined(__x86_64__)
		"movq %c[in0](%[base]), %%r12\n\t"
		"movq %c[in1](%[base]), %%r13\n\t"
#elif defined(__i386__)
		"movl %c[in0](%[base]), %%ebx\n\t"
		"movl %c[in1](%[base]), %%esi\n\t"
#else
#error This register oracle requires an x86 Darling guest.
#endif
		"movdqu %c[fp_in](%[base]), %%xmm0\n\t"
		"movdqu %c[fp_in1](%[base]), %%xmm1\n\t"
#if defined(__x86_64__)
		"leaq 2f(%%rip), %%rax\n\t"
		"movq %%rax, %c[trap](%[base])\n\t"
		"leaq 3f(%%rip), %%rax\n\t"
		"movq %%rax, %c[resume](%[base])\n\t"
#elif defined(__i386__)
		"leal 2f, %%eax\n\t"
		"movl %%eax, %c[trap](%[base])\n\t"
		"leal 3f, %%eax\n\t"
		"movl %%eax, %c[resume](%[base])\n\t"
#endif
		"movl $1, %c[ready](%[base])\n\t"
		"cmpl $0, %c[hardware](%[base])\n\t"
		"je 4f\n\t"
		"1:\n\t"
		"cmpl $0, %c[arm](%[base])\n\t"
		"je 1b\n\t"
		"2:\n\t"
		"ud2\n\t"
		"3:\n\t"
		"4:\n\t"
		"cmpl $0, %c[observe](%[base])\n\t"
		"je 5f\n\t"
		"movl $1, %c[resumed](%[base])\n\t"
		"5:\n\t"
		"cmpl $0, %c[release](%[base])\n\t"
		"jne 6f\n\t"
		"pause\n\t"
		"jmp 4b\n\t"
		"6:\n\t"
#if defined(__x86_64__)
		"movq %%r12, %c[out0](%[base])\n\t"
		"movq %%r13, %c[out1](%[base])\n\t"
#else
		"movl %%ebx, %c[out0](%[base])\n\t"
		"movl %%esi, %c[out1](%[base])\n\t"
#endif
		"movdqu %%xmm0, %c[fp_out](%[base])\n\t"
		"movdqu %%xmm1, %c[fp_out1](%[base])\n\t"
		:
		: [base] "D" (probe),
		  [ready] "i" (offsetof(struct register_probe, ready)),
		  [arm] "i" (offsetof(struct register_probe, arm)),
		  [hardware] "i" (offsetof(struct register_probe, hardware)),
		  [observe] "i" (offsetof(struct register_probe, observe)),
		  [resumed] "i" (offsetof(struct register_probe, resumed)),
		  [release] "i" (offsetof(struct register_probe, release)),
		  [trap] "i" (offsetof(struct register_probe, trap_pc)),
		  [resume] "i" (offsetof(struct register_probe, resume_pc)),
		  [in0] "i" (offsetof(struct register_probe, integer_input)),
		  [in1] "i" (offsetof(struct register_probe, integer_input) + sizeof(uintptr_t)),
		  [out0] "i" (offsetof(struct register_probe, integer_output)),
		  [out1] "i" (offsetof(struct register_probe, integer_output) + sizeof(uintptr_t)),
		  [fp_in] "i" (offsetof(struct register_probe, float_input)),
		  [fp_in1] "i" (offsetof(struct register_probe, float_input) + 16),
		  [fp_out] "i" (offsetof(struct register_probe, float_output)),
		  [fp_out1] "i" (offsetof(struct register_probe, float_output) + 16)
#if defined(__x86_64__)
		: "rax", "r12", "r13", "xmm0", "xmm1", "cc", "memory"
#else
		: "eax", "ebx", "esi", "xmm0", "xmm1", "cc", "memory"
#endif
	);
}

/* Integer thread state the debugger reads back from the interrupted thread. */
#if defined(__x86_64__)
typedef x86_thread_state64_t target_integer_state_t;
#define TARGET_INTEGER_STATE x86_THREAD_STATE64
#define TARGET_INTEGER_STATE_COUNT x86_THREAD_STATE64_COUNT
#if defined(__DARWIN_UNIX03) && __DARWIN_UNIX03
#define TARGET_STATE_PC(state) ((state).__rip)
#else
#define TARGET_STATE_PC(state) ((state).rip)
#endif
#elif defined(__i386__)
typedef x86_thread_state32_t target_integer_state_t;
#define TARGET_INTEGER_STATE x86_THREAD_STATE32
#define TARGET_INTEGER_STATE_COUNT x86_THREAD_STATE32_COUNT
#if defined(__DARWIN_UNIX03) && __DARWIN_UNIX03
#define TARGET_STATE_PC(state) ((state).__eip)
#else
#define TARGET_STATE_PC(state) ((state).eip)
#endif
#else
#error This register oracle requires an x86 Darling guest.
#endif

/*
 * A real #UD must not be "consumed" by replying: x86 UD2 leaves the saved
 * instruction pointer on the faulting instruction (Intel SDM), so replying
 * alone would resume at the same ud2 and fault again.  Read the interrupted
 * thread's actual integer state through its received Mach thread port, require
 * the exact trap label published by the target, then advance ONLY the PC to
 * the adjacent resume label with thread_set_state.  A second read proves every
 * other integer field survived; floating-point state is never read or written
 * here, so the target's own post-resume XMM snapshot is the live proof.
 */
static void debugger_advance_trap_pc(mach_port_t thread_port)
{
	target_integer_state_t state;
	target_integer_state_t original;
	target_integer_state_t reread;
	mach_msg_type_number_t count = TARGET_INTEGER_STATE_COUNT;
	mach_msg_type_number_t reread_count = TARGET_INTEGER_STATE_COUNT;
	uintptr_t trap_pc = atomic_load_explicit(&s_register_probe.trap_pc, memory_order_acquire);
	uintptr_t resume_pc = atomic_load_explicit(&s_register_probe.resume_pc, memory_order_acquire);
	kern_return_t result;

	if (trap_pc == 0 || resume_pc != trap_pc + 2)
		fail("debugger-trap-labels-invalid");

	memset(&state, 0, sizeof(state));
	result = thread_get_state(thread_port, TARGET_INTEGER_STATE,
		(thread_state_t)&state, &count);
	if (result != KERN_SUCCESS || count != TARGET_INTEGER_STATE_COUNT)
		fail("debugger-target-state-unavailable");
	if ((uintptr_t)TARGET_STATE_PC(state) != trap_pc)
		fail("debugger-trap-pc-wrong");

	original = state;
	TARGET_STATE_PC(state) = resume_pc;
	result = thread_set_state(thread_port, TARGET_INTEGER_STATE,
		(thread_state_t)&state, TARGET_INTEGER_STATE_COUNT);
	if (result != KERN_SUCCESS)
		fail("debugger-resume-state-rejected");

	result = thread_get_state(thread_port, TARGET_INTEGER_STATE,
		(thread_state_t)&reread, &reread_count);
	if (result != KERN_SUCCESS || reread_count != TARGET_INTEGER_STATE_COUNT)
		fail("debugger-resumed-state-unavailable");
	if ((uintptr_t)TARGET_STATE_PC(reread) != resume_pc)
		fail("debugger-resume-pc-not-set");
	TARGET_STATE_PC(reread) = TARGET_STATE_PC(original);
	if (memcmp(&reread, &original, sizeof(reread)) != 0)
		fail("debugger-changed-more-than-pc");
}

static mach_port_t s_exception_port = MACH_PORT_NULL;
static int s_progress_fd = -1;
static unsigned s_deferred_scenarios;

static void *debugger_thread(void *argument)
{
	unsigned int scenario;

	(void)argument;
	for (scenario = 0; scenario < DEBUG_SCENARIOS; scenario++) {
		struct {
			__Request__exception_raise_t body;
			mach_msg_trailer_t trailer;
		} incoming;
		__Request__exception_raise_t *request = &incoming.body;
		__Reply__exception_raise_t reply;
		mach_msg_return_t received;
		int action;
		int send_status;

		memset(&incoming, 0, sizeof(incoming));
		received = mach_msg(&request->Head, MACH_RCV_MSG, 0,
			(mach_msg_size_t)sizeof(incoming), s_exception_port,
			MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
		if (received != MACH_MSG_SUCCESS)
			fail("debugger-receive-failed");
		if (request->Head.msgh_id != EXC_RAISE_MESSAGE_ID)
			fail("debugger-unexpected-message");
		if (request->exception != s_debug_scenarios[scenario].exception ||
				request->codeCnt < 2 ||
				request->code[0] != s_debug_scenarios[scenario].expected_code0 ||
				request->code[1] != s_debug_scenarios[scenario].expected_code1)
			fail("debugger-unexpected-exception");

		pthread_mutex_lock(&s_debug_lock);
		s_debug.exception = request->exception;
		s_debug.code0 = request->code[0];
		s_debug.code1 = request->code[1];
		s_debug.exception_received = 1;
		pthread_cond_broadcast(&s_debug_cond);
		while (!s_debug.decision_ready)
			pthread_cond_wait(&s_debug_cond, &s_debug_lock);
		action = s_debug.action;
		pthread_mutex_unlock(&s_debug_lock);

		/* The debugger's decision is applied before it resumes the
		 * interrupted thread by replying to the exception. */
		if (action == DEBUG_ACTION_SUPPRESS || action == DEBUG_ACTION_REPLACE) {
			int signum = action == DEBUG_ACTION_SUPPRESS ? 0 : SIGUSR2;
			int status = ptrace(PT_THUPDATE, getpid(),
				(void *)(uintptr_t)request->thread.name, signum);

			pthread_mutex_lock(&s_debug_lock);
			s_debug.thupdate_called = 1;
			s_debug.thupdate_rc = status;
			pthread_mutex_unlock(&s_debug_lock);
			if (status != 0)
				fail("debugger-thupdate-failed");
		} else if (action == DEBUG_ACTION_RESUME_PC) {
			/* A real hardware fault is not consumed by the reply: the
			 * saved PC still names the ud2.  Read the actual target
			 * state and advance only that PC, before replying. */
			debugger_advance_trap_pc(request->thread.name);
		}

		memset(&reply, 0, sizeof(reply));
		reply.Head.msgh_bits = MACH_MSGH_BITS(
			MACH_MSGH_BITS_REMOTE(request->Head.msgh_bits), 0);
		reply.Head.msgh_size = (mach_msg_size_t)sizeof(reply);
		reply.Head.msgh_remote_port = request->Head.msgh_remote_port;
		reply.Head.msgh_local_port = MACH_PORT_NULL;
		reply.Head.msgh_id = request->Head.msgh_id + 100;
		reply.NDR = NDR_record;
		reply.RetCode = KERN_SUCCESS;
		send_status = (int)mach_msg(&reply.Head, MACH_SEND_MSG,
			reply.Head.msgh_size, 0, MACH_PORT_NULL,
			MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

		pthread_mutex_lock(&s_debug_lock);
		s_debug.reply_rc = send_status;
		s_debug.replied = 1;
		pthread_cond_broadcast(&s_debug_cond);
		pthread_mutex_unlock(&s_debug_lock);
		if (send_status != 0)
			fail("debugger-reply-failed");

		(void)mach_port_deallocate(mach_task_self(), request->thread.name);
		(void)mach_port_deallocate(mach_task_self(), request->task.name);
	}
	return NULL;
}

static void *target_thread(void *argument)
{
	sigset_t original;
	sigset_t signals;
	unsigned int scenario;

	(void)argument;
	if (pthread_sigmask(SIG_SETMASK, NULL, &original) != 0)
		fail("target-mask-read-failed");
	sigemptyset(&signals);
	sigaddset(&signals, SIGUSR1);
	sigaddset(&signals, SIGUSR2);
	sigaddset(&signals, SIGILL);

	for (scenario = 0; scenario < DEBUG_SCENARIOS; scenario++) {
		pthread_mutex_lock(&s_target_lock);
		while (s_target_go != (int)scenario + 1)
			pthread_cond_wait(&s_target_cond, &s_target_lock);
		pthread_mutex_unlock(&s_target_lock);
		if (pthread_sigmask(SIG_UNBLOCK, &signals, NULL) != 0)
			fail("target-mask-unblock-failed");

		s_register_probe.integer_input[0] = (uintptr_t)UINT64_C(0x13579bdf2468ace0) ^ scenario;
		s_register_probe.integer_input[1] = (uintptr_t)UINT64_C(0xfedcba9876543210) ^ scenario;
		s_register_probe.float_input[0] = UINT64_C(0x0123456789abcdef) ^ scenario;
		s_register_probe.float_input[1] = UINT64_C(0xfedcba9876543210);
		s_register_probe.float_input[2] = UINT64_C(0x55aa33cc77ee1199);
		s_register_probe.float_input[3] = UINT64_C(0xa55ac33ce77e9119) ^ scenario;
		hold_register_state(&s_register_probe);
		if (memcmp(s_register_probe.integer_input, s_register_probe.integer_output,
				sizeof(s_register_probe.integer_input)) != 0 ||
				memcmp(s_register_probe.float_input, s_register_probe.float_output,
				sizeof(s_register_probe.float_input)) != 0)
			fail("target-register-state-clobbered");
		if (pthread_sigmask(SIG_SETMASK, &original, NULL) != 0)
			fail("target-mask-restore-failed");

		pthread_mutex_lock(&s_target_lock);
		s_target_continued = 1;
		pthread_cond_broadcast(&s_target_cond);
		pthread_mutex_unlock(&s_target_lock);
	}
	return TARGET_SENTINEL;
}

static void *progress_peer(void *argument)
{
	int fd = (int)(intptr_t)argument;
	unsigned char value;

	for (;;) {
		if (read_byte(fd, &value) != 0)
			fail("progress-peer-read-failed");
		if (value == PROGRESS_STOP)
			break;
		if (write_byte(fd, value) != 0)
			fail("progress-peer-write-failed");
	}
	return PROGRESS_SENTINEL;
}

static void target_start(int sequence, int hardware)
{
	atomic_store_explicit(&s_register_probe.ready, 0, memory_order_relaxed);
	atomic_store_explicit(&s_register_probe.arm, 0, memory_order_relaxed);
	atomic_store_explicit(&s_register_probe.hardware, hardware ? 1u : 0u, memory_order_relaxed);
	atomic_store_explicit(&s_register_probe.observe, 0, memory_order_relaxed);
	atomic_store_explicit(&s_register_probe.resumed, 0, memory_order_relaxed);
	atomic_store_explicit(&s_register_probe.release, 0, memory_order_relaxed);
	atomic_store_explicit(&s_register_probe.trap_pc, 0, memory_order_relaxed);
	atomic_store_explicit(&s_register_probe.resume_pc, 0, memory_order_relaxed);
	pthread_mutex_lock(&s_target_lock);
	s_target_go = sequence;
	s_target_continued = 0;
	pthread_cond_broadcast(&s_target_cond);
	pthread_mutex_unlock(&s_target_lock);
	while (!atomic_load_explicit(&s_register_probe.ready, memory_order_acquire)) {
		if (sched_yield() != 0)
			fail("target-readiness-yield-failed");
	}
}

static int target_is_continued(void)
{
	int value;

	pthread_mutex_lock(&s_target_lock);
	value = s_target_continued;
	pthread_mutex_unlock(&s_target_lock);
	return value;
}

static void target_wait_continued(void)
{
	pthread_mutex_lock(&s_target_lock);
	while (!s_target_continued)
		pthread_cond_wait(&s_target_cond, &s_target_lock);
	pthread_mutex_unlock(&s_target_lock);
}

static void progress_burst(void)
{
	unsigned int round;

	for (round = 0; round < PROGRESS_ROUNDS; round++) {
		unsigned char value = (unsigned char)(round + 1u);
		unsigned char echo;

		write_byte_or_fail(s_progress_fd, value);
		if (read_byte(s_progress_fd, &echo) != 0)
			fail("progress-roundtrip-failed");
		if (echo != value)
			fail("progress-roundtrip-failed");
	}
}

static void wait_exception_received(const struct debug_scenario *scenario)
{
	pthread_mutex_lock(&s_debug_lock);
	while (!s_debug.exception_received)
		pthread_cond_wait(&s_debug_cond, &s_debug_lock);
	if (s_debug.exception != scenario->exception ||
			s_debug.code0 != scenario->expected_code0 ||
			s_debug.code1 != scenario->expected_code1)
		fail("debugger-wrong-signal");
	pthread_mutex_unlock(&s_debug_lock);
}

static void run_debug_scenario(unsigned int sequence, const struct debug_scenario *scenario)
{
	unsigned char handler_bytes[16];
	size_t handler_count;
	int hardware = scenario->action == DEBUG_ACTION_RESUME_PC;
	int expect_thupdate = scenario->action == DEBUG_ACTION_SUPPRESS ||
		scenario->action == DEBUG_ACTION_REPLACE;

	pthread_mutex_lock(&s_debug_lock);
	s_debug.exception_received = 0;
	s_debug.decision_ready = 0;
	s_debug.action = scenario->action;
	s_debug.replied = 0;
	s_debug.thupdate_rc = 0;
	s_debug.thupdate_called = 0;
	s_debug.reply_rc = 0;
	pthread_mutex_unlock(&s_debug_lock);

	target_start((int)sequence, hardware);
	if (hardware) {
		/* The target is armed inside its live-register window and
		 * executes the real ud2 itself; no asynchronous signal is sent. */
		atomic_store_explicit(&s_register_probe.arm, 1, memory_order_release);
	} else if (pthread_kill(s_target_thread, scenario->signal_number) != 0) {
		fail("target-signal-failed");
	}
	wait_exception_received(scenario);
	atomic_store_explicit(&s_register_probe.observe, 1, memory_order_release);

	/* The debugger holds the genuine exception; SIGPROCESS must not have
	 * completed: no delivery and no continuation may have happened. */
	handler_count = read_handler_bytes(handler_bytes, sizeof(handler_bytes));
	if (handler_count != 0)
		fail("signal-delivered-before-debugger-reply");
	if (target_is_continued() ||
			atomic_load_explicit(&s_register_probe.resumed, memory_order_acquire))
		fail("sigprocess-completed-before-debugger-reply");

	progress_burst();

	handler_count = read_handler_bytes(handler_bytes, sizeof(handler_bytes));
	if (handler_count != 0)
		fail("signal-delivered-before-debugger-reply");
	if (target_is_continued() ||
			atomic_load_explicit(&s_register_probe.resumed, memory_order_acquire))
		fail("sigprocess-completed-before-debugger-reply");
	s_deferred_scenarios++;

	pthread_mutex_lock(&s_debug_lock);
	s_debug.decision_ready = 1;
	pthread_cond_broadcast(&s_debug_cond);
	while (!s_debug.replied)
		pthread_cond_wait(&s_debug_cond, &s_debug_lock);
	if (s_debug.thupdate_rc != 0)
		fail("debugger-thupdate-failed");
	if (s_debug.thupdate_called != expect_thupdate)
		fail("debugger-thupdate-unexpected");
	if (s_debug.reply_rc != 0)
		fail("debugger-reply-failed");
	pthread_mutex_unlock(&s_debug_lock);
	atomic_store_explicit(&s_register_probe.release, 1, memory_order_release);

	target_wait_continued();
	if (!atomic_load_explicit(&s_register_probe.resumed, memory_order_acquire))
		fail("target-never-resumed");

	handler_count = read_handler_bytes(handler_bytes, sizeof(handler_bytes));
	if (scenario->expected_signal == 0) {
		if (handler_count != 0)
			fail("suppressed-signal-delivered");
	} else if (handler_count != 1 || handler_bytes[0] != (unsigned char)scenario->expected_signal) {
		fail("handler-outcome-wrong");
	}
}

static void debugger_phase(void)
{
	pthread_t debugger;
	pthread_t target;
	pthread_t progress;
	int progress_sockets[2];
	kern_return_t result;
	unsigned int i;
	void *thread_result;

	s_handler_hold = 0;
	install_replacement_handler();
	make_read_nonblocking(s_signal_pipe[0]);

	result = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &s_exception_port);
	if (result != KERN_SUCCESS)
		fail("exception-port-unavailable");
	result = mach_port_insert_right(mach_task_self(), s_exception_port, s_exception_port,
		MACH_MSG_TYPE_MAKE_SEND);
	if (result != KERN_SUCCESS)
		fail("exception-port-unavailable");

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, progress_sockets) != 0)
		fail("progress-socketpair-unavailable");
	s_progress_fd = progress_sockets[0];

	/* Threads exist before sigexc is enabled, so no unrelated signal
	 * generated by thread creation can reach the debugger port. */
	if (pthread_create(&debugger, NULL, debugger_thread, NULL) != 0)
		fail("debugger-create-failed");
	if (pthread_create(&target, NULL, target_thread, NULL) != 0)
		fail("target-create-failed");
	s_target_thread = target;
	if (pthread_create(&progress, NULL, progress_peer,
			(void *)(intptr_t)progress_sockets[1]) != 0)
		fail("progress-peer-create-failed");

	result = task_set_exception_ports(mach_task_self(), EXC_MASK_SOFTWARE | EXC_MASK_BAD_INSTRUCTION,
		s_exception_port, EXCEPTION_DEFAULT, 0 /* THREAD_STATE_NONE */);
	if (result != KERN_SUCCESS)
		fail("exception-port-set-failed");
	if (ptrace(PT_SIGEXC, getpid(), NULL, 0) != 0)
		fail("sigexc-enable-refused");

	for (i = 0; i < DEBUG_SCENARIOS; i++)
		run_debug_scenario(i + 1, &s_debug_scenarios[i]);

	write_byte_or_fail(s_progress_fd, PROGRESS_STOP);
	if (pthread_join(progress, &thread_result) != 0)
		fail("progress-join-failed");
	if (thread_result != PROGRESS_SENTINEL)
		fail("progress-lost");
	if (pthread_join(target, &thread_result) != 0)
		fail("target-join-failed");
	if (thread_result != TARGET_SENTINEL)
		fail("target-lost");
	if (pthread_join(debugger, NULL) != 0)
		fail("debugger-join-failed");

	if (ptrace(PT_DETACH, getpid(), NULL, 0) != 0)
		fail("sigexc-disable-failed");
	(void)task_set_exception_ports(mach_task_self(), EXC_MASK_SOFTWARE | EXC_MASK_BAD_INSTRUCTION,
		MACH_PORT_NULL, EXCEPTION_DEFAULT, 0);
	(void)mach_port_deallocate(mach_task_self(), s_exception_port);
	s_exception_port = MACH_PORT_NULL;
	if (sigaction(SIGILL, &s_previous_sigill_action, NULL) != 0)
		fail("sigill-handler-restore-failed");

	(void)close(progress_sockets[0]);
	(void)close(progress_sockets[1]);

	printf("URGENT_SIGNAL_LIFECYCLE_DEFERRED=%u\n", s_deferred_scenarios);
	printf("URGENT_SIGNAL_LIFECYCLE_SUPPRESSED=1\n");
	printf("URGENT_SIGNAL_LIFECYCLE_REPLACED=1\n");
	printf("URGENT_SIGNAL_LIFECYCLE_ORIGINAL=1\n");
	printf("URGENT_SIGNAL_LIFECYCLE_SIGILL_SOFTWARE=1\n");
	printf("URGENT_SIGNAL_LIFECYCLE_HARDWARE_RESUMED=1\n");
}

int main(void)
{
	pthread_t watchdog;
	sigset_t watchdog_block;
	sigset_t original_mask;

	setvbuf(stdout, NULL, _IONBF, 0);

	if (pipe(s_signal_pipe) != 0)
		fail("signal-pipe-unavailable");
	install_signal_handlers();

	/* The watchdog must never consume a process-directed nested-test signal.
	 * Block before creation so there is no child-startup mask race. */
	sigemptyset(&watchdog_block);
	sigaddset(&watchdog_block, SIGUSR1);
	if (pthread_sigmask(SIG_BLOCK, &watchdog_block, &original_mask) != 0)
		fail("watchdog-mask-block-failed");
	if (pthread_create(&watchdog, NULL, watchdog_thread, NULL) != 0)
		fail("watchdog-start-failed");
	if (pthread_sigmask(SIG_SETMASK, &original_mask, NULL) != 0)
		fail("watchdog-mask-restore-failed");

	nested_signal_phase();

	cancellation_phase();
	debugger_phase();

	finish_watchdog(watchdog);
	printf("URGENT_SIGNAL_LIFECYCLE_PASS=1\n");
	return 0;
}
