#include "../rootless_shutdown.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
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

static void terminate_worker(int signal_number)
{
	(void)signal_number;
	char reply;
	if (write(client, "T", 1) != 1 || read(client, &reply, 1) != 1 || reply != 'A')
		_exit(43);
	_exit(42);
}

static void announce(int fd, int role)
{
	struct ready ready = { .role = role, .pid = getpid() };
	if (write(fd, &ready, sizeof(ready)) != sizeof(ready))
		_exit(44);
}

static int run_fixture(int separate_sessions)
{
	int ready_pipe[2];
	int sockets[2];
	if (pipe(ready_pipe) != 0 || socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
		return 1;
	client = sockets[1];
	pid_t server = fork();
	if (server < 0)
		return 1;
	if (server == 0) {
		close(sockets[1]);
		char request;
		while (read(sockets[0], &request, 1) == 1) {
			/* Termination needs a live server, not merely successful kill(). */
			struct timespec delay = { .tv_nsec = 200000000L };
			nanosleep(&delay, NULL);
			if (request != 'T' || write(sockets[0], "A", 1) != 1)
				_exit(45);
		}
		_exit(0);
	}
	close(sockets[0]);
	pid_t leader = fork();
	if (leader < 0)
		return 1;
	if (leader == 0) {
		close(ready_pipe[0]);
		if (setsid() < 0)
			_exit(46);
		pid_t worker = fork();
		if (worker < 0)
			_exit(47);
		if (worker == 0) {
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
				announce(ready_pipe[1], 2);
			} else {
				announce(ready_pipe[1], 1);
			}
		} else {
			announce(ready_pipe[1], 0);
		}
		for (;;)
			pause();
	}
	close(ready_pipe[1]);
	close(sockets[1]);
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
	close(ready_pipe[0]);
	if (!failed && shutdown_rootless_process_tree(leader) != 0)
		failed = 1;
	/* The caller may now stop its server. Any still-running termination
	 * handler loses the reply and produces a distinct failing exit status. */
	if (kill(server, 0) != 0)
		failed = 1;
	kill(server, SIGKILL);
	int status;
	waitpid(server, &status, 0);
	for (int i = 0; i < 3; ++i) {
		if (guests[i] <= 0)
			continue;
		pid_t reaped = waitpid(guests[i], &status, WNOHANG);
		if (reaped != guests[i]) {
			failed = 1;
			kill(guests[i], SIGKILL);
			waitpid(guests[i], &status, 0);
			continue;
		}
		if (i == 1 ? !(WIFEXITED(status) && WEXITSTATUS(status) == 42) :
				!(WIFSIGNALED(status) && WTERMSIG(status) == (i == 0 ? SIGTERM : SIGKILL)))
			failed = 1;
	}
	if (failed)
		fprintf(stderr, "rootless shutdown failed guest-tree termination (separate_sessions=%d)\n",
			separate_sessions);
	return failed;
}

int main(void)
{
	alarm(15);
	if (prctl(PR_SET_CHILD_SUBREAPER, 1) != 0)
		return 1;
	int failed = run_fixture(0);
	failed |= run_fixture(1);
	if (failed)
		return 1;
	puts("ROOTLESS_SHUTDOWN_SESSION_OK");
	return 0;
}
