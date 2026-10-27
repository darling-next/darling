#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/xattr.h>
#include <unistd.h>

#define ROOT "/usr/share/darling/tests/eunion/state"
static int failures;
static void check(const char* label, int ok) {
    if (!ok) { fprintf(stderr, "FAIL %s (errno=%d: %s)\n", label, errno, strerror(errno)); failures++; }
}
static int visible(const char* path) { struct stat st; return lstat(path, &st) == 0; }
static int create(const char* path, const char* text) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    int ok = fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text);
    if (fd >= 0 && close(fd) != 0) ok = 0;
    return ok;
}
static void remove_case(const char* path, int expected) {
    errno = 0;
    int result = rmdir(path), error = errno;
    check(path, expected ? result == -1 && error == expected && visible(path) : result == 0 && !visible(path));
    if (!expected) {
        errno = 0; result = rmdir(path); error = errno;
        check("removed directory stays absent after repeated removal", result == -1 && error == ENOENT && !visible(path));
    }
}
static void removal_checks(void) {
    remove_case(ROOT "/rmdir-file", ENOTDIR);
    remove_case(ROOT "/nonempty", ENOTEMPTY);
    check("failed nonempty rmdir preserves child", visible(ROOT "/nonempty/child"));
    errno = 0;
    int result = unlink(ROOT "/nonempty"), error = errno;
    check("unlink directory rejects without hiding", result == -1 && (error == EPERM || error == EISDIR) && visible(ROOT "/nonempty/child"));
    check("remove lower empty seed", unlink(ROOT "/empty/seed") == 0);
    remove_case(ROOT "/empty", 0);
    check("create upper empty directory", mkdir(ROOT "/upper-empty", 0755) == 0);
    remove_case(ROOT "/upper-empty", 0);
    int parent = open(ROOT, O_RDONLY);
    errno = 0;
    result = parent >= 0 ? unlinkat(parent, "rmdir-file", AT_REMOVEDIR) : -2; error = errno;
    check("relative unlinkat directory intent rejects regular file", parent >= 0 && result == -1 && error == ENOTDIR && visible(ROOT "/rmdir-file"));
    errno = 0;
    result = parent >= 0 ? unlinkat(parent, "nonempty", AT_REMOVEDIR) : -2; error = errno;
    check("relative unlinkat nonempty failure preserves lower child", parent >= 0 && result == -1 && error == ENOTEMPTY && visible(ROOT "/nonempty/child"));
    check("create relative unlinkat removal fixture", mkdir(ROOT "/upper-unlinkat", 0755) == 0);
    result = parent >= 0 ? unlinkat(parent, "upper-unlinkat", AT_REMOVEDIR) : -2;
    check("relative unlinkat removes legitimate empty directory", result == 0 && !visible(ROOT "/upper-unlinkat"));
    if (parent >= 0) close(parent);
    check("create dataless-flag removal fixture", mkdir(ROOT "/upper-dataless", 0755) == 0);
    /* Existing Darwin AT_REMOVEDIR_DATALESS conversion is a removal alias. */
    check("unlinkat dataless removal retains native directory semantics",
        syscall(SYS_unlinkat, AT_FDCWD, ROOT "/upper-dataless", 0x100) == 0 && !visible(ROOT "/upper-dataless"));
    check("materialize merged directory", create(ROOT "/merged/upper", "UPPER\n"));
    check("empty merged directory via visible unlink", unlink(ROOT "/merged/seed") == 0 && unlink(ROOT "/merged/upper") == 0);
    remove_case(ROOT "/merged", 0);
    check("empty opaque recreation source", unlink(ROOT "/opaque/seed") == 0);
    check("remove opaque recreation source", rmdir(ROOT "/opaque") == 0);
    check("recreate opaque directory", mkdir(ROOT "/opaque", 0755) == 0);
    check("opaque recreation hides old lower child", !visible(ROOT "/opaque/seed"));
    remove_case(ROOT "/opaque", 0);
    errno = 0;
    result = rename(ROOT "/rename-source", ROOT "/rename-directory"); error = errno;
    check("file over directory rename rejects EISDIR", result == -1 && error == EISDIR);
    check("failed rename preserves both visible operands", visible(ROOT "/rename-source") && visible(ROOT "/rename-directory/child"));
    check("whiteout failed-create fixture", unlink(ROOT "/create-abort") == 0 && !visible(ROOT "/create-abort"));
    errno = 0;
    result = symlink("", ROOT "/create-abort"); error = errno;
    check("failed create reports empty-target ENOENT", result == -1 && error == ENOENT);
    check("failed create cannot resurrect lower name", !visible(ROOT "/create-abort"));
}

