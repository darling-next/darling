#import <Foundation/Foundation.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    @autoreleasepool {
        char path[] = "/private/var/tmp/file-coordination-XXXXXX";
        int descriptor = mkstemp(path);
        if (descriptor < 0) {
            perror("mkstemp");
            return 1;
        }
        static const char original[] = "coordinated-original";
        static const char replacement[] = "coordinated-replacement";
        if (write(descriptor, original, sizeof(original)) != (ssize_t)sizeof(original) || close(descriptor) != 0) {
            perror("prepare file");
            unlink(path);
            return 1;
        }
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
        __block int reads = 0;
        __block int writes = 0;
        __block int failures = 0;
        NSError *error = nil;

        puts("FILE_COORDINATION_READ_BEGIN");
        [coordinator coordinateReadingItemAtURL:url options:0 error:&error byAccessor:^(NSURL *coordinatedURL) {
            ++reads;
            char bytes[sizeof(original)] = {0};
            int fd = open([[coordinatedURL path] fileSystemRepresentation], O_RDONLY);
            if (fd < 0) {
                ++failures;
                return;
            }
            if (read(fd, bytes, sizeof(bytes)) != (ssize_t)sizeof(bytes) || memcmp(bytes, original, sizeof(bytes)) != 0)
                ++failures;
            if (close(fd) != 0)
                ++failures;
            puts("FILE_COORDINATION_READ_ACCESSOR");
        }];
        if (error != nil || reads != 1)
            ++failures;
        puts("FILE_COORDINATION_READ_RETURNED");

        error = nil;
        puts("FILE_COORDINATION_WRITE_BEGIN");
        [coordinator coordinateWritingItemAtURL:url options:0 error:&error byAccessor:^(NSURL *coordinatedURL) {
            ++writes;
            int fd = open([[coordinatedURL path] fileSystemRepresentation], O_WRONLY | O_TRUNC);
            if (fd < 0) {
                ++failures;
                return;
            }
            if (write(fd, replacement, sizeof(replacement)) != (ssize_t)sizeof(replacement))
                ++failures;
            if (close(fd) != 0)
                ++failures;
            puts("FILE_COORDINATION_WRITE_ACCESSOR");
        }];
        if (error != nil || writes != 1)
            ++failures;
        puts("FILE_COORDINATION_WRITE_RETURNED");

        descriptor = open(path, O_RDONLY);
        char bytes[sizeof(replacement)] = {0};
        if (descriptor < 0) {
            ++failures;
        } else {
            if (read(descriptor, bytes, sizeof(bytes)) != (ssize_t)sizeof(bytes) || memcmp(bytes, replacement, sizeof(bytes)) != 0)
                ++failures;
            if (close(descriptor) != 0)
                ++failures;
        }
        if (unlink(path) != 0)
            ++failures;
        [coordinator release];
        printf("FILE_COORDINATION_RESULT reads=%d writes=%d failures=%d\n", reads, writes, failures);
        if (failures != 0)
            return 1;
        puts("FILE_COORDINATION_ROUNDTRIP_OK");
    }
    return 0;
}
