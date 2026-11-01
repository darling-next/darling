#include <mach/mach.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static mach_port_t ready_port;
static const char *self_path;
struct message { mach_msg_header_t header; mach_msg_max_trailer_t trailer; };

static void replace_image(void) {
    char *const args[] = {(char *)self_path, "after", NULL};
    execv(self_path, args);
    perror("execv");
    _exit(4);
}

static void *worker(void *unused) {
    (void)unused;
    struct message message;
    memset(&message, 0, sizeof(message));
    kern_return_t result = mach_msg(&message.header, MACH_RCV_MSG, 0,
                                    sizeof(message), ready_port,
                                    MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (result != KERN_SUCCESS || message.header.msgh_id != 204) {
        fprintf(stderr, "EXEC_READY_ERROR result=%d id=%d\n", result,
                message.header.msgh_id);
        _exit(3);
    }
    replace_image();
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    self_path = argv[0];
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (strcmp(argv[1], "after") == 0) {
        puts("EXEC_AFTER_PASS=1");
        return 0;
    }
    if (strcmp(argv[1], "main") == 0) {
        puts("EXEC_MAIN_CONTROL_BEGIN=1");
        replace_image();
    }
    if (strcmp(argv[1], "worker") != 0) return 2;
    mach_port_t reply_port;
    kern_return_t result = mach_port_allocate(mach_task_self(),
                                              MACH_PORT_RIGHT_RECEIVE,
                                              &ready_port);
    if (result != KERN_SUCCESS) return 3;
    result = mach_port_insert_right(mach_task_self(), ready_port, ready_port,
                                     MACH_MSG_TYPE_MAKE_SEND);
    if (result != KERN_SUCCESS) return 3;
    result = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
                                &reply_port);
    if (result != KERN_SUCCESS) return 3;
    pthread_t thread;
    int error = pthread_create(&thread, NULL, worker, NULL);
    if (error) {
        fprintf(stderr, "EXEC_THREAD_ERROR=%d\n", error);
        return 3;
    }
    struct message message;
    memset(&message, 0, sizeof(message));
    message.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    message.header.msgh_size = sizeof(message.header);
    message.header.msgh_remote_port = ready_port;
    message.header.msgh_id = 204;
    puts("EXEC_MAIN_SEND_AND_PARK=1");
    result = mach_msg(&message.header, MACH_SEND_MSG | MACH_RCV_MSG,
                      sizeof(message.header), sizeof(message), reply_port,
                      MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    fprintf(stderr, "EXEC_MAIN_UNEXPECTED_RETURN=%d\n", result);
    return 5;
}
