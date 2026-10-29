#import <Foundation/Foundation.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
extern void _CFSuddenTerminationEnable(void);
extern void _CFSuddenTerminationExitIfTerminationEnabled(int status);

static int child(const char *mode)
{
    @autoreleasepool {
        NSProcessInfo *process = [NSProcessInfo processInfo];
        _CFSuddenTerminationEnable();
        if (!strcmp(mode, "nested")) {
            id<NSObject> outer = [[process beginActivityWithOptions:
                NSActivitySuddenTerminationDisabled | NSActivityAutomaticTerminationDisabled
                reason:@"outer operation"] retain];
            @autoreleasepool {
                [process beginActivityWithOptions:NSActivitySuddenTerminationDisabled
                                          reason:@"inner operation"];
                [process endActivity:outer];
                _CFSuddenTerminationExitIfTerminationEnabled(42);
            }
            [outer release];
        } else if (!strcmp(mode, "automatic")) {
            id<NSObject> activity = [[process beginActivityWithOptions:NSActivityAutomaticTerminationDisabled
                                                               reason:@"pending operation"] retain];
            process.automaticTerminationSupportEnabled = YES;
            _CFSuddenTerminationExitIfTerminationEnabled(42);
            [process endActivity:activity];
            [activity release];
        } else if (!strcmp(mode, "exception")) {
            BOOL caught = NO;
            @try {
                [process performActivityWithOptions:NSActivitySuddenTerminationDisabled |
                                                    NSActivityAutomaticTerminationDisabled
                                             reason:@"throwing operation" usingBlock:^{
                    _CFSuddenTerminationExitIfTerminationEnabled(42);
                    [NSException raise:@"ActivityConsumerException" format:@"operation failed"];
                }];
            } @catch (NSException *error) {
                caught = [[error name] isEqualToString:@"ActivityConsumerException"];
            }
            if (!caught) return 95;
        } else if (!strcmp(mode, "invalid")) {
            BOOL caught = NO;
            @try {
                [process beginActivityWithOptions:NSActivitySuddenTerminationDisabled reason:@""];
            } @catch (NSException *error) {
                caught = [[error name] isEqualToString:NSInvalidArgumentException];
            }
            if (!caught) return 96;
        } else if (!strcmp(mode, "unsupported")) {
            BOOL caught = NO;
            @try {
                [process beginActivityWithOptions:NSActivityLatencyCritical reason:@"precision requested"];
            } @catch (NSException *error) {
                caught = [[error name] isEqualToString:NSInvalidArgumentException];
            }
            if (!caught) return 99;
        } else {
            return 97;
        }
        _CFSuddenTerminationExitIfTerminationEnabled(43);
        return 98;
    }
}

int main(int argc, char **argv)
{
    if (argc == 2) return child(argv[1]);
    const char *modes[] = {"nested", "automatic", "exception", "invalid", "unsupported"};
    for (size_t index = 0; index < sizeof(modes) / sizeof(modes[0]); index++) {
        pid_t pid;
        char *args[] = {argv[0], (char *)modes[index], NULL};
        int error = posix_spawn(&pid, argv[0], NULL, NULL, args, environ);
        if (error) { fprintf(stderr, "FAIL activity spawn mode=%s error=%d\n", modes[index], error); return 1; }
        int status = -1;
        if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 43) {
            fprintf(stderr, "FAIL activity lifetime mode=%s status=%d expected exit after completion=43\n",
                    modes[index], status);
            return 2;
        }
    }
    puts("PROCESS_ACTIVITY_GUEST_OK");
    return 0;
}
