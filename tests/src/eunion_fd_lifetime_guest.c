#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#define ROOT "/usr/share/darling/tests/eunion/metadata"
static int failures;
static int checks;

static void expect(const char* state, const char* transition, int ok)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FAIL fd-lifetime %s: %s (errno=%d: %s)\n", state, transition, errno, strerror(errno));
    }
}

static int value_is(int fd, const char* expected)
{
    char value[64];
    ssize_t size = fgetxattr(fd, "test.detached", value, sizeof(value), 0, 0);
    return size == (ssize_t)strlen(expected) && !memcmp(value, expected, (size_t)size);
}

static int create_replacement(const char* path)
{
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (fd >= 0 && write(fd, "REPLACEMENT\n", 12) != 12) {
        close(fd);
        return -1;
    }
    return fd;
}

static void lifetime_case(const char* state, const char* path, const char* rename_source,
    int recreate)
{
    char original[64], observed[64], replacement[64];
    int fd = open(path, O_RDONLY);
    int alias = fd >= 0 ? dup(fd) : -1;
    int fresh = -1;
    ssize_t original_size = fd >= 0 ? pread(fd, original, sizeof(original), 0) : -1;
    struct stat st;
    int seeded = fd >= 0 && alias >= 0 && fstat(fd, &st) == 0 &&
        original_size == st.st_size && lseek(fd, 2, SEEK_SET) == 2;
    expect(state, "retain original lower inode, alias and shared offset", seeded);
    if (!seeded)
        goto out;

    int changed = rename_source ? rename(rename_source, path) : unlink(path);
    expect(state, "commit namespace removal or replacement", changed == 0);
    if (changed != 0)
        goto out;
    if (rename_source)
        fresh = open(path, O_RDONLY);
    else if (recreate)
        fresh = create_replacement(path);
    ssize_t replacement_size = fresh >= 0 ? pread(fresh, replacement, sizeof(replacement), 0) : -1;
    if (rename_source || recreate) {
        expect(state, "retain independent visible replacement inode", fresh >= 0 && replacement_size >= 0);
        if (fresh < 0 || replacement_size < 0)
            goto out;
    }

    expect(state, "first metadata mutation through detached lower alias succeeds",
        fsetxattr(alias, "test.detached", "DETACHED", 8, 0, 0) == 0);
    expect(state, "original fd observes detached metadata", value_is(fd, "DETACHED"));
    expect(state, "preexisting alias observes detached metadata", value_is(alias, "DETACHED"));
    expect(state, "metadata promotion preserves original bytes",
        pread(fd, observed, sizeof(observed), 0) == original_size &&
        !memcmp(observed, original, (size_t)original_size));
    expect(state, "metadata promotion preserves shared offset", lseek(alias, 0, SEEK_CUR) == 2);
    errno = 0;
    expect(state, "metadata promotion cannot upgrade read-only access",
        write(alias, "X", 1) == -1 && errno == EBADF);

    if (fresh >= 0) {
        errno = 0;
        expect(state, "metadata cannot leak onto replacement inode",
            fgetxattr(fresh, "test.detached", observed, sizeof(observed), 0, 0) == -1 && errno == ENOATTR);
        expect(state, "replacement bytes remain independent",
            pread(fresh, observed, sizeof(observed), 0) == replacement_size &&
            !memcmp(observed, replacement, (size_t)replacement_size));
        int reopened = open(path, O_RDONLY);
        errno = 0;
        expect(state, "replacement pathname remains independent",
            reopened >= 0 && fgetxattr(reopened, "test.detached", observed, sizeof(observed), 0, 0) == -1 && errno == ENOATTR);
        if (reopened >= 0)
            close(reopened);
    } else {
        errno = 0;
        expect(state, "detached mutation cannot resurrect removed pathname",
            access(path, F_OK) == -1 && errno == ENOENT);
    }

out:
    if (fresh >= 0) close(fresh);
    if (alias >= 0) close(alias);
    if (fd >= 0) close(fd);
}

int main(void)
{
    lifetime_case("lower-unlinked", ROOT "/fdmeta.txt", NULL, 0);
    lifetime_case("lower-recreated", ROOT "/fdxattr.txt", NULL, 1);
    lifetime_case("lower-renamed-over", ROOT "/pathxattr.txt", ROOT "/ftrunc.txt", 0);
    printf("EUNION_FD_LIFETIME checks=%d failures=%d\n", checks, failures);
    if (failures)
        return 1;
    puts("WEST_EUNION_FD_LIFETIME_OK");
    return 0;
}