/* Use the actual BSD syscall, not DIR buffering: distinct DIR objects have
 * independent user buffers, but their duplicated fds must share the OFD cursor. */
static int next_name(int fd, char* out) {
    char buffer[40]; long base = 0;
    ssize_t size = syscall(SYS_getdirentries64, fd, buffer, sizeof(buffer), &base);
    if (size <= 0) return (int)size;
    struct dirent* entry = (void*)buffer;
    if (entry->d_reclen != size || entry->d_namlen >= 256) return -2;
    memcpy(out, entry->d_name, entry->d_namlen);
    out[entry->d_namlen] = '\0';
    return 1;
}
static int note(const char* name, unsigned* seen) {
    if (!strcmp(name, ".") || !strcmp(name, "..")) return 1;
    unsigned index; char extra;
    if (sscanf(name, "n%u%c", &index, &extra) != 1 || index >= 12 || (*seen & (1u << index))) return 0;
    *seen |= 1u << index;
    return 1;
}
static int drain(int fd, unsigned* seen) {
    char name[256]; int result, guard = 0;
    while ((result = next_name(fd, name)) > 0) {
        if (!note(name, seen) || ++guard > 32) return 0;
    }
    return result == 0;
}
static void libc_directory_checks(void) {
    struct stat before = {0}, opened = {0};
    int valid = stat(ROOT "/stream", &before) == 0;
    DIR* directory = opendir(ROOT "/stream");
    valid = valid && directory && fstat(dirfd(directory), &opened) == 0 &&
        before.st_dev == opened.st_dev && before.st_ino == opened.st_ino;
    unsigned seen = 0;
    int entries = 0;
    struct dirent* entry;
    errno = 0;
    while (directory && (entry = readdir(directory))) {
        if (!note(entry->d_name, &seen) || ++entries > 32) {
            valid = 0;
            break;
        }
    }
    int scan_error = errno;
    if (directory && closedir(directory) != 0) valid = 0;
    check("libc opendir retains lower directory identity and enumerates every member once",
        valid && !scan_error && seen == 0xfffu);
}
static void descriptor_checks(void) {
    char name[256], other[256];
    int fd = open(ROOT "/regular-fd", O_RDONLY);
    errno = 0;
    int result = fd >= 0 ? next_name(fd, name) : -2;
    check("regular fd enumeration is ENOTDIR", fd >= 0 && result == -1 && errno == ENOTDIR);
    if (fd >= 0) close(fd);
    check("create independent dup2 cache target", mkdir(ROOT "/dup-target", 0755) == 0);
    for (int mode = 0; mode < 6; mode++) {
        int kind = mode % 3, before_materialization = mode >= 3;
        fd = open(ROOT "/stream", O_RDONLY);
        unsigned seen = 0;
        int first = !before_materialization && fd >= 0 ? next_name(fd, name) : -2;
        int valid = first == 1 && note(name, &seen);
        int alias = -1;
        if (kind == 0 && fd >= 0) alias = dup(fd);
        if (kind == 1 && fd >= 0) {
            alias = open(ROOT "/dup-target", O_RDONLY);
            if (alias >= 0) { char buffer[512]; long base = 0; syscall(SYS_getdirentries64, alias, buffer, sizeof(buffer), &base); }
            if (alias >= 0 && dup2(fd, alias) < 0) { close(alias); alias = -1; }
        }
        if (kind == 2 && fd >= 0) alias = fcntl(fd, F_DUPFD, 0);
        if (before_materialization) {
            first = fd >= 0 ? next_name(fd, name) : -2;
            valid = first == 1 && note(name, &seen);
        }
        int second = alias >= 0 ? next_name(alias, other) : -2;
        valid &= second == 1 && note(other, &seen);
        valid &= fd >= 0 && drain(fd, &seen) && seen == 0xfffu;
        check(kind == 0 ? "dup shares cursor" : kind == 1 ? "dup2 shares cursor and replaces old cache" : "F_DUPFD shares cursor", valid);
        check("all aliases observe exhausted directory", alias >= 0 && next_name(alias, other) == 0);
        check("rewind through alias succeeds", alias >= 0 && lseek(alias, 0, SEEK_SET) == 0);
        result = fd >= 0 ? next_name(fd, other) : -2;
        check("alias rewind updates original cursor", first == 1 && result == 1 && !strcmp(name, other));
        off_t cookie = fd >= 0 ? lseek(fd, 0, SEEK_CUR) : -1;
        char replay[256];
        int read_next = alias >= 0 ? next_name(alias, replay) : -2;
        int sought = alias >= 0 && cookie >= 0 && lseek(alias, cookie, SEEK_SET) == cookie;
        result = fd >= 0 ? next_name(fd, other) : -2;
        check("alias seek cookie updates original cursor", sought && read_next == 1 && result == 1 && !strcmp(replay, other));
        if (alias >= 0) close(alias);
        seen = 0;
        check("closing one alias leaves survivor rewindable", fd >= 0 && lseek(fd, 0, SEEK_SET) == 0 && drain(fd, &seen) && seen == 0xfffu);
        if (fd >= 0) close(fd);
    }
    fd = open(ROOT "/stream", O_RDONLY);
    int original = fd;
    int partial = fd >= 0 && next_name(fd, name) == 1;
    if (fd >= 0) close(fd);
    fd = open(ROOT "/stream", O_RDONLY);
    unsigned seen = 0;
    check("closed fd number reuse starts fresh snapshot", partial && fd == original && drain(fd, &seen) && seen == 0xfffu);
    if (fd >= 0) close(fd);
    fd = open(ROOT "/identity", O_RDONLY);
    int moved = rename(ROOT "/identity", ROOT "/moved") == 0;
    int replacement = mkdir(ROOT "/identity", 0755) == 0 && create(ROOT "/identity/replacement", "NEW\n");
    int old = 0, unexpected = 0;
    while (fd >= 0 && (result = next_name(fd, name)) > 0) {
        if (!strcmp(name, "original")) old++;
        else if (strcmp(name, ".") && strcmp(name, "..")) unexpected++;
    }
    check("opened directory identity survives rename and replacement before enumeration", moved && replacement && fd >= 0 && result == 0 && old == 1 && unexpected == 0);
    if (fd >= 0) close(fd);
}
static int drain_promoted_aliases(int fd, int alias, unsigned* seen, int expect_upper) {
    char name[256];
    int result = -1, upper = 0;
    for (int page = 0; page < 32; page++) {
        result = next_name(page & 1 ? fd : alias, name);
        if (result <= 0) break;
        if (!strcmp(name, "fresh-upper")) {
            if (!expect_upper || upper++) return 0;
        } else if (!note(name, seen)) return 0;
    }
    return result == 0 && *seen == 0xfffu && upper == expect_upper;
}
static void directory_promotion_checks(void) {
    int fd = open(ROOT "/stream", O_RDONLY);
    int alias = fd >= 0 ? dup(fd) : -1;
    check("directory promotion opens actual lower aliases", fd >= 0 && alias >= 0);
    if (fd < 0 || alias < 0) {
        if (fd >= 0) close(fd);
        if (alias >= 0) close(alias);
        return;
    }
    char name[256];
    unsigned seen = 0;
    int partial = next_name(fd, name) == 1 && note(name, &seen);
    off_t cookie = lseek(fd, 0, SEEK_CUR);
    check("directory promotion begins at a shared partial cookie",
        partial && cookie > 0 && lseek(alias, 0, SEEK_CUR) == cookie);
    struct stat original = {0}, duplicate = {0}, named = {0};
    check("directory metadata promotion retains live upper aliases",
        fchmod(fd, 0750) == 0 && fstat(fd, &original) == 0 &&
        fstat(alias, &duplicate) == 0 && stat(ROOT "/stream", &named) == 0 &&
        (original.st_mode & 0777) == 0750 && (duplicate.st_mode & 0777) == 0750 &&
        original.st_dev == duplicate.st_dev && original.st_ino == duplicate.st_ino &&
        original.st_dev == named.st_dev && original.st_ino == named.st_ino);
    check("directory metadata promotion preserves the shared stream cookie",
        lseek(fd, 0, SEEK_CUR) == cookie && lseek(alias, 0, SEEK_CUR) == cookie);
    int flags = fcntl(fd, F_GETFD), alias_flags = fcntl(alias, F_GETFD);
    check("self dup2 preserves flags and the existing shared directory cursor",
        flags >= 0 && alias_flags >= 0 &&
        fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0 && dup2(fd, fd) == fd &&
        fcntl(fd, F_GETFD) == (flags | FD_CLOEXEC) &&
        fcntl(alias, F_GETFD) == alias_flags && lseek(alias, 0, SEEK_CUR) == cookie);
    check("promoted aliases continue without replay or loss",
        partial && drain_promoted_aliases(fd, alias, &seen, 0));
    check("promoted aliases share exhaustion", next_name(fd, name) == 0 && next_name(alias, name) == 0);
    int created = create(ROOT "/stream/fresh-upper", "UPPER\n");
    check("create actual upper child after shared exhaustion", created);
    seen = 0;
    check("shared rewind refreshes the promoted merged directory",
        lseek(alias, 0, SEEK_SET) == 0 && drain_promoted_aliases(fd, alias, &seen, 1));
    check("rewound promoted aliases share exhaustion", next_name(fd, name) == 0 && next_name(alias, name) == 0);
    if (created) check("remove owned upper promotion child", unlink(ROOT "/stream/fresh-upper") == 0);
    check("close promoted directory alias", close(alias) == 0);
    check("close promoted directory original", close(fd) == 0);
}
static void publication_checks(void) {
    unsigned char expected[2048], actual[2048];
    for (size_t i = 0; i < sizeof(expected); i++) expected[i] = (unsigned char)(i % 251);
    ssize_t size = getxattr(ROOT "/large-xattr", "user.test.large", actual, sizeof(actual), 0, 0);
    check("large xattr lower fixture is exact", size == sizeof(expected) && !memcmp(expected, actual, sizeof(expected)));
    int fd = open(ROOT "/large-xattr", O_WRONLY);
    int wrote = fd >= 0 && write(fd, "UPPER\n", 6) == 6;
    int closed = fd >= 0 && close(fd) == 0;
    check("copyup stages 2048-byte xattr and allows successful write", wrote && closed);
    size = getxattr(ROOT "/large-xattr", "user.test.large", actual, sizeof(actual), 0, 0);
    check("successful publication preserves exact 2048-byte xattr", size == sizeof(expected) && !memcmp(expected, actual, sizeof(expected)));
    char data[8] = {0};
    fd = open(ROOT "/large-xattr", O_RDONLY);
    ssize_t count = fd >= 0 ? read(fd, data, sizeof(data)) : -1;
    if (fd >= 0) close(fd);
    check("successful content write survives xattr staging", count == 6 && !memcmp(data, "UPPER\n", 6));
}
static int fd_inventory(int* values, int capacity) {
    DIR* directory = opendir("/Volumes/SystemRoot/proc/self/fd");
    if (!directory) return -1;
    int observer = dirfd(directory), count = 0;
    struct dirent* entry;
    errno = 0;
    while ((entry = readdir(directory))) {
        char* end;
        long value = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end || value < 0 || value > INT_MAX || value == observer) continue;
        if (count == capacity) { closedir(directory); return -1; }
        values[count++] = (int)value;
    }
    int error = errno;
    return closedir(directory) == 0 && !error ? count : -1;
}
static int contains_fd(const int* values, int count, int fd) {
    for (int i = 0; i < count; i++) if (values[i] == fd) return 1;
    return 0;
}
static void descriptor_protection_checks(void) {
    int before[1024], after[1024], pins[1024], pin_count = 0;
    int sentinel = open(ROOT "/regular-fd", O_RDONLY);
    int baseline = fd_inventory(before, 1024);
    int fd = open(ROOT "/stream", O_RDONLY);
    int observed = fd_inventory(after, 1024);
    check("inventory real loader-owned directory pins", sentinel >= 0 && fd >= 0 && baseline >= 0 && observed >= 0);
    if (baseline >= 0 && observed >= 0) {
        for (int i = 0; i < observed; i++)
            if (after[i] != fd && !contains_fd(before, baseline, after[i])) pins[pin_count++] = after[i];
    }
    check("opened directory retains native authority", pin_count > 0);
    for (int i = 0; i < pin_count; i++) {
        errno = 0;
        int result = fcntl(pins[i], F_GETFD);
        check("internal directory pin is not a guest fcntl target", result == -1 && errno == EBADF);
        check("guest close cannot release internal authority", close(pins[i]) == 0);
        errno = 0;
        result = dup2(sentinel, pins[i]);
        check("guest dup2 cannot overwrite internal authority", result == -1 && errno == EBADF);
    }
    char name[256];
    check("directory remains readable after attempted pin close", fd >= 0 && next_name(fd, name) == 1);
    pid_t child = fd >= 0 ? fork() : -1;
    if (child == 0) {
        unsigned seen = 0;
        int ok = lseek(fd, 0, SEEK_SET) == 0 && drain(fd, &seen) && seen == 0xfffu;
        if (close(fd) != 0) ok = 0;
        _exit(ok ? 0 : 1);
    }
    int status = -1;
    check("fork child retains protected opened-directory authority", child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    unsigned seen = 0;
    check("parent authority survives child release", fd >= 0 && lseek(fd, 0, SEEK_SET) == 0 && drain(fd, &seen) && seen == 0xfffu);
    if (fd >= 0) close(fd);
    for (int i = 0; i < pin_count; i++) {
        char bytes[8];
        int copied = sentinel >= 0 && lseek(sentinel, 0, SEEK_SET) == 0 && dup2(sentinel, pins[i]) == pins[i];
        int readable = copied && read(pins[i], bytes, sizeof(bytes)) == sizeof(bytes) && !memcmp(bytes, "REGULAR\n", sizeof(bytes));
        check("last directory close releases reservation for real descriptor reuse", readable);
        if (copied) close(pins[i]);
    }
    if (sentinel >= 0) close(sentinel);
}
static void private_namespace_checks(void) {
    struct stat st;
    errno = 0;
    int result = lstat("/.union-work", &st);
    check("private work authority is not guest-addressable", result == -1 && errno == ENOENT);
    int root = open("/", O_RDONLY);
    errno = 0;
    int probe = root >= 0 ? openat(root, "./.union-work/guest-probe", O_RDWR | O_CREAT | O_EXCL, 0600) : -2;
    check("relative canonical alias cannot create in private work", root >= 0 && probe == -1 && errno == ENOENT);
    if (probe >= 0) { close(probe); unlinkat(root, "./.union-work/guest-probe", 0); }
    if (root >= 0) close(root);
    check("create guest symlink to private work", symlink("/.union-work", ROOT "/private-work-alias") == 0);
    errno = 0;
    probe = open(ROOT "/private-work-alias/.namespace-lock", O_RDWR);
    check("symlink alias cannot obtain namespace lock authority", probe == -1 && errno == ENOENT);
    if (probe >= 0) close(probe);
    check("remove private-work probe symlink", unlink(ROOT "/private-work-alias") == 0);
    DIR* directory = opendir("/");
    int hidden = directory != NULL;
    struct dirent* entry;
    errno = 0;
    while (directory && (entry = readdir(directory))) if (!strcmp(entry->d_name, ".union-work")) hidden = 0;
    int scan_error = errno;
    if (directory && closedir(directory) != 0) hidden = 0;
    check("private work is omitted from merged root enumeration", hidden && !scan_error);
    errno = 0;
    result = rmdir("/");
    check("root removal rejects without detaching prefix", result == -1 && errno == EBUSY && visible(ROOT "/nonempty/child"));
    errno = 0;
    result = rename("/", ROOT "/root-moved");
    check("root rename rejects without moving prefix", result == -1 && errno == EBUSY && visible(ROOT "/nonempty/child"));
}
static void rename_noop_checks(void) {
    struct stat before = {0}, after = {0};
    int existed = stat(ROOT "/nonempty", &before) == 0;
    check("same-name nonempty-directory rename succeeds", rename(ROOT "/nonempty", ROOT "/nonempty") == 0);
    check("same-name rename preserves directory identity and children", existed &&
        stat(ROOT "/nonempty", &after) == 0 && before.st_dev == after.st_dev &&
        before.st_ino == after.st_ino && visible(ROOT "/nonempty/child"));
    check("create actual hardlink no-op fixture", link(ROOT "/rmdir-file", ROOT "/noop-hardlink") == 0);
    int fd = open(ROOT "/rmdir-file", O_RDONLY);
    existed = stat(ROOT "/rmdir-file", &before) == 0;
    check("distinct same-inode rename succeeds", rename(ROOT "/rmdir-file", ROOT "/noop-hardlink") == 0);
    struct stat other = {0};
    check("same-inode rename preserves both names and link identity", existed &&
        stat(ROOT "/rmdir-file", &after) == 0 && stat(ROOT "/noop-hardlink", &other) == 0 &&
        after.st_dev == before.st_dev && after.st_ino == before.st_ino &&
        other.st_dev == before.st_dev && other.st_ino == before.st_ino &&
        after.st_nlink == before.st_nlink && other.st_nlink == before.st_nlink);
    char path[4096];
    check("same-inode no-op does not rename opened descriptor binding", fd >= 0 &&
        fcntl(fd, F_GETPATH, path) == 0 && !strcmp(path, ROOT "/rmdir-file"));
    if (fd >= 0) close(fd);
}
int main(void) {
    libc_directory_checks();
    publication_checks();
    removal_checks();
    rename_noop_checks();
    descriptor_checks();
    directory_promotion_checks();
    descriptor_protection_checks();
    private_namespace_checks();
    if (failures) return 1;
    puts("WEST_EUNION_STATE_OK");
    return 0;
}
