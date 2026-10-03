#include "rootless_shutdown.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* The live rootless server is a Linux child subreaper. Descendants cannot
 * escape its ownership by setsid(), double-forking, or losing their parent. */
enum shutdown_state {
	QUIESCING,
	TERM_PENDING,
	RESUMING,
	DRAINING,
	QUIESCING_FOR_KILL,
	KILL_PENDING,
	WAITING_FOR_EXIT,
	VERIFYING_EMPTY,
	STOPPING_SERVER,
	WAITING_FOR_SERVER,
	COMPLETE,
};

enum guest_state {
	RUNNING,
	STOP_REQUESTED,
	STOPPED,
	PREVIOUSLY_STOPPED,
	EXITING,
	EXITED,
};

struct process_identity {
	pid_t pid;
	pid_t parent;
	unsigned long long start_time;
	char state;
};

struct guest {
	struct process_identity identity;
	int pidfd;
	enum guest_state state;
};

struct shutdown {
	pid_t server;
	int server_fd;
	struct guest* guests;
	size_t count;
	size_t capacity;
	int64_t deadline;
	enum shutdown_state state;
};

static int64_t now_ms(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return -errno;
	return now.tv_sec * INT64_C(1000) + now.tv_nsec / 1000000;
}

static int read_process(pid_t pid, struct process_identity* process)
{
	char path[64];
	char stat[4096];
	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return -errno;
	size_t length = fread(stat, 1, sizeof(stat) - 1, file);
	int result = ferror(file) ? -(errno ? errno : EIO) : 0;
	fclose(file);
	if (result != 0)
		return result;
	stat[length] = '\0';
	char* fields = strrchr(stat, ')');
	if (fields == NULL || sscanf(fields + 1,
			" %c %d %*s %*s"
			" %*s %*s %*s %*s %*s"
			" %*s %*s %*s %*s %*s"
			" %*s %*s %*s %*s %*s %llu",
			&process->state, &process->parent, &process->start_time) != 3)
		return -EIO;
	process->pid = pid;
	return 0;
}

static int process_exited(int pidfd)
{
	struct pollfd fd = { .fd = pidfd, .events = POLLIN };
	int result;
	do {
		result = poll(&fd, 1, 0);
	} while (result < 0 && errno == EINTR);
	if (result < 0)
		return -errno;
	if (result == 0)
		return 0;
	return fd.revents & (POLLIN | POLLHUP) ? 1 : -EIO;
}

static int owner_alive(const struct shutdown* shutdown)
{
	int result = process_exited(shutdown->server_fd);
	return result > 0 ? -EPIPE : result;
}

static int signal_guest(struct guest* guest, int signal_number)
{
	if (guest->state == EXITED)
		return 0;
	if (syscall(SYS_pidfd_send_signal, guest->pidfd, signal_number, NULL, 0) == 0)
		return 0;
	int error = errno;
	if (error == ESRCH && process_exited(guest->pidfd) == 1) {
		guest->state = EXITED;
		return 0;
	}
	return -error;
}

/* Return one when the ownership view changed, including a child disappearing
 * during discovery. That requires another pass over the subreaper's children. */
static int pin_child(struct shutdown* shutdown, pid_t pid, pid_t parent, int parent_fd)
{
	for (size_t i = 0; i < shutdown->count; ++i) {
		if (shutdown->guests[i].identity.pid == pid &&
				process_exited(shutdown->guests[i].pidfd) == 0)
			return 0;
	}
	int fd = syscall(SYS_pidfd_open, pid, 0);
	if (fd < 0)
		return errno == ESRCH ? 1 : -errno;
	struct process_identity identity;
	int result = read_process(pid, &identity);
	if (result == -ENOENT || result == -ESRCH) {
		close(fd);
		return 1;
	}
	if (result != 0) {
		close(fd);
		return result;
	}
	int exited = process_exited(fd);
	int parent_exited = process_exited(parent_fd);
	if (exited < 0 || parent_exited < 0) {
		close(fd);
		return exited < 0 ? exited : parent_exited;
	}
	/* A live pinned parent proves the relationship, not just a reused PID
	 * number in a stale /proc snapshot. Ownership then remains monotonic. */
	if (identity.parent != parent || parent_exited) {
		close(fd);
		return 1;
	}
	if (exited) {
		for (size_t i = 0; i < shutdown->count; ++i) {
			if (shutdown->guests[i].identity.pid == pid &&
					shutdown->guests[i].identity.start_time == identity.start_time) {
				close(fd);
				return 0;
			}
		}
	}
	if (pid == getpid()) {
		close(fd);
		return -EDEADLK;
	}
	if (shutdown->count == shutdown->capacity) {
		if (shutdown->capacity > SIZE_MAX / 2 / sizeof(*shutdown->guests)) {
			close(fd);
			return -ENOMEM;
		}
		size_t capacity = shutdown->capacity ? shutdown->capacity * 2 : 16;
		void* resized = realloc(shutdown->guests, capacity * sizeof(*shutdown->guests));
		if (resized == NULL) {
			close(fd);
			return -ENOMEM;
		}
		shutdown->guests = resized;
		shutdown->capacity = capacity;
	}
	shutdown->guests[shutdown->count++] = (struct guest) {
		.identity = identity,
		.pidfd = fd,
		.state = exited ? EXITED : RUNNING,
	};
	return 1;
}

