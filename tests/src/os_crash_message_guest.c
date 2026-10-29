#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef void (*crash_message_fn)(const char *, char *);
typedef void (*crash_callback_fn)(const char *);

static int callback_fd;

static void crash_callback(const char *message)
{
	size_t size = strlen(message) + 1;
	if (write(callback_fd, message, size) != (ssize_t)size)
		_exit(71);
}

static ssize_t read_message(int fd, char *buffer, size_t capacity)
{
	size_t used = 0;
	while (used + 1 < capacity) {
		ssize_t size = read(fd, buffer + used, capacity - used - 1);
		if (size < 0 && errno == EINTR)
			continue;
		if (size < 0)
			return -1;
		if (size == 0) {
			buffer[used] = '\0';
			return (ssize_t)used;
		}
		used += (size_t)size;
	}
	return -1;
}

static int check_crash(crash_message_fn crash, crash_callback_fn *callback,
		const char *format, char *message)
{
	int notification[2], diagnostic[2];
	if (pipe(notification) || pipe(diagnostic))
		return 1;
	pid_t child = fork();
	if (child < 0)
		return 1;
	if (child == 0) {
		struct rlimit core_limit = {0, 0};
		if (setrlimit(RLIMIT_CORE, &core_limit) ||
				dup2(diagnostic[1], STDOUT_FILENO) < 0 ||
				dup2(diagnostic[1], STDERR_FILENO) < 0)
			_exit(72);
		close(notification[0]);
		close(diagnostic[0]);
		close(diagnostic[1]);
		callback_fd = notification[1];
		*callback = crash_callback;
		crash(format, message);
		_exit(73);
	}
	close(notification[1]);
	close(diagnostic[1]);
	char observed_format[256], observed_message[2048];
	ssize_t format_size = read_message(notification[0], observed_format, sizeof(observed_format));
	ssize_t message_size = read_message(diagnostic[0], observed_message, sizeof(observed_message));
	close(notification[0]);
	close(diagnostic[0]);
	int status = 0;
	pid_t waited;
	do {
		waited = waitpid(child, &status, 0);
	} while (waited < 0 && errno == EINTR);
	if (waited != child || !WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT) {
		fprintf(stderr, "FAIL crash message: child must terminate with SIGABRT (status=%d)\n", status);
		return 1;
	}
	if (format_size < 0 || (format &&
			(format_size != (ssize_t)strlen(format) + 1 || strcmp(format, observed_format))) ||
			(!format && format_size != 0)) {
		fprintf(stderr, "FAIL crash message: callback receives only the non-null format\n");
		return 1;
	}
	if (message_size < 0 || (message && !strstr(observed_message, message))) {
		fprintf(stderr, "FAIL crash message: composed reason must survive fatal reporting\n");
		return 1;
	}
	return 0;
}

int main(void)
{
	crash_message_fn crash = (crash_message_fn)dlsym(RTLD_DEFAULT, "_os_crash_msg");
	crash_callback_fn *callback = (crash_callback_fn *)dlsym(RTLD_DEFAULT, "_os_crash_callback");
	if (!crash || !callback) {
		fprintf(stderr, "FAIL crash message: public crash API and callback must be available\n");
		return 1;
	}
	char message[] = "composed fatal reason: 42, not the format";
	if (check_crash(crash, callback, "format remains literal: %s/%d", message) ||
			check_crash(crash, callback, NULL, message) ||
			check_crash(crash, callback, "format without composed message", NULL))
		return 1;
	puts("OS_CRASH_MESSAGE_GUEST_OK");
	return 0;
}
