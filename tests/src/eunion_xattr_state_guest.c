#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#define ROOT "/usr/share/darling/tests/eunion/xattr-state"

static int failures;
static int checks;

static void expect(const char* object, const char* transition, int ok)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FAIL xattr %s: %s (errno=%d: %s)\n", object, transition, errno, strerror(errno));
    }
}

static int value_is(const char* path, int fd, const char* name, const char* expected)
{
    char value[128] = {0};
    ssize_t size = fd < 0 ? getxattr(path, name, value, sizeof(value), 0, 0) :
        fgetxattr(fd, name, value, sizeof(value), 0, 0);
    return size == (ssize_t)strlen(expected) && !memcmp(value, expected, (size_t)size);
}

static int attr_absent(const char* path, int fd, const char* name)
{
    char value[128];
    errno = 0;
    ssize_t size = fd < 0 ? getxattr(path, name, value, sizeof(value), 0, 0) :
        fgetxattr(fd, name, value, sizeof(value), 0, 0);
    return size == -1 && errno == ENOATTR;
}

static int listed(const char* path, int fd, const char* wanted)
{
    char names[1024];
    ssize_t size = fd < 0 ? listxattr(path, names, sizeof(names), 0) :
        flistxattr(fd, names, sizeof(names), 0);
    ssize_t measured = fd < 0 ? listxattr(path, NULL, 0, 0) :
        flistxattr(fd, NULL, 0, 0);
    int found = wanted == NULL;
    if (size < 0 || size > (ssize_t)sizeof(names) || measured != size)
        return 0;
    for (size_t pos = 0; pos < (size_t)size;) {
        const char* end = memchr(names + pos, 0, (size_t)size - pos);
        if (!end || !strncmp(names + pos, "union.", 6) ||
            !strncmp(names + pos, "user.union.", 11))
            return 0;
        if (wanted && !strcmp(names + pos, wanted))
            found = 1;
        pos = (size_t)(end - names) + 1;
    }
    return found;
}

static void remove_cycle(const char* path, const char* reopen, int by_fd)
{
    int fd = by_fd ? open(path, O_RDONLY) : -1;
    expect(path, "open original lower fd", !by_fd || fd >= 0);
    if (by_fd && fd < 0)
        return;
    int alias = by_fd ? dup(fd) : -1;
    expect(path, "duplicate original opened description", !by_fd || alias >= 0);
    expect(path, "initial normalized value and native size", value_is(path, fd, "test.remove", "REMOVE_ME"));
    int ret = by_fd ? fremovexattr(fd, "test.remove", 0) : removexattr(path, "test.remove", 0);
    expect(path, "present to absent removal succeeds", ret == 0);
    expect(path, "path observes attribute absence", attr_absent(path, -1, "test.remove"));
    expect(path, "unrelated path value survives removal", value_is(path, -1, "test.keep", "KEEP_ME"));
    if (by_fd) {
        expect(path, "same fd observes successful attribute removal", attr_absent(NULL, fd, "test.remove"));
        expect(path, "preexisting alias observes attribute removal", alias >= 0 && attr_absent(NULL, alias, "test.remove"));
        expect(path, "same fd preserves unrelated attribute", value_is(NULL, fd, "test.keep", "KEEP_ME"));
    }
    errno = 0;
    ret = by_fd ? fremovexattr(fd, "test.remove", 0) : removexattr(path, "test.remove", 0);
    expect(path, "absent removal reports ENOATTR", ret == -1 && errno == ENOATTR);
    ret = by_fd ? fsetxattr(fd, "test.remove", "READDED", 7, 0, 0) :
        setxattr(path, "test.remove", "READDED", 7, 0, 0);
    expect(path, "absent to present re-add succeeds", ret == 0 && value_is(path, fd, "test.remove", "READDED"));
    if (by_fd)
        expect(path, "preexisting alias observes successful re-add", alias >= 0 && value_is(NULL, alias, "test.remove", "READDED"));
    expect(path, "list and size query expose normalized names only", listed(path, fd, "test.remove"));
    ret = by_fd ? fremovexattr(alias >= 0 ? alias : fd, "user.test.remove", 0) : removexattr(path, "user.test.remove", 0);
    expect(path, "explicit namespace removes re-added value", ret == 0 && attr_absent(path, fd, "test.remove"));
    expect(path, "second removal preserves unrelated value", value_is(path, fd, "test.keep", "KEEP_ME"));
    if (by_fd) {
        expect(path, "unlink pathname while descriptions remain open", unlink(path) == 0);
        int replacement = open(reopen, O_CREAT | O_EXCL | O_RDWR, 0644);
        expect(path, "create replacement inode at old pathname", replacement >= 0);
        if (replacement >= 0) {
            expect(path, "seed replacement attribute", fsetxattr(replacement, "test.remove", "REPLACEMENT", 11, 0, 0) == 0);
            expect(path, "old fd mutation succeeds on detached inode", fsetxattr(fd, "test.remove", "DETACHED", 8, 0, 0) == 0);
            expect(path, "old fd and alias observe detached value", value_is(NULL, fd, "test.remove", "DETACHED") &&
                alias >= 0 && value_is(NULL, alias, "test.remove", "DETACHED"));
            expect(path, "replacement fd and path are not retargeted", value_is(NULL, replacement, "test.remove", "REPLACEMENT") &&
                value_is(path, -1, "test.remove", "REPLACEMENT"));
            close(replacement);
        }
        if (alias >= 0)
            close(alias);
        close(fd);
    }
}