static int scan_children(struct shutdown* shutdown, pid_t parent, int parent_fd)
{
	char path[96];
	snprintf(path, sizeof(path), "/proc/%d/task", parent);
	DIR* tasks = opendir(path);
	if (tasks == NULL)
		return errno == ENOENT || errno == ESRCH ? 1 : -errno;
	int changes = 0;
	struct dirent* entry;
	while (errno = 0, (entry = readdir(tasks)) != NULL) {
		char* end;
		long tid = strtol(entry->d_name, &end, 10);
		if (*end != '\0' || tid <= 0 || tid > INT_MAX)
			continue;
		snprintf(path, sizeof(path), "/proc/%d/task/%ld/children", parent, tid);
		FILE* children = fopen(path, "r");
		if (children == NULL) {
			if (errno == ENOENT || errno == ESRCH) {
				++changes;
				continue;
			}
			changes = -errno;
			break;
		}
		long pid;
		int parsed;
		while ((parsed = fscanf(children, "%ld", &pid)) == 1) {
			int result = pid > 0 && pid <= INT_MAX
				? pin_child(shutdown, (pid_t)pid, parent, parent_fd) : -EIO;
			if (result < 0) {
				changes = result;
				break;
			}
			changes += result;
		}
		if (changes >= 0 && (parsed == 0 || ferror(children)))
			changes = -(errno ? errno : EIO);
		fclose(children);
		if (changes < 0)
			break;
	}
	if (entry == NULL && errno != 0)
		changes = -errno;
	closedir(tasks);
	return changes;
}

static int threads_stopped(pid_t pid)
{
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/task", pid);
	DIR* tasks = opendir(path);
	if (tasks == NULL)
		return errno == ENOENT || errno == ESRCH ? 0 : -errno;
	int stopped = 0;
	struct dirent* entry;
	while (errno = 0, (entry = readdir(tasks)) != NULL) {
		char* end;
		long tid = strtol(entry->d_name, &end, 10);
		if (*end != '\0' || tid <= 0 || tid > INT_MAX)
			continue;
		struct process_identity thread;
		int result = read_process((pid_t)tid, &thread);
		if (result == -ENOENT || result == -ESRCH) {
			stopped = 0;
			break;
		}
		if (result != 0) {
			stopped = result;
			break;
		}
		if (thread.state != 'T' && thread.state != 't' &&
				thread.state != 'Z' && thread.state != 'X') {
			stopped = 0;
			break;
		}
		stopped = 1;
	}
	if (entry == NULL && errno != 0)
		stopped = -errno;
	closedir(tasks);
	return stopped;
}

static int quiesce(struct shutdown* shutdown)
{
	int64_t started = now_ms();
	if (started < 0)
		return (int)started;
	for (;;) {
		int result = owner_alive(shutdown);
		if (result != 0)
			return result;
		int changes = scan_children(shutdown, shutdown->server, shutdown->server_fd);
		if (changes < 0)
			return changes;
		size_t pending = 0;
		for (size_t i = 0; i < shutdown->count; ++i) {
			struct guest* guest = &shutdown->guests[i];
			result = process_exited(guest->pidfd);
			if (result < 0)
				return result;
			if (result) {
				if (guest->state != EXITED)
					++changes;
				guest->state = EXITED;
				continue;
			}
			result = threads_stopped(guest->identity.pid);
			if (result < 0)
				return result;
			if (result) {
				if (guest->state == RUNNING)
					guest->state = PREVIOUSLY_STOPPED;
				else if (guest->state == STOP_REQUESTED)
					guest->state = STOPPED;
				/* Scanning may grow the array; retain no guest pointer across it. */
				result = scan_children(shutdown, guest->identity.pid, guest->pidfd);
				if (result < 0)
					return result;
				changes += result;
			} else {
				if (guest->state != STOP_REQUESTED) {
					result = signal_guest(guest, SIGSTOP);
					if (result != 0)
						return result;
					if (guest->state != EXITED)
						guest->state = STOP_REQUESTED;
				}
				++pending;
			}
		}
		if (pending == 0 && changes == 0)
			return owner_alive(shutdown);
		int64_t now = now_ms();
		if (now < 0)
			return (int)now;
		if (now - started >= 1000 || now >= shutdown->deadline)
			return -ETIMEDOUT;
		usleep(10000);
	}
}

static int wait_pidfd(int pidfd, int64_t deadline)
{
	struct pollfd fd = { .fd = pidfd, .events = POLLIN };
	for (;;) {
		int64_t now = now_ms();
		if (now < 0)
			return (int)now;
		int result = poll(&fd, 1, now < deadline ? (int)(deadline - now) : 0);
		if (result < 0 && errno == EINTR)
			continue;
		if (result < 0)
			return -errno;
		if (result == 0)
			return -ETIMEDOUT;
		return fd.revents & (POLLIN | POLLHUP) ? 0 : -EIO;
	}
}

