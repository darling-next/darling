/*
 * Mach-port kqchan progress while main is blocked in a real Mach receive.
 * Like the proc/console consumers, only the worker's completed I/O releases
 * main. Two new EVFILT_MACHPORT channels must deliver their actual messages
 * into the worker's supplied buffer, and a subsequent mach_port_construct
 * must complete before main resumes. A receive timeout is always failure.
 */
#include <mach/mach.h>
#include <mach/mach_port.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/event.h>
#include <time.h>
#include <unistd.h>

#define MESSAGE_ID 0x504d50
#define RELEASE_ID 0x504d51
#define PAYLOAD_COOKIE 0x6d616368u

static mach_port_t release_port;
static mach_port_t watched_port;
static int worker_ok;

struct payload_message {
    mach_msg_header_t header;
    uint32_t cookie;
    uint32_t round;
};

union receive_buffer {
    struct payload_message message;
    unsigned char bytes[512]; /* Include room for the Mach trailer. */
};

static int mach_ok(kern_return_t result, const char *operation)
{
    if (result == KERN_SUCCESS)
        return 1;
    fprintf(stderr, "FAIL plane Mach-port open: %s result=%d\n", operation, (int)result);
    return 0;
}

static int consume_round(unsigned round)
{
    int ok = 0;
    int kq = kqueue();
    if (kq < 0) {
        perror("plane Mach-port open: kqueue");
        return 0;
    }

    union receive_buffer received;
    memset(&received, 0, sizeof(received));
    struct kevent64_s change;
    memset(&change, 0, sizeof(change));
    change.ident = watched_port;
    change.filter = EVFILT_MACHPORT;
    change.flags = EV_ADD | EV_ENABLE;
    change.fflags = MACH_RCV_MSG;
    change.ext[0] = (uint64_t)(uintptr_t)&received;
    change.ext[1] = sizeof(received);
    if (kevent64(kq, &change, 1, NULL, 0, 0, NULL) < 0) {
        perror("plane Mach-port open: register");
        goto out;
    }

    /* The channel open must also relinquish the process's sole mailbox. */
    mach_port_options_t options;
    memset(&options, 0, sizeof(options));
    mach_port_t constructed = MACH_PORT_NULL;
    if (!mach_ok(mach_port_construct(mach_task_self(), &options, 0, &constructed),
                 "construct after channel open"))
        goto out;
    mach_port_type_t type = 0;
    int valid = mach_ok(mach_port_type(mach_task_self(), constructed, &type),
                        "constructed port type") && (type & MACH_PORT_TYPE_RECEIVE) != 0;
    int destroyed = mach_ok(mach_port_destroy(mach_task_self(), constructed),
                            "destroy constructed port");
    if (!valid || !destroyed) {
        fprintf(stderr, "FAIL plane Mach-port open: constructed receive right type=0x%x\n", type);
        goto out;
    }

    struct payload_message sent;
    memset(&sent, 0, sizeof(sent));
    sent.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
    sent.header.msgh_size = sizeof(sent);
    sent.header.msgh_remote_port = watched_port;
    sent.header.msgh_id = MESSAGE_ID;
    sent.cookie = PAYLOAD_COOKIE;
    sent.round = round;
    if (!mach_ok(mach_msg(&sent.header, MACH_SEND_MSG, sizeof(sent), 0,
                          MACH_PORT_NULL, 0, MACH_PORT_NULL), "send watched message"))
        goto out;

    struct kevent64_s event;
    memset(&event, 0, sizeof(event));
    struct timespec timeout = { 5, 0 };
    int count = kevent64(kq, NULL, 0, &event, 1, 0, &timeout);
    if (count != 1 || event.ident != watched_port || event.filter != EVFILT_MACHPORT ||
        (event.flags & (EV_ERROR | EV_EOF)) != 0 || event.fflags != MACH_MSG_SUCCESS ||
        event.ext[0] != (uint64_t)(uintptr_t)&received ||
        event.ext[1] < sizeof(sent) || event.ext[1] > sizeof(received) ||
        received.message.header.msgh_size != sizeof(sent) ||
        received.message.header.msgh_id != MESSAGE_ID ||
        received.message.cookie != PAYLOAD_COOKIE || received.message.round != round) {
        fprintf(stderr, "FAIL plane Mach-port open: channel round=%u count=%d flags=0x%x fflags=0x%x\n",
                round, count, event.flags, event.fflags);
        goto out;
    }

    union receive_buffer empty;
    kern_return_t empty_result = mach_msg(&empty.message.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                                          0, sizeof(empty), watched_port, 0, MACH_PORT_NULL);
    if (empty_result != MACH_RCV_TIMED_OUT) {
        fprintf(stderr, "FAIL plane Mach-port open: message not consumed result=%d\n", (int)empty_result);
        goto out;
    }
    printf("PLANE_MACH_PORT_OPEN round=%u channel-io=1 construct=1 before-main-release=1\n", round);
    ok = 1;
out:
    if (close(kq) != 0) {
        perror("plane Mach-port open: close kqueue");
        ok = 0;
    }
    return ok;
}

static void *worker(void *argument)
{
    (void)argument;
    /* Match the existing consumers' delay before main's blocked-call admission. */
    struct timespec delay = { 1, 0 };
    if (nanosleep(&delay, NULL) != 0) {
        perror("plane Mach-port open: worker delay");
        return NULL;
    }
    worker_ok = consume_round(1) && consume_round(2);
    mach_msg_header_t release;
    memset(&release, 0, sizeof(release));
    release.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
    release.msgh_size = sizeof(release);
    release.msgh_remote_port = release_port;
    release.msgh_id = RELEASE_ID;
    if (!mach_ok(mach_msg(&release, MACH_SEND_MSG, sizeof(release), 0,
                          MACH_PORT_NULL, 0, MACH_PORT_NULL), "release main"))
        worker_ok = 0;
    return NULL;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("PLANE_MACH_PORT_OPEN begin pid=%d\n", (int)getpid());
    if (!mach_ok(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &release_port),
                 "allocate release port"))
        return 2;
    if (!mach_ok(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &watched_port),
                 "allocate watched port")) {
        mach_port_destroy(mach_task_self(), release_port);
        return 2;
    }
    pthread_t thread;
    int error = pthread_create(&thread, NULL, worker, NULL);
    if (error != 0) {
        fprintf(stderr, "FAIL plane Mach-port open: pthread_create=%d\n", error);
        mach_port_destroy(mach_task_self(), watched_port);
        mach_port_destroy(mach_task_self(), release_port);
        return 2;
    }

    union receive_buffer release;
    kern_return_t result = mach_msg(&release.message.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                                    0, sizeof(release), release_port, 12000, MACH_PORT_NULL);
    if (result != KERN_SUCCESS || release.message.header.msgh_id != RELEASE_ID) {
        fprintf(stderr, "FAIL plane Mach-port open: main resumed without worker completion result=%d\n",
                (int)result);
        /* Do not join or free buffers underneath a stranded worker. */
        return 1;
    }
    int ok = pthread_join(thread, NULL) == 0;
    if (ok)
        ok = worker_ok;
    ok = mach_ok(mach_port_destroy(mach_task_self(), watched_port), "destroy watched port") && ok;
    ok = mach_ok(mach_port_destroy(mach_task_self(), release_port), "destroy release port") && ok;
    if (!ok)
        return 1;
    puts("PLANE_MACH_PORT_OPEN_GUEST_OK");
    return 0;
}
