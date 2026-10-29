#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define REQUIRE(condition, label) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL kqueue socket EOF: %s errno=%d (%s)\n", label, errno, strerror(errno)); \
        goto cleanup; \
    } \
} while (0)

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
    int datagram[2] = {-1, -1}, stream[2] = {-1, -1};
    struct kevent event;
    const struct timespec ready = {1, 0}, zero = {0, 0};
    void *datagram_token = (void *)(uintptr_t)1;
    void *stream_token = (void *)(uintptr_t)2;
    char bytes[4];

    queue = kqueue();
    REQUIRE(queue >= 0, "create queue");
    REQUIRE(socketpair(AF_UNIX, SOCK_DGRAM, 0, datagram) == 0, "create datagram pair");
    REQUIRE(register_read(queue, datagram[0], datagram_token), "register datagram reader");
    REQUIRE(send(datagram[1], "", 0, 0) == 0, "send zero-length datagram");
    REQUIRE(kevent(queue, NULL, 0, &event, 1, &ready) == 1, "deliver empty datagram readiness");
    REQUIRE(event.ident == (uintptr_t)datagram[0] && event.filter == EVFILT_READ &&
            event.udata == datagram_token && !(event.flags & EV_ERROR), "identify datagram readiness");
    fprintf(stderr, "KQUEUE_SOCKET_EOF datagram flags=%u bytes=%lld\n",
            event.flags, (long long)event.data);
    REQUIRE(event.data == 0 && !(event.flags & EV_EOF), "zero-length datagram is data, not EOF");
    REQUIRE(recv(datagram[0], bytes, sizeof(bytes), 0) == 0, "receive zero-length datagram");
    REQUIRE(send(datagram[1], "x", 1, 0) == 1, "send subsequent datagram");
    REQUIRE(kevent(queue, NULL, 0, &event, 1, &ready) == 1 && !(event.flags & EV_EOF),
            "datagram peer remains readable and open");
    REQUIRE(recv(datagram[0], bytes, sizeof(bytes), 0) == 1 && bytes[0] == 'x',
            "receive subsequent datagram");
    REQUIRE(kevent(queue, NULL, 0, &event, 1, &zero) == 0, "drained datagram has no readiness");

    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, stream) == 0, "create stream pair");
    REQUIRE(register_read(queue, stream[0], stream_token), "register stream reader");
    REQUIRE(kevent(queue, NULL, 0, &event, 1, &zero) == 0, "empty live stream has no readiness");
    REQUIRE(send(stream[1], "abc", 3, 0) == 3, "send stream payload");
    REQUIRE(shutdown(stream[1], SHUT_WR) == 0, "half-close stream writer");
    REQUIRE(kevent(queue, NULL, 0, &event, 1, &ready) == 1, "deliver half-close with buffered data");
    REQUIRE(event.ident == (uintptr_t)stream[0] && event.filter == EVFILT_READ &&
            event.udata == stream_token && !(event.flags & EV_ERROR), "identify stream readiness");
    fprintf(stderr, "KQUEUE_SOCKET_EOF stream flags=%u bytes=%lld\n",
            event.flags, (long long)event.data);
    REQUIRE((event.flags & EV_EOF) && event.data == 3, "half-close reports EOF before buffered data is drained");
    REQUIRE(recv(stream[0], bytes, sizeof(bytes), 0) == 3 && memcmp(bytes, "abc", 3) == 0,
            "preserve buffered stream payload");
    REQUIRE(recv(stream[0], bytes, sizeof(bytes), 0) == 0, "observe stream EOF after payload");
    REQUIRE(send(stream[0], "z", 1, 0) == 1 && recv(stream[1], bytes, sizeof(bytes), 0) == 1 && bytes[0] == 'z',
            "half-close preserves reverse stream direction");
    result = 0;

cleanup:
    for (unsigned index = 0; index < 2; ++index) {
        if (datagram[index] >= 0 && close(datagram[index]) < 0) result = 1;
        if (stream[index] >= 0 && close(stream[index]) < 0) result = 1;
    }
    if (queue >= 0 && close(queue) < 0) result = 1;
    if (result == 0) puts("KQUEUE_SOCKET_EOF_GUEST_OK");
    return result;
}