static int wait_guests(struct shutdown* shutdown)
{
	int64_t now = now_ms();
	if (now < 0)
		return (int)now;
	int64_t deadline = now + 1000;
	if (deadline > shutdown->deadline)
		deadline = shutdown->deadline;
	for (size_t i = 0; i < shutdown->count; ++i) {
		int result = wait_pidfd(shutdown->guests[i].pidfd, deadline);
		if (result != 0)
			return result;
		shutdown->guests[i].state = EXITED;
	}
	return owner_alive(shutdown);
}

static int resume_guests(struct shutdown* shutdown)
{
	for (size_t i = 0; i < shutdown->count; ++i) {
		struct guest* guest = &shutdown->guests[i];
		if (guest->state != STOPPED && guest->state != STOP_REQUESTED)
			continue;
		int result = signal_guest(guest, SIGCONT);
		if (result != 0)
			return result;
		if (guest->state != EXITED)
			guest->state = RUNNING;
	}
	return 0;
}

int shutdown_rootless_process_tree(pid_t server)
{
	if (server <= 1 || server == getpid())
		return -EINVAL;
	struct shutdown shutdown = { .server = server, .server_fd = -1, .state = QUIESCING };
	int64_t started = now_ms();
	if (started < 0)
		return (int)started;
	shutdown.deadline = started + 5000;
	shutdown.server_fd = syscall(SYS_pidfd_open, server, 0);
	if (shutdown.server_fd < 0)
		return -errno;
	int result = 0;
	while (shutdown.state != COMPLETE) {
		result = shutdown.state == WAITING_FOR_SERVER ? 0 : owner_alive(&shutdown);
		if (result != 0)
			break;
		switch (shutdown.state) {
		case QUIESCING:
		case QUIESCING_FOR_KILL:
			result = quiesce(&shutdown);
			if (result == 0)
				shutdown.state = shutdown.state == QUIESCING ? TERM_PENDING : KILL_PENDING;
			break;
		case TERM_PENDING:
		case KILL_PENDING:
			for (size_t i = 0; i < shutdown.count; ++i) {
				result = signal_guest(&shutdown.guests[i],
					shutdown.state == TERM_PENDING ? SIGTERM : SIGKILL);
				if (result != 0)
					break;
				if (shutdown.state == KILL_PENDING && shutdown.guests[i].state != EXITED)
					shutdown.guests[i].state = EXITING;
			}
			if (result == 0)
				shutdown.state = shutdown.state == TERM_PENDING ? RESUMING : WAITING_FOR_EXIT;
			break;
		case RESUMING:
			result = resume_guests(&shutdown);
			if (result == 0)
				shutdown.state = DRAINING;
			break;
		case DRAINING:
			result = wait_guests(&shutdown);
			if (result == 0 || result == -ETIMEDOUT) {
				/* Resumed TERM handlers may fork. Re-freeze the complete kernel
				 * ownership closure before any final kill, not the old snapshot. */
				result = 0;
				shutdown.state = QUIESCING_FOR_KILL;
			}
			break;
		case WAITING_FOR_EXIT:
			result = wait_guests(&shutdown);
			if (result == 0)
				shutdown.state = VERIFYING_EMPTY;
			break;
		case VERIFYING_EMPTY:
			result = scan_children(&shutdown, server, shutdown.server_fd);
			if (result >= 0) {
				shutdown.state = result == 0 ? STOPPING_SERVER : QUIESCING_FOR_KILL;
				result = 0;
			}
			break;
		case STOPPING_SERVER:
			if (syscall(SYS_pidfd_send_signal, shutdown.server_fd, SIGKILL, NULL, 0) != 0)
				result = -errno;
			else
				shutdown.state = WAITING_FOR_SERVER;
			break;
		case WAITING_FOR_SERVER:
			result = wait_pidfd(shutdown.server_fd, shutdown.deadline);
			if (result == 0)
				shutdown.state = COMPLETE;
			break;
		case COMPLETE:
			break;
		}
		if (result != 0)
			break;
		int64_t now = now_ms();
		if (now < 0 || (shutdown.state != COMPLETE && now >= shutdown.deadline)) {
			result = now < 0 ? (int)now : -ETIMEDOUT;
			break;
		}
	}
	/* Failure is not successful shutdown: release only stops acquired by us,
	 * and leave the live RPC owner and its runtime state for the caller. */
	if (result != 0) {
		int resume_result = resume_guests(&shutdown);
		if (resume_result != 0)
			fprintf(stderr, "Cannot release rootless shutdown stops: %s\n", strerror(-resume_result));
	}
	for (size_t i = 0; i < shutdown.count; ++i)
		close(shutdown.guests[i].pidfd);
	free(shutdown.guests);
	close(shutdown.server_fd);
	return result;
}
