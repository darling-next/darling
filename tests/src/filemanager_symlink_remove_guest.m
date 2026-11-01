#import <Foundation/Foundation.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define REQUIRE(condition, label) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL filemanager symlink removal: %s\n", label); \
        return 1; \
    } \
} while (0)

static const char payload[] = "the link target must survive unchanged\n";

static BOOL writePayload(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return NO;
    ssize_t written = write(fd, payload, sizeof(payload) - 1);
    int closed = close(fd);
    return written == (ssize_t)(sizeof(payload) - 1) && closed == 0;
}

static BOOL keptPayload(const char *path, ino_t original)
{
    struct stat info;
    if (lstat(path, &info) != 0 || info.st_ino != original || !S_ISREG(info.st_mode)) return NO;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NO;
    char actual[sizeof(payload)];
    ssize_t count = read(fd, actual, sizeof(actual));
    int closed = close(fd);
    return count == (ssize_t)(sizeof(payload) - 1) && closed == 0 &&
           memcmp(actual, payload, sizeof(payload) - 1) == 0;
}

static BOOL absent(const char *path)
{
    struct stat info;
    return lstat(path, &info) == -1 && errno == ENOENT;
}

int main(void)
{
    @autoreleasepool {
        char base[] = "/private/var/tmp/filemanager-remove-links-XXXXXX";
        REQUIRE(mkdtemp(base) != NULL, "create isolated fixture directory");
        char target[256], keep[256], rootLink[256], tree[256], child[256], nested[256], dangling[256], missing[256];
        snprintf(target, sizeof(target), "%s/target", base);
        snprintf(keep, sizeof(keep), "%s/target/keep", base);
        snprintf(rootLink, sizeof(rootLink), "%s/root-link", base);
        snprintf(tree, sizeof(tree), "%s/tree", base);
        snprintf(child, sizeof(child), "%s/tree/child", base);
        snprintf(nested, sizeof(nested), "%s/tree/nested-link", base);
        snprintf(dangling, sizeof(dangling), "%s/dangling", base);
        snprintf(missing, sizeof(missing), "%s/missing", base);
        REQUIRE(mkdir(target, 0700) == 0 && writePayload(keep), "create real link-target payload");
        struct stat original;
        REQUIRE(lstat(keep, &original) == 0, "identify original target");
        REQUIRE(symlink(target, rootLink) == 0, "create root directory symlink");
        NSFileManager *manager = [NSFileManager defaultManager];
        NSError *error = nil;
        REQUIRE([manager removeItemAtPath:[NSString stringWithUTF8String:rootLink] error:&error],
                "remove root symlink itself");
        REQUIRE(absent(rootLink), "root symlink remains");
        REQUIRE(keptPayload(keep, original.st_ino), "root symlink removal damaged its target");

        REQUIRE(mkdir(tree, 0700) == 0 && writePayload(child) && symlink(target, nested) == 0,
                "create tree with a real child and external directory link");
        error = nil;
        REQUIRE([manager removeItemAtPath:[NSString stringWithUTF8String:tree] error:&error],
                "remove ordinary directory tree");
        REQUIRE(absent(tree), "ordinary directory tree remains");
        REQUIRE(keptPayload(keep, original.st_ino), "nested symlink traversal damaged external target");

        REQUIRE(symlink(missing, dangling) == 0, "create dangling symlink");
        error = nil;
        REQUIRE([manager removeItemAtPath:[NSString stringWithUTF8String:dangling] error:&error],
                "remove dangling symlink without following it");
        REQUIRE(absent(dangling) && absent(missing), "dangling-link removal changed the missing target");
        REQUIRE(keptPayload(keep, original.st_ino), "unrelated original target changed");
        REQUIRE(unlink(keep) == 0 && rmdir(target) == 0 && rmdir(base) == 0, "remove owned fixture contents");
    }
    puts("FILEMANAGER_SYMLINK_REMOVE_PASS=1");
    return 0;
}
