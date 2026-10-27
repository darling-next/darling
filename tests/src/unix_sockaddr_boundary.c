/* Guest CLT fixture, identical source for old/new installed-runtime proof.
 * DARLING_PREFIX must be the actual launcher-published absolute prefix. The
 * parent must supply a prefix short enough for the 104-byte host boundary. */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static const char directory[] = "/private/tmp/unix-boundary";
static size_t prefix_length;

static struct sockaddr_un address(size_t host_length, char suffix)
{
    struct sockaddr_un result;
    memset(&result, 0, sizeof(result));
    result.sun_family = AF_UNIX;
    assert(host_length > prefix_length + sizeof(directory));
    size_t length = host_length - prefix_length;
    assert(length < sizeof(result.sun_path));
    memcpy(result.sun_path, directory, strlen(directory));
    result.sun_path[strlen(directory)] = '/';
    memset(result.sun_path + sizeof(directory), 'x', length - sizeof(directory));
    result.sun_path[length - 1] = suffix;
    result.sun_len = offsetof(struct sockaddr_un, sun_path) + length + 1;
    return result;
}

static void check_address(const struct sockaddr_un* actual, socklen_t length,
                          const struct sockaddr_un* expected)
{
    assert(length == expected->sun_len);
    assert(actual->sun_len == length);
    assert(actual->sun_family == AF_UNIX);
    assert(strcmp(actual->sun_path, expected->sun_path) == 0);
}

static void boundary(size_t host_length)
{
    struct sockaddr_un server = address(host_length, 'a');
    struct sockaddr_un client = address(host_length, 'b');
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    int sender = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(listener >= 0 && sender >= 0);
    assert(bind(listener, (struct sockaddr*)&server, server.sun_len) == 0);
    assert(bind(sender, (struct sockaddr*)&client, client.sun_len) == 0);
    assert(listen(listener, 1) == 0);
    assert(connect(sender, (struct sockaddr*)&server, server.sun_len) == 0);
    struct sockaddr_un returned;
    socklen_t length = sizeof(returned);
    int receiver = accept(listener, (struct sockaddr*)&returned, &length);
    assert(receiver >= 0);
    check_address(&returned, length, &client);
    length = sizeof(returned);
    assert(getpeername(sender, (struct sockaddr*)&returned, &length) == 0);
    check_address(&returned, length, &server);
    length = sizeof(returned);
    assert(getsockname(sender, (struct sockaddr*)&returned, &length) == 0);
    check_address(&returned, length, &client);
    for (socklen_t capacity = 0; capacity <= sizeof(returned); ++capacity) {
        unsigned char guarded[sizeof(returned) + 16];
        memset(guarded, 0xa5, sizeof(guarded));
        length = capacity;
        assert(getpeername(sender, (struct sockaddr*)(guarded + 8), &length) == 0);
        assert(length == server.sun_len);
        for (size_t i = 0; i < 8; ++i) assert(guarded[i] == 0xa5);
        for (size_t i = 8 + capacity; i < sizeof(guarded); ++i)
            assert(guarded[i] == 0xa5);
        assert(memcmp(guarded + 8, &server, capacity < length ? capacity : length) == 0);
    }
    char byte;
    assert(write(sender, "s", 1) == 1);
    assert(read(receiver, &byte, 1) == 1 && byte == 's');
    assert(close(receiver) == 0 && close(sender) == 0 && close(listener) == 0);
    assert(unlink(server.sun_path) == 0 && unlink(client.sun_path) == 0);

    listener = socket(AF_UNIX, SOCK_DGRAM, 0);
    sender = socket(AF_UNIX, SOCK_DGRAM, 0);
    assert(listener >= 0 && sender >= 0);
    assert(bind(listener, (struct sockaddr*)&server, server.sun_len) == 0);
    assert(bind(sender, (struct sockaddr*)&client, client.sun_len) == 0);
    assert(connect(sender, (struct sockaddr*)&server, server.sun_len) == 0);
    for (socklen_t capacity = 0; capacity <= sizeof(returned); ++capacity) {
        unsigned char guarded[sizeof(returned) + 16];
        memset(guarded, 0xa5, sizeof(guarded));
        assert(write(sender, "d", 1) == 1);
        length = capacity;
        assert(recvfrom(listener, &byte, 1, 0, (struct sockaddr*)(guarded + 8), &length) == 1);
        assert(byte == 'd' && length == client.sun_len);
        for (size_t i = 0; i < 8; ++i) assert(guarded[i] == 0xa5);
        for (size_t i = 8 + capacity; i < sizeof(guarded); ++i)
            assert(guarded[i] == 0xa5);
        assert(memcmp(guarded + 8, &client, capacity < length ? capacity : length) == 0);
    }
    assert(close(sender) == 0 && close(listener) == 0);
    assert(unlink(server.sun_path) == 0 && unlink(client.sun_path) == 0);
    printf("host-expanded unix path %zu: bind/connect/roundtrip/guards OK\n", host_length);
}

int main(void)
{
    const char* prefix = getenv("DARLING_PREFIX");
    assert(prefix && prefix[0] == '/');
    prefix_length = strlen(prefix);
    assert(prefix_length && prefix[prefix_length - 1] != '/');
    char host_root[4096];
    int root_length = snprintf(host_root, sizeof(host_root), "/Volumes/SystemRoot%s", prefix);
    assert(root_length >= 0 && (size_t)root_length < sizeof(host_root));
    struct stat guest, host;
    assert(stat("/", &guest) == 0 && stat(host_root, &host) == 0);
    assert(guest.st_dev == host.st_dev && guest.st_ino == host.st_ino);
    assert(mkdir(directory, 0700) == 0);
    boundary(104);
    boundary(107);
    struct sockaddr_un over = address(108, 'a');
    struct sockaddr_un maximum = address(107, 'a');
    /* The overlimit name extends the valid 107-byte name: rejection must not
     * silently bind/connect the truncated valid identity. */
    memcpy(over.sun_path, maximum.sun_path, strlen(maximum.sun_path));
    over.sun_path[strlen(maximum.sun_path)] = 'z';
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    int client = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(server >= 0 && client >= 0);
    assert(bind(server, (struct sockaddr*)&maximum, maximum.sun_len) == 0);
    assert(listen(server, 1) == 0);
    errno = 0;
    assert(bind(client, (struct sockaddr*)&over, over.sun_len) == -1 && errno == ENAMETOOLONG);
    errno = 0;
    assert(connect(client, (struct sockaddr*)&over, over.sun_len) == -1 && errno == ENAMETOOLONG);
    assert(lstat(over.sun_path, &guest) == -1 && errno == ENOENT);
    assert(close(client) == 0 && close(server) == 0);
    assert(unlink(maximum.sun_path) == 0);
    assert(rmdir(directory) == 0);
    puts("DARLING_UNIX_SOCKADDR_BOUNDARY_OK");
    return 0;
}
