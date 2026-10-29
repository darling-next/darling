#include <errno.h>
#include <malloc/malloc.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define REQUIRE(condition, label) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL spawn PID cache: %s errno=%d\n", label, errno); \
		exit(1); \
	} \
} while (0)

static int observation[2];
static pid_t preparing_pid;
struct child_identity {
	pid_t preparing_pid;
	pid_t child_pid;
};

/* An empty zone observes real libSystem fork callbacks, including the child
 * setup before posix_spawn exec. It neither owns nor serves allocations. */
static size_t empty_size(malloc_zone_t *zone, const void *address)
{
	(void)zone;
	(void)address;
	return 0;
}

static void prepare(malloc_zone_t *zone)
{
	(void)zone;
	preparing_pid = getpid();
}

static void parent(malloc_zone_t *zone)
{
	(void)zone;
}

static void child(malloc_zone_t *zone)
{
	(void)zone;
	int saved_errno = errno;
	struct child_identity identity = { preparing_pid, getpid() };
	if (write(observation[1], &identity, sizeof(identity)) != sizeof(identity))
		_exit(99);
	errno = saved_errno;
}

static malloc_introspection_t introspection = {
	.force_lock = prepare,
	.force_unlock = parent,
	.reinit_lock = child,
};
static malloc_zone_t zone = {
	.size = empty_size,
	.zone_name = "spawn PID observer",
	.introspect = &introspection,
	.version = 9,
};

static void check_child(pid_t expected)
{
	struct child_identity identity;
	REQUIRE(read(observation[0], &identity, sizeof(identity)) == sizeof(identity),
		"child callback observation");
	REQUIRE(identity.preparing_pid == getpid(), "prepare sees the parent PID");
	REQUIRE(identity.child_pid != identity.preparing_pid,
		"child callback must not inherit the parent's cached PID");
	if (expected > 0)
		REQUIRE(identity.child_pid == expected, "child callback sees the actual spawned PID");
}

static void *spawn_worker(void *unused)
{
	(void)unused;
	pid_t pid = -1;
	char *missing[] = { "/nonexistent/darling-spawn-pid-cache", NULL };
	REQUIRE(posix_spawn(&pid, missing[0], NULL, NULL, missing, environ) == ENOENT,
		"missing executable preserves ENOENT");
	check_child(-1);
	char *valid[] = { "/usr/bin/true", NULL };
	REQUIRE(posix_spawn(&pid, valid[0], NULL, NULL, valid, environ) == 0,
		"spawn from a non-main thread");
	check_child(pid);
	int status;
	REQUIRE(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
		"spawned process completes successfully");
	return NULL;
}

int main(void)
{
	alarm(20);
	REQUIRE(pipe(observation) == 0, "observation pipe");
	REQUIRE(getpid() > 0, "warm parent PID cache");
	malloc_zone_register(&zone);
	pthread_t worker;
	REQUIRE(pthread_create(&worker, NULL, spawn_worker, NULL) == 0, "create spawn worker");
	REQUIRE(pthread_join(worker, NULL) == 0, "join spawn worker");
	pid_t pid = fork();
	REQUIRE(pid >= 0, "ordinary fork");
	if (pid == 0)
		_exit(0);
	check_child(pid);
	int status;
	REQUIRE(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
		"ordinary fork completes successfully");
	malloc_zone_unregister(&zone);
	REQUIRE(close(observation[0]) == 0 && close(observation[1]) == 0, "close observation pipe");
	alarm(0);
	puts("POSIX_SPAWN_PID_CACHE_GUEST_OK");
	return 0;
}
