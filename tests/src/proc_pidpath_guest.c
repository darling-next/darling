#include <errno.h>
#include <limits.h>
#include <libproc.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int failures;

static void check(int condition, const char *operation)
{
    if (!condition) {
        fprintf(stderr, "FAIL proc_pidpath: %s (errno=%d)\n", operation, errno);
        ++failures;
    }
}

static int identifies(const char *path, const char *expected)
{
    struct stat actual, wanted;
    return stat(path, &actual) == 0 && stat(expected, &wanted) == 0 &&
        actual.st_dev == wanted.st_dev && actual.st_ino == wanted.st_ino;
}

int main(int argc, char **argv)
{
    (void)argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(20);
    signal(SIGPIPE, SIG_IGN);

    char own_path[PROC_PIDPATHINFO_MAXSIZE] = {0};
    int count = proc_pidpath(getpid(), own_path, sizeof(own_path));
    check(count > 0 && memchr(own_path, '\0', sizeof(own_path)) != NULL &&
        identifies(own_path, argv[0]), "self path identifies the actual executable");

    int input[2], output[2];
    if (pipe(input) != 0) {
        perror("proc_pidpath input pipe");
        return 1;
    }
    if (pipe(output) != 0) {
        perror("proc_pidpath output pipe");
        close(input[0]);
        close(input[1]);
        return 1;
    }
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error == 0) {
        error = posix_spawn_file_actions_adddup2(&actions, input[0], STDIN_FILENO);
        if (error == 0)
            error = posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
        const int descriptors[] = {input[0], input[1], output[0], output[1]};
        for (unsigned i = 0; error == 0 && i < sizeof(descriptors) / sizeof(descriptors[0]); ++i)
            error = posix_spawn_file_actions_addclose(&actions, descriptors[i]);
        pid_t child = -1;
        char *child_argv[] = {"/bin/bash", "-c", "printf R; IFS= read -r token; exit 0", NULL};
        if (error == 0)
            error = posix_spawn(&child, child_argv[0], &actions, NULL, child_argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        close(input[0]);
        close(output[1]);
        check(error == 0, "spawn a distinct executable");
        if (error == 0) {
            char ready = 0;
            ssize_t received;
            do {
                received = read(output[0], &ready, 1);
            } while (received < 0 && errno == EINTR);
            check(received == 1 && ready == 'R', "child has executed and remains alive");
            if (received == 1 && ready == 'R') {
                char child_path[PROC_PIDPATHINFO_MAXSIZE] = {0};
                count = proc_pidpath(child, child_path, sizeof(child_path));
                check(count > 0 && memchr(child_path, '\0', sizeof(child_path)) != NULL &&
                    identifies(child_path, "/bin/bash") && strcmp(child_path, own_path) != 0,
                    "target path identifies the child, not the caller");
            }
            check(write(input[1], "done\n", 5) == 5, "release the owned child");
            close(input[1]);
            close(output[0]);
            int status = 0;
            pid_t waited;
            do {
                waited = waitpid(child, &status, 0);
            } while (waited < 0 && errno == EINTR);
            check(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "child exits normally and is reaped");
        } else {
            close(input[1]);
            close(output[0]);
        }
    } else {
        check(0, "initialize spawn actions");
        close(input[0]);
        close(input[1]);
        close(output[0]);
        close(output[1]);
    }

    char missing_path[PROC_PIDPATHINFO_MAXSIZE] = {0};
    errno = 0;
    count = proc_pidpath(INT_MAX, missing_path, sizeof(missing_path));
    check(count == 0 && errno == ESRCH, "missing process reports ESRCH without terminating the caller");
    printf("PROC_PIDPATH failures=%d\n", failures);
    if (failures != 0)
        return 1;
    puts("PROC_PIDPATH_GUEST_OK");
    return 0;
}
