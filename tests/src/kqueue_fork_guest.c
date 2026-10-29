#include <sys/event.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define REQUIRE(condition, label) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL kqueue fork: %s errno=%d (%s)\n", label, errno, strerror(errno)); \
        goto cleanup; \
    } \
} while (0)

enum child_status {
    CHILD_OK = 0,
    CHILD_QUEUE_INHERITED = 20,
    CHILD_QUEUE_CHECK_ERROR = 21,
    CHILD_PIPE_CLOSE_ERROR = 22
};

static const char *child_reason(int status)
{
    switch (status) {
    case CHILD_OK: return "success";
    case CHILD_QUEUE_INHERITED: return "kqueue descriptor remains open after fork";
    case CHILD_QUEUE_CHECK_ERROR: return "F_GETFD failed without EBADF";
    case CHILD_PIPE_CLOSE_ERROR: return "closing unrelated inherited pipe descriptor failed";
    default: return "unexpected child exit status";
    }
}

static int register_read(int queue, int fd, void *token)
{
    struct kevent change, receipt;
    const struct timespec zero = {0, 0};
    EV_SET(&change, fd, EVFILT_READ, EV_ADD | EV_CLEAR | EV_RECEIPT, 0, 0, token);
    int count = kevent(queue, &change, 1, &receipt, 1, &zero);
    return count == 1 && (receipt.flags & EV_ERROR) && receipt.data == 0;
}

int main(void)
{
    int result = 1, queue = -1;
    int monitored[2] = {-1, -1}, unrelated[2] = {-1, -1};
    struct kevent event;
    const struct timespec ready = {1, 0}, zero = {0, 0};
    void *token = (void *)(uintptr_t)1;
    const char payload[] = "parent queue survives child cleanup";
    char bytes[sizeof(payload)];
    int status;
    pid_t child;

    alarm(20);
    queue = kqueue();
    REQUIRE(queue >= 0, "create queue");
    REQUIRE(pipe(monitored) == 0, "create monitored pipe");
    REQUIRE(pipe(unrelated) == 0, "create unrelated pipe");
    REQUIRE(register_read(queue, monitored[0], token), "register pipe reader");
    REQUIRE(kevent(queue, NULL, 0, &event, 1, &zero) == 0, "empty pipe has no readiness");
    REQUIRE(write(monitored[1], payload, sizeof(payload)) == (ssize_t)sizeof(payload),
            "queue parent payload before fork");

    child = fork();
    REQUIRE(child >= 0, "fork child");
    if (child == 0) {
        /* First public child operation: no allocation can reuse the queue fd.
         * ENOENT from kevent would only prove a missing userspace map entry,
         * not Darwin's required closure of the inherited descriptor. */
        int flags = fcntl(queue, F_GETFD);
        int check_errno = errno;
        if (flags != -1) _exit(CHILD_QUEUE_INHERITED);
        if (check_errno != EBADF) _exit(CHILD_QUEUE_CHECK_ERROR);

        /* fork does not inherit the parent's alarm. Bound the close callback
         * independently, without timing a race or polling for completion. */
        alarm(5);
        if (close(unrelated[0]) != 0) _exit(CHILD_PIPE_CLOSE_ERROR);
        _exit(CHILD_OK);
    }

    REQUIRE(waitpid(child, &status, 0) == child, "reap actual child");
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "FAIL kqueue fork: child terminated by signal %d\n", WTERMSIG(status));
    } else if (WIFEXITED(status) && WEXITSTATUS(status) != CHILD_OK) {
        fprintf(stderr, "FAIL kqueue fork: child exit=%d (%s)\n",
                WEXITSTATUS(status), child_reason(WEXITSTATUS(status)));
    }
    REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == CHILD_OK,
            "child verifies non-inheritance and completes unrelated close");

    /* Do not register again: readiness must come from the original queue entry. */
    REQUIRE(fcntl(queue, F_GETFD) >= 0, "parent queue descriptor remains open");
    REQUIRE(kevent(queue, NULL, 0, &event, 1, &ready) == 1,
            "parent original registration delivers readiness after child cleanup");
    REQUIRE(event.ident == (uintptr_t)monitored[0] && event.filter == EVFILT_READ &&
            event.udata == token && !(event.flags & (EV_ERROR | EV_EOF)),
            "identify original parent pipe readiness");
    REQUIRE(event.data == (intptr_t)sizeof(payload), "readiness retains parent payload length");
    REQUIRE(read(monitored[0], bytes, sizeof(bytes)) == (ssize_t)sizeof(payload) &&
            memcmp(bytes, payload, sizeof(payload)) == 0, "preserve parent pipe payload");
    REQUIRE(kevent(queue, NULL, 0, &event, 1, &zero) == 0,
            "drained parent pipe has no readiness");
    result = 0;

cleanup:
    for (unsigned index = 0; index < 2; ++index) {
        if (monitored[index] >= 0 && close(monitored[index]) < 0) result = 1;
        if (unrelated[index] >= 0 && close(unrelated[index]) < 0) result = 1;
    }
    if (queue >= 0 && close(queue) < 0) result = 1;
    alarm(0);
    if (result == 0) puts("KQUEUE_FORK_GUEST_OK");
    return result;
}
