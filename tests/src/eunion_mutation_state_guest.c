/* Real Darwin syscall regression. Immutable lower inputs are installed by
 * tests-data; the workload also checks their bytes/modes/xattrs after exit. */
#define __APPLE_API_PRIVATE 1
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/xattr.h>
#include <unistd.h>

#define ROOT "/usr/share/darling/tests/eunion/routing"
#define PAYLOAD "ROUTING_LOWER\n"
static int failures;
static const char* prefix;

static void check(const char* name, int ok) {
    printf("%s %s (errno=%d)\n", ok ? "ok" : "FAIL", name, errno);
    if (!ok) failures++;
}
static int bytes(const char* path, const char* expected) {
    char data[128] = {0};
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, data, sizeof(data));
    close(fd);
    return n == (ssize_t)strlen(expected) && !memcmp(data, expected, n);
}
static void upper(char* out, const char* leaf) {
    snprintf(out, 4096, "/Volumes/SystemRoot%s%s/%s", prefix, ROOT, leaf);
}
static int mode(const char* path, mode_t expected) {
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & 07777) == expected;
}
static int absent(const char* path) {
    struct stat st;
    return lstat(path, &st) < 0 && errno == ENOENT;
}
static int link_target(const char* path, const char* target) {
    char data[128];
    ssize_t n = readlink(path, data, sizeof(data));
    return n == (ssize_t)strlen(target) && !memcmp(data, target, n);
}
static int tag(const char* path) {
    char data[32];
    return getxattr(path, "test.routing", data, sizeof(data), 0, 0) == 11 &&
           !memcmp(data, "routing-tag", 11);
}
static void metadata(int rootfd) {
    char path[4096];
    check("fchmodat dirfd relative", fchmodat(rootfd, "chmod-dirfd", 0600, 0) == 0);
    upper(path, "chmod-dirfd");
    check("fchmodat dirfd upper bytes mode xattr", bytes(path, PAYLOAD) && mode(path, 0600) && tag(path));
    check("fchmodat cwd relative", fchmodat(AT_FDCWD, "chmod-cwd", 0600, 0) == 0);
    upper(path, "chmod-cwd");
    check("fchmodat cwd upper bytes mode xattr", bytes(path, PAYLOAD) && mode(path, 0600) && tag(path));
    struct timeval tv[2] = {{123456789, 0}, {123456790, 0}};
    check("utimes cwd relative", utimes("utimes-cwd", tv) == 0);
    upper(path, "utimes-cwd");
    struct stat st;
    check("utimes upper effect and preserved bytes xattr", stat(path, &st) == 0 && st.st_mtime == 123456790 && bytes(path, PAYLOAD) && tag(path));
    /* chmod_extended is not a public SDK libc entry point. Invoke the actual
     * Darwin syscall with unchanged ownership and no extended security blob. */
    check("chmod_extended cwd relative", syscall(SYS_chmod_extended, "extended-cwd", -1, -1, 0600, NULL) == 0);
    upper(path, "extended-cwd");
    check("chmod_extended upper bytes mode xattr", bytes(path, PAYLOAD) && mode(path, 0600) && tag(path));
    check("truncate cwd relative", truncate("truncate-cwd", 3) == 0);
    upper(path, "truncate-cwd");
    check("truncate upper bytes and xattr", bytes(path, "ROU") && tag(path));
    struct attrlist attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.commonattr = ATTR_CMN_MODTIME | ATTR_CMN_ACCTIME | ATTR_CMN_ACCESSMASK;
    struct { struct timespec modified, accessed; unsigned int permissions; } values = {
        {111222333, 123000}, {111222334, 456000}, 0600
    };
    check("setattrlist relative metadata", setattrlist("alist-path", &attributes, &values, sizeof(values), 0) == 0);
    upper(path, "alist-path");
    check("setattrlist upper bytes mode timestamp xattr", bytes(path, PAYLOAD) && mode(path, 0600) &&
          stat(path, &st) == 0 && st.st_mtime == 111222333 && tag(path));
    int metadata_fd = open("alist-fd", O_RDONLY);
    check("fsetattrlist lower descriptor metadata", metadata_fd >= 0 &&
          fsetattrlist(metadata_fd, &attributes, &values, sizeof(values), 0) == 0);
    if (metadata_fd >= 0) close(metadata_fd);
    upper(path, "alist-fd");
    check("fsetattrlist upper bytes mode timestamp xattr", bytes(path, PAYLOAD) && mode(path, 0600) &&
          stat(path, &st) == 0 && st.st_mtime == 111222333 && tag(path));
    attributes.commonattr = ATTR_CMN_ACCESSMASK;
    errno = 0;
    check("setattrlist short buffer rejects before prepare",
          setattrlist("alist-invalid", &attributes, &values.permissions, 3, 0) == -1 && errno == EINVAL);
    upper(path, "alist-invalid");
    check("setattrlist failure leaves upper absent", absent(path));
}
static int socket_name(int fd, int peer, const char* expected) {
    struct sockaddr_un address;
    socklen_t length = sizeof(address);
    memset(&address, 0, sizeof(address));
    int result = peer ? getpeername(fd, (struct sockaddr*)&address, &length)
                      : getsockname(fd, (struct sockaddr*)&address, &length);
    return result == 0 && address.sun_family == AF_UNIX && !strcmp(address.sun_path, expected);
}

