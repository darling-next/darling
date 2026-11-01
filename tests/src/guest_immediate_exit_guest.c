/*
 * Darwin _exit must skip inherited host finalizers; guest exit(3) must still
 * run guest handlers. Register a real glibc __cxa_atexit callback through the
 * existing loader bridge. A host-exit child proves the pipe oracle is live,
 * an _exit(127) child must leave it empty, and an exit(3) child must run its
 * guest handler while preserving the exact exit status.
 *
 * Each child has a five-second monotonic deadline. Cleanup signals only an
 * owned, unreaped child. The loader bridge is Darling-specific; missing
 * prerequisites fail rather than becoming a native-Darwin or runtime PASS.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Keep the consumer deadline well below the workload's 30 s catalog bound. */
#define GUEST_EXIT_DEADLINE_MS 5000
#define GUEST_EXIT_POLL_MS 10

/* Authoritative first four members of src/startup/mldr/elfcalls/elfcalls.h.
 * CLT does not ship this private header. Only the existing native dlopen and
 * dlsym members are used; the intervening members preserve their ABI offsets. */
struct elf_calls_prefix {
	void *(*dlopen)(const char *name);
	int (*dlclose)(void *lib);
	void *(*dlsym)(void *lib, const char *name);
	char *(*dlerror)(void);
};

static int s_host_marker_fd = -1;
static ssize_t (*s_host_write)(int fd, const void *buffer, size_t length) = NULL;

/*
 * Runs on host glibc's atexit chain.  It deliberately uses only the host
 * write resolved before fork: no stdio, no guest libc call, no lock that the
 * host exit path could already hold.  A byte here means host cleanup ran
 * while the guest asked for immediate termination.
 */
static void host_atexit_marker(void *context)
{
	static const char byte = 'E';

	(void)context;
	if (s_host_marker_fd >= 0 && s_host_write != NULL)
		(void)s_host_write(s_host_marker_fd, &byte, 1);
}

static int s_control_fd = -1;

/* Runs on guest libc's atexit chain in the exit(3) control child. */
static void guest_atexit_marker(void)
{
	static const char payload[] = "guest-atexit";

	if (s_control_fd >= 0)
		(void)write(s_control_fd, payload, sizeof(payload) - 1);
}

static long long monotonic_ms(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return -1;
	return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void fail(const char *reason)
{
	printf("GUEST_IMMEDIATE_EXIT_FAIL=%s\n", reason);
	fflush(stdout);
	_exit(1);
}

static void make_read_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);

	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		fail("pipe-nonblocking-failed");
}

/*
 * Reaps the exact child this fixture owns before the monotonic deadline.
 * Returns 0 when the process was reaped, 1 when the deadline expired and the
 * caller must kill and reap it, and -1 on a wait error.
 */
static int wait_bounded(pid_t child, int *status, long long deadline_ms)
{
	for (;;) {
		pid_t result = waitpid(child, status, WNOHANG);
		long long now;

		if (result == child)
			return 0;
		if (result < 0 && errno != EINTR)
			return errno == ECHILD ? -2 : -1;
		now = monotonic_ms();
		if (now < 0)
			return -1;
		if (now >= deadline_ms)
			return 1;

		struct timespec slice = {0, GUEST_EXIT_POLL_MS * 1000000L};
		(void)nanosleep(&slice, NULL);
	}
}

/* Kills and reaps only the pid returned by this fixture's own fork(2).  The
 * pid has not been reaped yet, so the kernel cannot have reused it. */
static void kill_and_reap(pid_t child, int *status)
{
	(void)kill(child, SIGKILL);
	while (waitpid(child, status, 0) < 0 && errno == EINTR)
		;
}

static ssize_t drain_pipe(int fd, char *buffer, size_t capacity)
{
	size_t total = 0;

	for (;;) {
		ssize_t count;

		if (total == capacity)
			break;
		count = read(fd, buffer + total, capacity - total);
		if (count > 0) {
			total += (size_t)count;
			continue;
		}
		if (count == 0)
			break;
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			break;
		return -1;
	}
	return (ssize_t)total;
}

