/* Compile and execute inside the selected Darling installation. This exercises
 * TASK_AUDIT_TOKEN, real Mach audit trailers and the pthread/fork/exec lifecycle.
 * A PID version is a bounded 32-bit value, not code-signature attestation.
 */
#include <mach/mach.h>
#include <mach/task_info.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

static int failures;
static void check(int good, const char *name) {
    printf("%s %s\n", good ? "OK" : "FAIL", name);
    if (!good) ++failures;
}
static void fatal(const char *what) { perror(what); exit(2); }
static audit_token_t token(void) {
    audit_token_t t;
    mach_msg_type_number_t n = TASK_AUDIT_TOKEN_COUNT;
    kern_return_t kr = task_info(mach_task_self(), TASK_AUDIT_TOKEN,
                                (task_info_t)&t, &n);
    if (kr != KERN_SUCCESS || n != TASK_AUDIT_TOKEN_COUNT) {
        fprintf(stderr, "task_info failed: %d count=%u\n", kr, n);
        exit(2);
    }
    return t;
}
static void show(const char *name, audit_token_t t) {
    printf("TOKEN %s pid=%u version=%u\n", name, t.val[5], t.val[7]);
}
static void transfer(int fd, void *data, size_t size, int writing) {
    char *p = data;
    while (size) {
        ssize_t n = writing ? write(fd, p, size) : read(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { fprintf(stderr, "pipe transfer failed\n"); exit(2); }
        p += n;
        size -= (size_t)n;
    }
}
static void *thread_token(void *arg) { *(audit_token_t *)arg = token(); return NULL; }
static void trailer_check(audit_token_t expected) {
    mach_port_t port;
    kern_return_t kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "port allocate: %d\n", kr); exit(2); }
    mach_msg_header_t send;
    memset(&send, 0, sizeof(send));
    send.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
    send.msgh_size = sizeof(send);
    send.msgh_remote_port = port;
    send.msgh_id = 457;
    kr = mach_msg(&send, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(send), 0,
                  MACH_PORT_NULL, 5000, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "mach send: %d\n", kr); exit(2); }
    union { mach_msg_header_t align; unsigned char bytes[4096]; } receive;
    memset(&receive, 0, sizeof(receive));
    kr = mach_msg(&receive.align, MACH_RCV_MSG | MACH_RCV_TIMEOUT |
                  MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) |
                  MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),
                  0, sizeof(receive), port, 5000, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "mach receive: %d\n", kr); exit(2); }
    size_t offset = (receive.align.msgh_size + 3u) & ~(size_t)3u;
    if (offset > sizeof(receive) - sizeof(mach_msg_audit_trailer_t)) {
        fprintf(stderr, "invalid message size\n"); exit(2);
    }
    mach_msg_audit_trailer_t *trailer = (void *)(receive.bytes + offset);
    check(trailer->msgh_trailer_type == MACH_MSG_TRAILER_FORMAT_0 &&
          trailer->msgh_trailer_size >= sizeof(*trailer), "audit trailer present");
    check(memcmp(&trailer->msgh_audit, &expected, sizeof(expected)) == 0,
          "Mach audit trailer equals TASK_AUDIT_TOKEN");
    mach_port_destroy(mach_task_self(), port);
}
int main(int argc, char **argv) {
    alarm(45);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 3 && strcmp(argv[1], "--after-exec") == 0) {
        int fd = atoi(argv[2]);
        audit_token_t t = token();
        trailer_check(t);
        transfer(fd, &t, sizeof(t), 1);
        close(fd);
        return failures ? 1 : 0;
    }
    audit_token_t initial = token(), worker, after_failed;
    show("parent", initial);
    check(initial.val[5] == (uint32_t)getpid(), "parent token PID");
    pthread_t thread;
    if (pthread_create(&thread, NULL, thread_token, &worker) != 0) exit(2);
    if (pthread_join(thread, NULL) != 0) exit(2);
    check(memcmp(&worker, &initial, sizeof(initial)) == 0, "thread token stable");
    trailer_check(initial);
    char *bad[] = { (char *)"missing", NULL };
    errno = 0;
    execv("/dev/null/audit-pidversion-no-such-executable", bad);
    check(errno == ENOTDIR || errno == ENOENT, "failed exec really failed");
    after_failed = token();
    check(memcmp(&after_failed, &initial, sizeof(initial)) == 0,
          "failed exec preserves token");
    char executable[4096];
    uint32_t capacity = sizeof(executable);
    if (_NSGetExecutablePath(executable, &capacity) != 0) exit(2);
    int fds[2];
    if (pipe(fds) != 0) fatal("pipe");
    pid_t child = fork();
    if (child < 0) fatal("fork");
    if (child == 0) {
        close(fds[0]);
        audit_token_t before = token();
        transfer(fds[1], &before, sizeof(before), 1);
        char fdarg[32];
        snprintf(fdarg, sizeof(fdarg), "%d", fds[1]);
        char *args[] = { executable, (char *)"--after-exec", fdarg, NULL };
        execv(executable, args);
        _exit(2);
    }
    close(fds[1]);
    audit_token_t forked, executed;
    transfer(fds[0], &forked, sizeof(forked), 0);
    transfer(fds[0], &executed, sizeof(executed), 0);
    close(fds[0]);
    int status;
    while (waitpid(child, &status, 0) < 0) { if (errno != EINTR) fatal("waitpid"); }
    show("fork", forked);
    show("exec", executed);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "exec child completed");
    check(forked.val[5] == (uint32_t)child, "fork token PID");
    check(forked.val[7] != initial.val[7], "fork gets new version");
    check(executed.val[5] == forked.val[5], "exec preserves PID");
    check(executed.val[7] != forked.val[7], "successful exec gets new version");
    audit_token_t final = token();
    check(memcmp(&final, &initial, sizeof(initial)) == 0, "parent unchanged by child lifecycle");
    printf("%s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