static void relative_cases(void)
{
    int saved = open(".", O_RDONLY);
    int directory = open(ROOT, O_RDONLY);
    expect("relative", "retain cwd and lower directory descriptors", saved >= 0 && directory >= 0);
    if (saved < 0 || directory < 0) {
        if (saved >= 0) close(saved);
        if (directory >= 0) close(directory);
        return;
    }
    expect("cwd", "chdir to guest input directory", chdir(ROOT) == 0);
    remove_cycle("cwd-remove", ROOT "/cwd-remove", 0);
    expect("cwd-set", "relative set creates upper value", setxattr("cwd-set", "com.apple.test", "NEW", 3, 0, 0) == 0 &&
        value_is("cwd-set", -1, "com.apple.test", "NEW"));
    expect("dirfd", "restore original cwd", fchdir(saved) == 0);
    /* xattr has no *at syscall: fchdir supplies directory-descriptor authority. */
    expect("dirfd", "enter directory by retained fd", fchdir(directory) == 0);
    remove_cycle("thread-remove", ROOT "/thread-remove", 0);
    expect("thread-set", "descriptor-relative set creates upper value", setxattr("thread-set", "com.apple.test", "NEW", 3, 0, 0) == 0 &&
        value_is("thread-set", -1, "com.apple.test", "NEW"));
    expect("dirfd", "restore cwd after descriptor-relative operations", fchdir(saved) == 0);
    close(directory);
    close(saved);
}

static void symlink_cases(void)
{
    char value[128];
    struct stat st;
    expect("follow", "installed final symlink exists", lstat(ROOT "/set-link", &st) == 0 && S_ISLNK(st.st_mode));
    expect("follow", "set follows final symlink to upper target", setxattr(ROOT "/set-link", "test.remove", "CHANGED", 7, 0, 0) == 0 &&
        value_is(ROOT "/follow-set", -1, "test.remove", "CHANGED"));
    expect("follow", "set preserves final symlink identity", lstat(ROOT "/set-link", &st) == 0 && S_ISLNK(st.st_mode));
    remove_cycle(ROOT "/remove-link", ROOT "/follow-remove", 0);
    errno = 0;
    expect("nofollow", "get never reads target attribute", getxattr(ROOT "/nofollow-link", "test.remove", value, sizeof(value), 0, XATTR_NOFOLLOW) == -1 && errno == ENOATTR);
    expect("nofollow", "list never reads target attributes", listxattr(ROOT "/nofollow-link", value, sizeof(value), XATTR_NOFOLLOW) == 0);
    errno = 0;
    expect("nofollow", "remove returns absent link attribute", removexattr(ROOT "/nofollow-link", "test.remove", XATTR_NOFOLLOW) == -1 && errno == ENOATTR);
    errno = 0;
    expect("nofollow", "set preserves native Linux symlink prohibition", setxattr(ROOT "/nofollow-link", "test.remove", "BAD", 3, 0, XATTR_NOFOLLOW) == -1 && errno == EPERM);
    expect("nofollow", "target attribute unchanged after nofollow operations", value_is(ROOT "/nofollow", -1, "test.remove", "REMOVE_ME"));
}