int main(void)
{
	struct elf_calls_prefix **calls_slot;
	struct elf_calls_prefix *calls;
	void *kernel;
	void *host_libc;
	void *host_atexit_raw;
	void *host_write_raw;
	void *host_exit_raw;
	int (*host_atexit)(void (*)(void *), void *, void *) = NULL;
	void (*host_exit)(int) = NULL;
	int oracle_pipe[2];
	int marker_pipe[2];
	int control_pipe[2];
	pid_t oracle;
	pid_t child;
	pid_t control;
	int status = 0;
	int wait_result;
	long long started;
	long long deadline;
	char marker[16];
	ssize_t marker_length;

	setvbuf(stdout, NULL, _IONBF, 0);

	/*
	 * The existing native bridge: the installed libsystem_kernel exports
	 * its loader/guest table as `_elfcalls` (dlsym takes the C name, and
	 * dyld prefixes the Mach-O underscore).  The address returned is the
	 * address of the variable, hence the double indirection.
	 */
	kernel = dlopen("/usr/lib/system/libsystem_kernel.dylib", RTLD_NOW);
	if (kernel == NULL)
		fail("loader-image-unavailable");
	calls_slot = (struct elf_calls_prefix **)dlsym(kernel, "_elfcalls");
	if (calls_slot == NULL || *calls_slot == NULL)
		fail("loader-bridge-unavailable");
	calls = *calls_slot;

	/*
	 * Resolve host glibc through the loader's own dlopen/dlsym.  The
	 * function pointers live in this process and are called with their
	 * native prototypes; there is no hand-built table, struct cast or
	 * fixed integer address here.
	 */
	host_libc = calls->dlopen("libc.so.6");
	if (host_libc == NULL)
		fail("host-libc-unavailable");
	host_atexit_raw = calls->dlsym(host_libc, "__cxa_atexit");
	host_write_raw = calls->dlsym(host_libc, "write");
	host_exit_raw = calls->dlsym(host_libc, "exit");
	if (host_atexit_raw == NULL)
		fail("host-atexit-unavailable");
	if (host_write_raw == NULL)
		fail("host-write-unavailable");
	if (host_exit_raw == NULL)
		fail("host-exit-unavailable");
	/*
	 * dlsym returns a void *; these conversions are width-correct on the
	 * native ABI and keep the exact declared function types.
	 * __cxa_atexit registers on the real glibc __exit_funcs chain.
	 * POSIX dlsym permits the object-pointer/function-pointer conversion.
	 */
	host_atexit = (int (*)(void (*)(void *), void *, void *))host_atexit_raw;
	s_host_write = (ssize_t (*)(int, const void *, size_t))host_write_raw;
	host_exit = (void (*)(int))host_exit_raw;

	if (host_atexit(host_atexit_marker, NULL, NULL) != 0)
		fail("host-atexit-registration-failed");

	/*
	 * Positive control: the marker must be reachable when host exit
	 * actually runs.  A child calls the host exit entry point directly
	 * (not through the guest boundary), so exactly one byte and status 0
	 * must arrive on both the unfixed and fixed images.  This proves the
	 * oracle is live rather than a silently unregistered callback.
	 */
	if (pipe(oracle_pipe) != 0)
		fail("oracle-pipe-unavailable");
	make_read_nonblocking(oracle_pipe[0]);
	s_host_marker_fd = oracle_pipe[1];
	fflush(stdout);
	started = monotonic_ms();
	if (started < 0)
		fail("monotonic-clock-unavailable");
	oracle = fork();
	if (oracle < 0)
		fail("oracle-fork-failed");
	if (oracle == 0) {
		close(oracle_pipe[0]);
		host_exit(0);
		_exit(98);
	}

	close(oracle_pipe[1]);
	s_host_marker_fd = -1;
	deadline = started + GUEST_EXIT_DEADLINE_MS;
	wait_result = wait_bounded(oracle, &status, deadline);
	if (wait_result == 1) {
		kill_and_reap(oracle, &status);
		fail("host-atexit-oracle-stalled");
	}
	if (wait_result < 0) {
		if (wait_result == -1)
			kill_and_reap(oracle, &status);
		fail("host-atexit-oracle-wait-failed");
	}
	marker_length = drain_pipe(oracle_pipe[0], marker, sizeof(marker));
	if (marker_length < 0)
		fail("host-atexit-oracle-read-failed");
	close(oracle_pipe[0]);
	if (marker_length <= 0)
		fail("host-atexit-oracle-not-live");
	if (marker_length != 1)
		fail("host-atexit-oracle-duplicated");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		fail("host-atexit-oracle-wrong-status");

	if (pipe(marker_pipe) != 0)
		fail("marker-pipe-unavailable");
	make_read_nonblocking(marker_pipe[0]);
	s_host_marker_fd = marker_pipe[1];

	fflush(stdout);
	started = monotonic_ms();
	if (started < 0)
		fail("monotonic-clock-unavailable");
	child = fork();
	if (child < 0)
		fail("fork-failed");
	if (child == 0) {
		/* The inherited host atexit chain is the thing under test. */
		close(marker_pipe[0]);
		_exit(127);
	}

	close(marker_pipe[1]);
	s_host_marker_fd = -1;

	deadline = started + GUEST_EXIT_DEADLINE_MS;
	wait_result = wait_bounded(child, &status, deadline);
	if (wait_result == 1) {
		kill_and_reap(child, &status);
		fail("immediate-exit-stalled-in-host-atexit");
	}
	if (wait_result < 0) {
		if (wait_result == -1)
			kill_and_reap(child, &status);
		fail("immediate-exit-wait-failed");
	}

	marker_length = drain_pipe(marker_pipe[0], marker, sizeof(marker));
	if (marker_length < 0)
		fail("immediate-exit-read-failed");
	close(marker_pipe[0]);
	if (marker_length > 0)
		fail("host-atexit-ran-on-immediate-exit");

	if (!WIFEXITED(status)) {
		if (WIFSIGNALED(status))
			printf("GUEST_IMMEDIATE_EXIT_SIGNAL=%d\n", WTERMSIG(status));
		fail("immediate-exit-not-exited");
	}
	if (WEXITSTATUS(status) != 127) {
		printf("GUEST_IMMEDIATE_EXIT_STATUS=%d\n", WEXITSTATUS(status));
		fail("wrong-immediate-exit-status");
	}
	printf("GUEST_IMMEDIATE_EXIT_STATUS=127\n");
	printf("GUEST_IMMEDIATE_EXIT_ELAPSED_MS=%lld\n", monotonic_ms() - started);

	/*
	 * Control: guest exit(3) must still run the guest libc's own atexit
	 * handler before reaching the loader boundary.  The handler reports
	 * through its own pipe so "ran", "did not run" and "ran twice" are
	 * distinguishable rather than inferred from stdout interleaving.
	 */
	if (pipe(control_pipe) != 0)
		fail("control-pipe-unavailable");
	make_read_nonblocking(control_pipe[0]);
	fflush(stdout);
	started = monotonic_ms();
	if (started < 0)
		fail("monotonic-clock-unavailable");
	control = fork();
	if (control < 0)
		fail("control-fork-failed");
	if (control == 0) {
		close(control_pipe[0]);
		s_control_fd = control_pipe[1];
		if (atexit(guest_atexit_marker) != 0)
			_exit(97);
		exit(3);
	}

	close(control_pipe[1]);
	deadline = started + GUEST_EXIT_DEADLINE_MS;
	wait_result = wait_bounded(control, &status, deadline);
	if (wait_result == 1) {
		kill_and_reap(control, &status);
		fail("guest-exit-stalled");
	}
	if (wait_result < 0) {
		if (wait_result == -1)
			kill_and_reap(control, &status);
		fail("control-wait-failed");
	}

	marker_length = drain_pipe(control_pipe[0], marker, sizeof(marker));
	if (marker_length < 0)
		fail("guest-exit-read-failed");
	close(control_pipe[0]);
	if (marker_length <= 0)
		fail("guest-atexit-did-not-run");
	if (marker_length != (ssize_t)(sizeof("guest-atexit") - 1) ||
			memcmp(marker, "guest-atexit", sizeof("guest-atexit") - 1) != 0)
		fail("guest-atexit-ran-wrong-number-of-times");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 3)
		fail("wrong-guest-exit-status");

	printf("GUEST_EXIT_ATEXIT_RAN=1\n");
	printf("GUEST_EXIT_ELAPSED_MS=%lld\n", monotonic_ms() - started);
	printf("GUEST_IMMEDIATE_EXIT_PASS=1\n");
	return 0;
}
