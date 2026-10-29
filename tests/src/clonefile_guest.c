#include <sys/clonefile.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REQUIRE(condition, label) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL clonefile %s errno=%d (%s)\n", label, errno, strerror(errno)); \
        result = 1; goto cleanup; \
    } \
} while (0)

int main(void)
{
    char root[] = "/private/var/tmp/clonefile-guest-XXXXXX";
    char source[256] = "", target[256] = "", existing[256] = "", link[256] = "";
    const char payload[] = "clone capability must not become a successful byte copy";
    int result = 0, source_fd = -1, target_fd = -1;
    if (!mkdtemp(root)) { perror("clonefile mkdtemp"); return 1; }
    snprintf(source, sizeof(source), "%s/source", root);
    snprintf(target, sizeof(target), "%s/target", root);
    snprintf(existing, sizeof(existing), "%s/existing", root);
    snprintf(link, sizeof(link), "%s/link", root);
    source_fd = open(source, O_RDWR | O_CREAT | O_EXCL, 0640);
    REQUIRE(source_fd >= 0, "create source");
    REQUIRE(write(source_fd, payload, sizeof(payload)) == (ssize_t)sizeof(payload), "write source");
    REQUIRE(fchmod(source_fd, 0640) == 0, "set source mode");
    REQUIRE(lseek(source_fd, 7, SEEK_SET) == 7, "set source offset");
    target_fd = open(existing, O_WRONLY | O_CREAT | O_EXCL, 0600);
    REQUIRE(target_fd >= 0, "create existing destination");
    REQUIRE(write(target_fd, "kept", 4) == 4, "write existing destination");
    REQUIRE(close(target_fd) == 0, "close existing destination");
    target_fd = -1;

    errno = 0;
    REQUIRE(fclonefileat(source_fd, AT_FDCWD, existing, 0) == -1 && errno == EEXIST,
            "existing destination must not be replaced");
    errno = 0;
    REQUIRE(fclonefileat(source_fd, AT_FDCWD, target, 0x80000000U) == -1 && errno == EINVAL,
            "invalid flags");
    errno = 0;
    REQUIRE(fclonefileat(-1, AT_FDCWD, target, 0) == -1 && errno == EBADF,
            "invalid source descriptor");
    REQUIRE(symlink(source, link) == 0, "create symlink source");
    errno = 0;
    REQUIRE(clonefile(link, target, CLONE_NOFOLLOW) == -1 && errno == ENOTSUP,
            "nofollow must not clone the symlink target");

    int cloned = fclonefileat(source_fd, AT_FDCWD, target, CLONE_NOOWNERCOPY);
    int clone_error = errno;
    REQUIRE(lseek(source_fd, 0, SEEK_CUR) == 7, "clone preserves source offset");
    if (cloned == -1) {
        REQUIRE(clone_error == ENOTSUP, "unsupported reflink uses Darwin ENOTSUP");
        struct stat absent;
        errno = 0;
        REQUIRE(lstat(target, &absent) == -1 && errno == ENOENT,
                "unsupported clone leaves no destination");
        puts("CLONEFILE_GUEST_CAPABILITY=ENOTSUP");
    } else {
        REQUIRE(cloned == 0, "clone result");
        target_fd = open(target, O_RDONLY);
        REQUIRE(target_fd >= 0, "open cloned file");
        char bytes[sizeof(payload) + 1];
        REQUIRE(read(target_fd, bytes, sizeof(bytes)) == (ssize_t)sizeof(payload) &&
                memcmp(bytes, payload, sizeof(payload)) == 0, "clone exact contents");
        struct stat original, copy;
        REQUIRE(fstat(source_fd, &original) == 0 && fstat(target_fd, &copy) == 0,
                "read clone metadata");
        REQUIRE(copy.st_ino != original.st_ino && copy.st_dev == original.st_dev &&
                (copy.st_mode & 07777) == 0640 && copy.st_size == original.st_size,
                "clone independent inode and mode");
        puts("CLONEFILE_GUEST_CAPABILITY=REFLINK");
    }
    if (target_fd >= 0) { REQUIRE(close(target_fd) == 0, "close clone"); target_fd = -1; }
    target_fd = open(existing, O_RDONLY);
    REQUIRE(target_fd >= 0, "reopen existing destination");
    char kept[5];
    REQUIRE(read(target_fd, kept, sizeof(kept)) == 4 && memcmp(kept, "kept", 4) == 0,
            "existing destination contents preserved");
cleanup:
    if (target_fd >= 0) close(target_fd);
    if (source_fd >= 0) close(source_fd);
    if (unlink(link) != 0 && errno != ENOENT) result = 1;
    if (unlink(target) != 0 && errno != ENOENT) result = 1;
    if (unlink(existing) != 0 && errno != ENOENT) result = 1;
    if (unlink(source) != 0 && errno != ENOENT) result = 1;
    if (rmdir(root) != 0) result = 1;
    if (result == 0) puts("CLONEFILE_GUEST_OK");
    return result;
}