static void marker_cases(void)
{
    const char* opaque = ROOT "/opaque";
    expect("opaque", "unlink visible lower child before rmdir", unlink(ROOT "/opaque/.lower") == 0);
    expect("opaque", "remove logically empty lower directory", rmdir(opaque) == 0);
    expect("opaque", "recreate directory with production opaque marker", mkdir(opaque, 0755) == 0);
    int fd = open(opaque, O_RDONLY);
    expect("opaque", "open recreated directory", fd >= 0);
    if (fd < 0)
        return;
    static const char* const names[] = { "user.union.opaque", "union.opaque", "user.union.whiteout", "union.whiteout" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char* name = names[i];
        expect(name, "path read hides protected marker", attr_absent(opaque, -1, name));
        expect(name, "fd read hides protected marker", attr_absent(NULL, fd, name));
        errno = 0;
        expect(name, "path marker set forbidden", setxattr(opaque, name, "BAD", 3, 0, 0) == -1 && errno == EPERM);
        errno = 0;
        expect(name, "fd marker set forbidden", fsetxattr(fd, name, "BAD", 3, 0, 0) == -1 && errno == EPERM);
        errno = 0;
        expect(name, "path marker removal forbidden", removexattr(opaque, name, 0) == -1 && errno == EPERM);
        errno = 0;
        expect(name, "fd marker removal forbidden", fremovexattr(fd, name, 0) == -1 && errno == EPERM);
        expect(name, "path and fd lists hide markers including size query", listed(opaque, -1, NULL) && listed(NULL, fd, NULL));
        errno = 0;
        expect(name, "rejected mutations cannot resurrect opaque child", access(ROOT "/opaque/.lower", F_OK) == -1 && errno == ENOENT);
    }
    close(fd);

    expect("whiteout", "unlink lower file using production whiteout", unlink(ROOT "/whiteout") == 0);
    expect("whiteout", "whiteout marker read hidden", attr_absent(ROOT "/whiteout", -1, "union.whiteout"));
    errno = 0;
    expect("whiteout", "cannot clear whiteout marker", removexattr(ROOT "/whiteout", "union.whiteout", 0) == -1 && errno == EPERM);
    errno = 0;
    expect("whiteout", "whiteout entry remains absent", access(ROOT "/whiteout", F_OK) == -1 && errno == ENOENT);
    char list[128];
    errno = 0;
    expect("whiteout", "list reports absent namespace entry", listxattr(ROOT "/whiteout", list, sizeof(list), 0) == -1 && errno == ENOENT);
}

static void symlink_fd_cases(void)
{
    const char* path = ROOT "/fd-symlink";
    char names[128];
    struct stat before, after;
    memset(&before, 0, sizeof(before));
    expect("symlink-fd", "create upper symlink to attributed target", symlink("absolute", path) == 0);
    int fd = open(path, O_SYMLINK);
    expect("symlink-fd", "production open retains symlink inode", fd >= 0 && fstat(fd, &before) == 0 && S_ISLNK(before.st_mode));
    if (fd < 0)
        return;
    int alias = dup(fd);
    expect("symlink-fd", "duplicate symbolic description", alias >= 0);
    expect("symlink-fd", "size query does not list target attributes", flistxattr(fd, NULL, 0, 0) == 0);
    expect("symlink-fd", "data query does not list target attributes", flistxattr(fd, names, sizeof(names), 0) == 0);
    expect("symlink-fd", "unlink opened upper symlink", unlink(path) == 0);
    int replacement = open(path, O_CREAT | O_EXCL | O_RDWR, 0644);
    expect("symlink-fd", "create distinct regular replacement", replacement >= 0);
    if (replacement >= 0) {
        expect("symlink-fd", "seed replacement attribute", fsetxattr(replacement, "test.replacement", "REPLACEMENT", 11, 0, 0) == 0);
        expect("symlink-fd", "replacement has independently nonempty listing", flistxattr(replacement, names, sizeof(names), 0) > 0);
        expect("symlink-fd", "opened descriptor retains original symlink identity", fstat(fd, &after) == 0 &&
            before.st_dev == after.st_dev && before.st_ino == after.st_ino && S_ISLNK(after.st_mode));
        expect("symlink-fd", "detached size query never lists replacement", flistxattr(fd, NULL, 0, 0) == 0);
        expect("symlink-fd", "detached data query never lists replacement", flistxattr(fd, names, sizeof(names), 0) == 0);
        expect("symlink-fd", "preexisting alias retains detached symlink listing", alias >= 0 &&
            flistxattr(alias, NULL, 0, 0) == 0 && flistxattr(alias, names, sizeof(names), 0) == 0);
        close(replacement);
    }
    if (alias >= 0)
        close(alias);
    close(fd);
}

