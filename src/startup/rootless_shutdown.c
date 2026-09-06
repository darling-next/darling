#include "rootless_shutdown.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct process {
	pid_t pid;
	pid_t parent;
	pid_t session;
	unsigned long long start_time;
	bool owned;
	int pidfd;
};

static int read_process(pid_t pid, struct process* process)
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
	char state;
	if (fields == NULL || sscanf(fields + 1,
			" %c %d %*d %d"
			" %*s %*s %*s %*s %*s"
			" %*s %*s %*s %*s %*s"
			" %*s %*s %*s %*s %*s %llu",
			&state, &process->parent, &process->session,
			&process->start_time) != 4)
		return -EIO;
	process->pid = pid;
	process->owned = false;
	process->pidfd = -1;
	return 0;
}

static int collect_processes(struct process** processes, size_t* count)
{
	DIR* proc = opendir("/proc");
	if (proc == NULL)
		return -errno;
	size_t capacity = 0;
	int result = 0;
	struct dirent* entry;
	while (errno = 0, (entry = readdir(proc)) != NULL) {
		char* end;
		long pid = strtol(entry->d_name, &end, 10);
		if (*end != '\0' || pid <= 0 || pid > INT_MAX)
			continue;
		struct process process;
		result = read_process((pid_t)pid, &process);
		if (result == -ENOENT || result == -ESRCH) {
			result = 0;
			continue;
		}
		if (result != 0)
			break;
		if (*count == capacity) {
			if (capacity > SIZE_MAX / 2 / sizeof(**processes)) {
				result = -ENOMEM;
				break;
			}
			capacity = capacity ? capacity * 2 : 64;
			void* resized = realloc(*processes, capacity * sizeof(**processes));
			if (resized == NULL) {
				result = -ENOMEM;
				break;
			}
			*processes = resized;
		}
		(*processes)[(*count)++] = process;
	}
	if (entry == NULL && errno != 0)
		result = -errno;
	closedir(proc);
	return result;
}

static int signal_processes(struct process* processes, size_t count, int signal_number)
{
	for (size_t i = 0; i < count; ++i) {
		if (processes[i].pidfd < 0)
			continue;
		if (syscall(SYS_pidfd_send_signal, processes[i].pidfd,
				signal_number, NULL, 0) < 0 && errno != ESRCH)
			return -errno;
	}
	return 0;
}

static int wait_processes(struct process* processes, size_t count, int timeout_ms)
{
	struct timespec deadline;
	if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
		return -errno;
	deadline.tv_sec += timeout_ms / 1000;
	deadline.tv_nsec += (timeout_ms % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		++deadline.tv_sec;
		deadline.tv_nsec -= 1000000000L;
	}
	for (size_t i = 0; i < count; ++i) {
		if (processes[i].pidfd < 0)
			continue;
		struct pollfd fd = { .fd = processes[i].pidfd, .events = POLLIN };
		for (;;) {
			struct timespec now;
			if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
				return -errno;
			long long remaining = (deadline.tv_sec - now.tv_sec) * 1000LL
				+ (deadline.tv_nsec - now.tv_nsec + 999999L) / 1000000L;
			int result = poll(&fd, 1, remaining > 0 ? (int)remaining : 0);
			if (result < 0 && errno == EINTR)
				continue;
			if (result < 0)
				return -errno;
			if (result == 0)
				return -ETIMEDOUT;
			if ((fd.revents & (POLLIN | POLLHUP)) == 0)
				return -EIO;
			break;
		}
	}
	return 0;
}

int shutdown_rootless_process_tree(pid_t leader)
{
	struct process root;
	int result = read_process(leader, &root);
	if (result != 0)
		return result;
	/* launchd owns a distinct session; never seed the caller's host session. */
	if (root.session != leader || root.session == getsid(0))
		return -EINVAL;

	struct process* processes = NULL;
	size_t count = 0;
	result = collect_processes(&processes, &count);
	if (result != 0)
		goto out;

	for (size_t i = 0; i < count; ++i)
		processes[i].owned = processes[i].session == root.session;
	/* shellspawn creates another session. Capture its descendants before any
	 * parent is terminated and they are reparented outside launchd's tree. */
	bool changed;
	do {
		changed = false;
		for (size_t i = 0; i < count; ++i) {
			if (processes[i].owned)
				continue;
			for (size_t j = 0; j < count; ++j) {
				if (processes[j].owned && processes[i].parent == processes[j].pid) {
					processes[i].owned = true;
					changed = true;
					break;
				}
			}
		}
	} while (changed);

	for (size_t i = 0; i < count; ++i) {
		if (!processes[i].owned)
			continue;
		if (processes[i].pid == getpid()) {
			result = -EDEADLK;
			goto out;
		}
		int fd = syscall(SYS_pidfd_open, processes[i].pid, 0);
		if (fd < 0) {
			if (errno == ESRCH)
				continue;
			result = -errno;
			goto out;
		}
		struct process current;
		result = read_process(processes[i].pid, &current);
		if (result == -ENOENT || result == -ESRCH ||
				(result == 0 && current.start_time != processes[i].start_time)) {
			close(fd);
			result = 0;
			continue;
		}
		if (result != 0) {
			close(fd);
			goto out;
		}
		processes[i].pidfd = fd;
	}

	result = signal_processes(processes, count, SIGTERM);
	if (result == 0)
		result = wait_processes(processes, count, 1000);
	if (result == -ETIMEDOUT) {
		result = signal_processes(processes, count, SIGKILL);
		if (result == 0)
			result = wait_processes(processes, count, 1000);
	}
	/* Only a verified terminal guest set permits the caller to stop the RPC
	 * server. An error leaves both the server and its runtime state intact. */
out:
	for (size_t i = 0; i < count; ++i) {
		if (processes[i].pidfd >= 0)
			close(processes[i].pidfd);
	}
	free(processes);
	return result;
}
