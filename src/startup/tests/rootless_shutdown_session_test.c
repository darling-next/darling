#include "../rootless_shutdown.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct ready {
	int role;
	pid_t pid;
};

static int client;
static int notifications;

static void announce_pid(int role, pid_t pid)
{
	struct ready ready = { .role = role, .pid = pid };
	if (write(notifications, &ready, sizeof(ready)) != sizeof(ready))
		_exit(44);
}

static void terminate_worker(int signal_number)
{
	(void)signal_number;
	char reply;
	if (write(client, "T", 1) != 1 || read(client, &reply, 1) != 1 || reply != 'A')
		_exit(43);
	/* A real TERM handler can fork and exit. The child changes session and
	 * is adopted by the live server, not by its dead guest parent. */
	pid_t child = fork();
	if (child < 0)
		_exit(54);
	if (child == 0) {
		if (setsid() < 0)
			_exit(55);
		signal(SIGTERM, SIG_IGN);
		for (;;)
			pause();
	}
	announce_pid(3, child);
	_exit(42);
}

static void restart_worker(int signal_number)
{
	(void)signal_number;
	pid_t child = fork();
	if (child == 0) {
		signal(SIGTERM, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);
		for (;;)
			pause();
	}
	if (child > 0)
		announce_pid(3, child);
}

static void guest_init(int separate_sessions)
{
	if (setsid() < 0)
		_exit(46);
	signal(SIGTERM, SIG_IGN);
	struct sigaction restart = { .sa_handler = restart_worker };
	sigemptyset(&restart.sa_mask);
	if (sigaction(SIGCHLD, &restart, NULL) != 0)
		_exit(53);
	pid_t worker = fork();
	if (worker < 0)
		_exit(47);
	if (worker == 0) {
		signal(SIGCHLD, SIG_DFL);
		if (separate_sessions && setsid() < 0)
			_exit(48);
		struct sigaction action = { .sa_handler = terminate_worker };
		sigemptyset(&action.sa_mask);
		if (sigaction(SIGTERM, &action, NULL) != 0)
			_exit(49);
		pid_t stubborn = fork();
		if (stubborn < 0)
			_exit(50);
		if (stubborn == 0) {
			if (separate_sessions && setsid() < 0)
				_exit(51);
			signal(SIGTERM, SIG_IGN);
			announce_pid(2, getpid());
		} else {
			announce_pid(1, getpid());
		}
	} else {
		announce_pid(0, getpid());
	}
	for (;;)
		pause();
}

static int run_fixture(int separate_sessions)
{
	int ready_pipe[2];
	int sockets[2];
	if (pipe(ready_pipe) != 0 || socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
		return 1;
	client = sockets[1];
	notifications = ready_pipe[1];
	pid_t server = fork();
	if (server < 0)
		return 1;
	if (server == 0) {
		close(ready_pipe[0]);
		if (prctl(PR_SET_CHILD_SUBREAPER, 1) != 0)
			_exit(56);
		pid_t init = fork();
		if (init < 0)
			_exit(57);
		if (init == 0) {
			close(sockets[0]);
			guest_init(separate_sessions);
		}
		close(sockets[1]);
		close(ready_pipe[1]);
		char request;
		while (read(sockets[0], &request, 1) == 1) {
			struct timespec delay = { .tv_nsec = 200000000L };
			nanosleep(&delay, NULL);
			if (request != 'T' || write(sockets[0], "A", 1) != 1)
				_exit(45);
		}
		for (;;)
			pause();
	}
	close(sockets[0]);
	close(sockets[1]);
	close(ready_pipe[1]);
	pid_t guests[3] = { 0 };
	int failed = 0;
	for (int i = 0; i < 3; ++i) {
		struct ready ready;
		if (read(ready_pipe[0], &ready, sizeof(ready)) != sizeof(ready) ||
				ready.role < 0 || ready.role >= 3 || guests[ready.role] != 0) {
			failed = 1;
			break;
		}
		guests[ready.role] = ready.pid;
	}
	if (!failed && shutdown_rootless_process_tree(server) != 0)
		failed = 1;
	int status;
	if (waitpid(server, &status, WNOHANG) != server) {
		failed = 1;
		kill(server, SIGKILL);
		waitpid(server, &status, 0);
	}
	for (int i = 0; i < 3; ++i) {
		if (guests[i] <= 0)
			continue;
		if (waitpid(guests[i], &status, WNOHANG) != guests[i]) {
			failed = 1;
			kill(guests[i], SIGKILL);
			waitpid(guests[i], &status, 0);
			continue;
		}
		if (i == 1 ? !(WIFEXITED(status) && WEXITSTATUS(status) == 42) : !WIFSIGNALED(status))
			failed = 1;
	}
	fcntl(ready_pipe[0], F_SETFL, O_NONBLOCK);
	struct ready replacement;
	while (read(ready_pipe[0], &replacement, sizeof(replacement)) == sizeof(replacement)) {
		if (waitpid(replacement.pid, &status, WNOHANG) != replacement.pid) {
			failed = 1;
			fprintf(stderr, "late guest survived shutdown: %d\n", replacement.pid);
			kill(replacement.pid, SIGKILL);
			waitpid(replacement.pid, &status, 0);
		}
	}
	close(ready_pipe[0]);
	if (failed)
		fprintf(stderr, "rootless shutdown failed guest-domain termination (separate_sessions=%d)\n",
			separate_sessions);
	return failed;
}

int main(void)
{
	alarm(20);
	if (prctl(PR_SET_CHILD_SUBREAPER, 1) != 0)
		return 1;
	int failed = run_fixture(0);
	failed |= run_fixture(1);
	if (failed)
		return 1;
	puts("ROOTLESS_SHUTDOWN_SESSION_OK");
	return 0;
}
