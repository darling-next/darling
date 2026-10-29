#include <mach/mach.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char directory[] = "/private/var/tmp/mach-port-stdout-XXXXXX";
    if (!mkdtemp(directory)) {
        perror("mkdtemp");
        return 1;
    }
    int channel[2];
    if (pipe(channel) != 0) {
        perror("pipe");
        rmdir(directory);
        return 2;
    }
    mach_port_t thread = mach_thread_self();
    int original = dup(STDOUT_FILENO);
    if (original < 0 || dup2(channel[1], STDOUT_FILENO) < 0) {
        perror("capture stdout");
        if (original >= 0) close(original);
        close(channel[0]);
        close(channel[1]);
        rmdir(directory);
        return 3;
    }
    close(channel[1]);
    kern_return_t status = mach_port_mod_refs(mach_task_self(), thread,
                                              MACH_PORT_RIGHT_SEND, -1);
    size_t length = strlen(directory);
    int written = write(STDOUT_FILENO, directory, length) == (ssize_t)length
        && write(STDOUT_FILENO, "\n", 1) == 1;
    if (dup2(original, STDOUT_FILENO) < 0) {
        close(original);
        close(channel[0]);
        rmdir(directory);
        return 4;
    }
    close(original);
    char captured[4096];
    size_t used = 0;
    ssize_t count;
    while (used < sizeof(captured) - 1) {
        count = read(channel[0], captured + used, sizeof(captured) - 1 - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += (size_t)count;
    }
    close(channel[0]);
    captured[used] = '\0';
    if (used && captured[used - 1] == '\n') captured[--used] = '\0';
    int observed = open(captured, O_RDONLY | O_DIRECTORY);
    struct stat expected, actual;
    int usable = observed >= 0 && stat(directory, &expected) == 0
        && fstat(observed, &actual) == 0
        && expected.st_dev == actual.st_dev && expected.st_ino == actual.st_ino;
    if (observed >= 0) close(observed);
    int removed = rmdir(directory) == 0;
    if (status != KERN_SUCCESS || !written || !usable || !removed) {
        fprintf(stderr, "FAIL Mach-port stdout consumer status=%d written=%d usable=%d removed=%d captured=%s\n",
                status, written, usable, removed, captured);
        return 5;
    }
    puts("MACH_PORT_STDOUT_GUEST_OK");
    return 0;
}