static void option_cases(void)
{
    const char* path = ROOT "/absolute";
    expect("options", "CREATE sets an absent attribute", setxattr(path, "test.remove", "OPTIONS", 7, 0, XATTR_CREATE) == 0);
    errno = 0;
    expect("options", "CREATE rejects existing attribute", setxattr(path, "test.remove", "BAD", 3, 0, XATTR_CREATE) == -1 && errno == EEXIST);
    errno = 0;
    expect("options", "REPLACE rejects absent attribute", setxattr(path, "test.absent", "BAD", 3, 0, XATTR_REPLACE) == -1 && errno == ENOATTR);
    int fd = open(path, O_RDONLY);
    expect("options", "open upper descriptor for option checks", fd >= 0);
    if (fd < 0)
        return;
    expect("options", "fd REPLACE changes existing value", fsetxattr(fd, "test.remove", "REPLACED", 8, 0, XATTR_REPLACE) == 0 &&
        value_is(NULL, fd, "test.remove", "REPLACED"));
    errno = 0;
    expect("options", "fd CREATE rejects existing attribute", fsetxattr(fd, "test.remove", "BAD", 3, 0, XATTR_CREATE) == -1 && errno == EEXIST);
    errno = 0;
    expect("options", "conflicting flags rejected", fsetxattr(fd, "test.remove", "BAD", 3, 0, XATTR_CREATE | XATTR_REPLACE) == -1 && errno == EINVAL);
    errno = 0;
    expect("options", "invalid remove flags preserve value", fremovexattr(fd, "test.remove", XATTR_CREATE) == -1 && errno == EINVAL &&
        value_is(NULL, fd, "test.remove", "REPLACED"));
    char names[128];
    ssize_t size = flistxattr(fd, NULL, 0, 0);
    expect("options", "fd list accepts exact guest-sized buffer", size > 0 && size <= (ssize_t)sizeof(names) && flistxattr(fd, names, (size_t)size, 0) == size);
    expect("options", "remove option-test attribute", fremovexattr(fd, "test.remove", 0) == 0);
    close(fd);
}

int main(void)
{
    remove_cycle(ROOT "/absolute", ROOT "/absolute", 0);
    remove_cycle(ROOT "/fd", ROOT "/fd", 1);
    relative_cases();
    symlink_cases();
    marker_cases();
    symlink_fd_cases();
    option_cases();
    errno = 0;
    expect("domain", "missing path removal reports ENOENT", removexattr(ROOT "/missing", "test.remove", 0) == -1 && errno == ENOENT);
    errno = 0;
    expect("domain", "regular parent reports ENOTDIR", setxattr(ROOT "/absolute/child", "test.keep", "x", 1, 0, 0) == -1 && errno == ENOTDIR);
    errno = 0;
    expect("domain", "invalid fd reports EBADF", fremovexattr(-1, "test.remove", 0) == -1 && errno == EBADF);
    printf("EUNION_XATTR_STATE checks=%d failures=%d\n", checks, failures);
    if (failures)
        return 1;
    puts("WEST_EUNION_XATTR_STATE_OK");
    return 0;
}