static void creators(int rootfd) {
    char path[4096];
    check("mkfifo relative parent", mkfifo("fifo/new", 0600) == 0);
    upper(path, "fifo/new");
    struct stat st;
    check("mkfifo upper node", lstat(path, &st) == 0 && S_ISFIFO(st.st_mode));
    check("mknod relative parent", mknod("node/new", S_IFIFO | 0600, 0) == 0);
    upper(path, "node/new");
    check("mknod upper node", lstat(path, &st) == 0 && S_ISFIFO(st.st_mode));
    /* A separate cwd makes accidental AT_FDCWD use observable. */
    check("change cwd for symlinkat dirfd", chdir("fifo") == 0);
    check("symlinkat honors explicit dirfd", symlinkat("../../hardlink-src", rootfd, "slink/new") == 0);
    check("restore routing cwd", fchdir(rootfd) == 0);
    upper(path, "slink/new");
    check("symlinkat upper target bytes", link_target(path, "../../hardlink-src"));
    check("relative bind lower parent cwd", chdir("socket") == 0);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, "new");
    address.sun_len = offsetof(struct sockaddr_un, sun_path) + 4;
    check("relative bind creates", fd >= 0 && bind(fd, (struct sockaddr*)&address, address.sun_len) == 0);
    struct sockaddr_un returned;
    socklen_t returned_length = sizeof(returned);
    memset(&returned, 0, sizeof(returned));
    check("relative bind returns stable canonical guest name",
          fd >= 0 && getsockname(fd, (struct sockaddr*)&returned, &returned_length) == 0 &&
          returned.sun_family == AF_UNIX && !strcmp(returned.sun_path, ROOT "/socket/new"));
    int listening = fd >= 0 && listen(fd, 1) == 0;
    int client = listening ? socket(AF_UNIX, SOCK_STREAM, 0) : -1;
    strcpy(address.sun_path, ROOT "/socket/new");
    address.sun_len = offsetof(struct sockaddr_un, sun_path) + strlen(address.sun_path) + 1;
    int connected = client >= 0 && connect(client, (struct sockaddr*)&address, address.sun_len) == 0;
    int accepted = connected ? accept(fd, NULL, NULL) : -1;
    check("bound socket connection setup", connected && accepted >= 0);
    if (fd >= 0) close(fd);
    check("client peer name survives listener close",
          connected && socket_name(client, 1, ROOT "/socket/new"));
    check("accepted local name survives listener close",
          accepted >= 0 && socket_name(accepted, 0, ROOT "/socket/new"));
    if (client >= 0) close(client);
    check("accepted local name survives peer close",
          accepted >= 0 && socket_name(accepted, 0, ROOT "/socket/new"));
    if (accepted >= 0) close(accepted);
    check("restore cwd after bind", fchdir(rootfd) == 0);
    upper(path, "socket/new");
    check("bind upper socket", lstat(path, &st) == 0 && S_ISSOCK(st.st_mode));
}
static void hardlinks(int rootfd) {
    char a[4096], b[4096];
    check("hardlink relative src and dest dirfd", linkat(rootfd, "hardlink-src", rootfd, "hardlink/new", 0) == 0);
    upper(a, "hardlink-src"); upper(b, "hardlink/new");
    struct stat sa, sb;
    check("hardlink shares upper inode", stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino && sa.st_nlink == 2);
    int fd = openat(rootfd, "hardlink/new", O_WRONLY | O_TRUNC);
    check("hardlink upper write", fd >= 0 && write(fd, "LINK_UPPER\n", 11) == 11);
    if (fd >= 0) close(fd);
    check("hardlink upper source observes write", bytes(a, "LINK_UPPER\n"));
    check("hardlink upper source retains xattr", tag(a));
}
static void final_symlinks(int rootfd) {
    struct { const char* stem; int flags; int error; int dangling; } cases[] = {
        {"follow", O_WRONLY | O_TRUNC, 0, 0},
        {"create-follow", O_WRONLY | O_CREAT | O_TRUNC, 0, 0},
        {"exclusive", O_WRONLY | O_CREAT | O_EXCL, EEXIST, 0},
        {"nofollow", O_WRONLY | O_NOFOLLOW, ELOOP, 0},
        {"create-nofollow", O_WRONLY | O_CREAT | O_NOFOLLOW, ELOOP, 0},
        {"dangling-create", O_WRONLY | O_CREAT, 0, 1},
        {"dangling-exclusive", O_WRONLY | O_CREAT | O_EXCL, EEXIST, 1},
        {"dangling-missing", O_WRONLY, ENOENT, 1}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char leaf[128], target[128], path[4096], label[160];
        snprintf(leaf, sizeof(leaf), "%s-link", cases[i].stem);
        snprintf(target, sizeof(target), "%s-target", cases[i].stem);
        errno = 0;
        int fd = openat(rootfd, leaf, cases[i].flags, 0600);
        int error = errno;
        snprintf(label, sizeof(label), "%s native result/errno", cases[i].stem);
        check(label, cases[i].error ? fd == -1 && error == cases[i].error : fd >= 0);
        if (fd >= 0) {
            check("final symlink upper write", write(fd, "FOLLOW_UPPER\n", 13) == 13);
            close(fd);
        }
        upper(path, target);
        check("final target upper effect or failure absence", cases[i].error ? absent(path) : bytes(path, "FOLLOW_UPPER\n"));
        snprintf(path, sizeof(path), ROOT "/%s", leaf);
        check("final symlink remains symlink", link_target(path, target));
    }
    check("metadata follows final symlink", fchmodat(rootfd, "meta-follow-link", 0600, 0) == 0);
    char path[4096]; upper(path, "meta-follow-target");
    check("metadata followed target upper", mode(path, 0600) && bytes(path, PAYLOAD));
    check("metadata nofollow symlink", fchownat(rootfd, "meta-nofollow-link", -1, -1, AT_SYMLINK_NOFOLLOW) == 0);
    upper(path, "meta-nofollow-link");
    check("metadata nofollow upper link", link_target(path, "meta-nofollow-target"));
    upper(path, "meta-nofollow-target");
    check("metadata nofollow target not copied", absent(path));
}
static void failures_preserve(int rootfd) {
    errno = 0;
    check("negative truncate returns EINVAL", truncate("failure", -1) == -1 && errno == EINVAL);
    char path[4096]; upper(path, "failure");
    check("negative truncate leaves upper absent", absent(path));
    check("failure guest bytes unchanged", bytes("failure", PAYLOAD));
    errno = 0;
    check("bad chmod flags EINVAL", fchmodat(rootfd, "failure", 0600, 0x40000000) == -1 && errno == EINVAL);
    errno = 0;
    check("bad dirfd EBADF", fchmodat(-1, "failure", 0600, 0) == -1 && errno == EBADF);
    int regular = open("failure", O_RDONLY);
    errno = 0;
    check("regular dirfd ENOTDIR", regular >= 0 && fchmodat(regular, "../failure", 0600, 0) == -1 && errno == ENOTDIR);
    if (regular >= 0) close(regular);
    errno = 0;
    check("creator missing parent ENOENT", mkfifo("absent/new", 0600) == -1 && errno == ENOENT);
}
int main(void) {
    prefix = getenv("DARLING_PREFIX");
    if (!prefix || prefix[0] != '/') { puts("FAIL DARLING_PREFIX must identify actual upper"); return 1; }
    umask(0);
    int rootfd = open(ROOT, O_RDONLY | O_DIRECTORY);
    if (rootfd < 0 || fchdir(rootfd) != 0) { perror("routing fixture"); return 1; }
    metadata(rootfd);
    creators(rootfd);
    hardlinks(rootfd);
    final_symlinks(rootfd);
    failures_preserve(rootfd);
    close(rootfd);
    if (failures) return 1;
    puts("WEST_EUNION_MUTATION_STATE_OK");
    return 0;
}
