/* Run only in an isolated guest lane with an owned HFS+ image. No host disks.
 * Arguments: image-path fresh-mountpoint relative-payload expected-payload-file.
 * Both fresh-mountpoint and fresh-mountpoint.link must not exist. The expected
 * file must be outside the image. The parent supplies the image using its
 * existing fixture producer; this program never formats a disk. */
#include <DiskArbitration/DiskArbitration.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;
static DADiskRef volume;
static int appeared, disappeared, changed, completions;
static DAReturn completionStatus;
static dispatch_semaphore_t completionSemaphore;
static DASessionRef pauseSession;
static int paused, approvals;
static Boolean pauseOnChange = true, vetoEject = true;
static CFStringRef completionReason;
static CFRunLoopRef approvalRunLoop;

#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "image backend: line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static void onAppeared(DADiskRef disk, void *context)
{
    ++appeared;
    if (!volume) volume = (DADiskRef)CFRetain(disk);
}
static void onDisappeared(DADiskRef disk, void *context)
{
    CFDictionaryRef description = DADiskCopyDescription(disk);
    REQUIRE(description && CFDictionaryContainsKey(description, kDADiskDescriptionVolumePathKey));
    CFRelease(description);
    ++disappeared;
}
static void onChanged(DADiskRef disk, CFArrayRef keys, void *context)
{
    if (CFArrayContainsValue(keys, CFRangeMake(0, CFArrayGetCount(keys)),
        kDADiskDescriptionVolumePathKey)) ++changed;
}
static void onChangedPause(DADiskRef disk, CFArrayRef keys, void *context)
{
    if (!CFArrayContainsValue(keys, CFRangeMake(0, CFArrayGetCount(keys)),
        kDADiskDescriptionVolumePathKey)) return;
    ++paused;
    if (pauseOnChange) {
        pauseOnChange = false;
        DASessionUnscheduleFromRunLoop(pauseSession, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    }
}
static DADissenterRef onUnmountApproval(DADiskRef disk, void *context)
{
    REQUIRE(context != NULL && CFRunLoopGetCurrent() == approvalRunLoop);
    REQUIRE(volume && CFEqual(disk, volume));
    CFDictionaryRef description = DADiskCopyDescription(disk);
    REQUIRE(description && CFDictionaryContainsKey(description, kDADiskDescriptionVolumePathKey));
    CFRelease(description);
    ++approvals;
    return vetoEject ? DADissenterCreate(NULL, kDAReturnNotPermitted, CFSTR("image proof veto")) : NULL;
}
static void onOperation(DADiskRef disk, DADissenterRef dissenter, void *context)
{
    ++completions;
    completionStatus = dissenter ? DADissenterGetStatus(dissenter) : 0;
    if (dissenter) REQUIRE(DADissenterGetStatusString(dissenter) != NULL);
    if (completionReason) CFRelease(completionReason);
    completionReason = dissenter ? DADissenterGetStatusString(dissenter) : NULL;
    if (completionReason) CFRetain(completionReason);
    if (completionSemaphore) dispatch_semaphore_signal(completionSemaphore);
}
static void runUntil(int *value, int target)
{
    CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 30;
    while (*value < target && CFAbsoluteTimeGetCurrent() < deadline)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    if (*value != target)
        fprintf(stderr, "image backend: wait target=%d observed=%d appeared=%d changed=%d paused=%d completions=%d approvals=%d status=%#x\n",
            target, *value, appeared, changed, paused, completions, approvals, (unsigned)completionStatus);
    if (*value != target && completionReason) CFShow(completionReason);
    REQUIRE(*value == target);
}
static CFURLRef fileURL(const char *path, Boolean directory)
{
    CFURLRef result = CFURLCreateFromFileSystemRepresentation(NULL,
        (const UInt8 *)path, strlen(path), directory);
    REQUIRE(result != NULL);
    return result;
}
static int spawnHdiutil(const char *image)
{
    char *argv[] = { "/usr/bin/hdiutil", "attach", "-nomount", "-quiet", (char *)image, NULL };
    pid_t pid;
    REQUIRE(posix_spawn(&pid, argv[0], NULL, NULL, argv, environ) == 0);
    int status;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
/* The child waits for approvals delivered on this process's runloop. A blocking
 * waitpid here would deadlock the very callback whose veto we are testing. */
static int detachWhilePumping(const char *mountpoint)
{
    char *argv[] = { "/usr/bin/hdiutil", "detach", (char *)mountpoint, NULL };
    pid_t pid;
    REQUIRE(posix_spawn(&pid, argv[0], NULL, NULL, argv, environ) == 0);
    CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 30;
    int status;
    for (;;) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) break;
        REQUIRE(result == 0 || (result == -1 && errno == EINTR));
        if (CFAbsoluteTimeGetCurrent() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {}
            REQUIRE(!"hdiutil detach timed out while driving approvals");
        }
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    }
    REQUIRE(WIFEXITED(status));
    return WEXITSTATUS(status);
}
static void compareFiles(const char *actualPath, const char *expectedPath)
{
    FILE *actual = fopen(actualPath, "rb"), *expected = fopen(expectedPath, "rb");
    REQUIRE(actual && expected);
    unsigned char a[4096], b[4096];
    for (;;) {
        size_t na = fread(a, 1, sizeof(a), actual), nb = fread(b, 1, sizeof(b), expected);
        REQUIRE(na == nb && memcmp(a, b, na) == 0);
        if (!na) break;
    }
    REQUIRE(!ferror(actual) && !ferror(expected));
    fclose(actual); fclose(expected);
}
static void barrier(void *unused) {}

int main(int argc, char **argv)
{
    REQUIRE(argc == 5 && argv[2][0] == '/' && argv[3][0] != '/');
    REQUIRE(strstr(argv[3], "..") == NULL);
    struct stat st;
    REQUIRE(lstat(argv[2], &st) != 0);
    REQUIRE(errno == ENOENT);
    REQUIRE(mkdir(argv[2], 0700) == 0);
    struct statfs underlying, afterEject;
    REQUIRE(statfs(argv[2], &underlying) == 0);
    char linkPath[4096];
    int length = snprintf(linkPath, sizeof(linkPath), "%s.link", argv[2]);
    REQUIRE(length > 0 && (size_t)length < sizeof(linkPath));
    REQUIRE(symlink(argv[2], linkPath) == 0);
    DASessionRef session = DASessionCreate(NULL);
    REQUIRE(session != NULL);
    DADiskRef missing = DADiskCreateFromBSDName(NULL, session, "darling-nonexistent-test-disk");
    REQUIRE(missing && strcmp(DADiskGetBSDName(missing), "darling-nonexistent-test-disk") == 0);
    REQUIRE(DADiskCopyDescription(missing) == NULL);
    REQUIRE(DADiskCopyWholeDisk(missing) == NULL);
    CFRelease(missing);

    const void *key = kDADiskDescriptionVolumeMountableKey, *value = kCFBooleanTrue;
    CFDictionaryRef match = CFDictionaryCreate(NULL, &key, &value, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    DARegisterDiskAppearedCallback(session, match, onAppeared, NULL);
    pauseSession = session;
    DARegisterDiskDescriptionChangedCallback(session, match, NULL, onChangedPause, NULL);
    DARegisterDiskDescriptionChangedCallback(session, match, NULL, onChanged, NULL);
    DARegisterDiskDisappearedCallback(session, match, onDisappeared, NULL);
    DASessionRef approvalSession = DASessionCreate(NULL);
    REQUIRE(approvalSession != NULL && approvalSession != session);
    approvalRunLoop = CFRunLoopGetCurrent();
    DARegisterDiskUnmountApprovalCallback(approvalSession, match, onUnmountApproval, approvalSession);
    DASessionScheduleWithRunLoop(approvalSession, approvalRunLoop, kCFRunLoopDefaultMode);
    REQUIRE(spawnHdiutil(argv[1]) == 0);
    DASessionScheduleWithRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    runUntil(&appeared, 1);
    REQUIRE(volume && DADiskGetBSDName(volume) == NULL && DADiskCopyIOMedia(volume) == 0);
    CFDictionaryRef description = DADiskCopyDescription(volume);
    REQUIRE(description && !CFDictionaryContainsKey(description, kDADiskDescriptionVolumePathKey));
    REQUIRE(!CFDictionaryContainsKey(description, kDADiskDescriptionMediaBSDNameKey));
    CFRelease(description);
    DADiskRef whole = DADiskCopyWholeDisk(volume);
    REQUIRE(whole != NULL);
    description = DADiskCopyDescription(whole);
    REQUIRE(description && CFDictionaryGetValue(description, kDADiskDescriptionMediaWholeKey) == kCFBooleanTrue);
    CFRelease(description);
    char canonicalMountpoint[4096];
    REQUIRE(realpath(argv[2], canonicalMountpoint) != NULL);
    CFURLRef mountURL = fileURL(canonicalMountpoint, true);
    CFURLRef linkURL = fileURL(linkPath, true);

    CFStringRef writable[] = { CFSTR("rw"), NULL };
    DADiskMountWithArguments(volume, mountURL, 0, onOperation, NULL, writable);
    runUntil(&completions, 1);
    REQUIRE(completionStatus == kDAReturnUnsupported && DADiskCreateFromVolumePath(NULL, session, mountURL) == NULL);

    DASessionUnscheduleFromRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFStringRef readOnly[] = { CFSTR("rdonly"), CFSTR("nobrowse"), NULL };
    DADiskMountWithArguments(volume, linkURL, 0, onOperation, NULL, readOnly);
    usleep(250000);
    REQUIRE(completions == 1 && DADiskCreateFromVolumePath(NULL, session, mountURL) == NULL);
    DASessionScheduleWithRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    runUntil(&paused, 1);
    REQUIRE(changed == 0 && completions == 1);
    DASessionScheduleWithRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    runUntil(&completions, 2);
    REQUIRE(completionStatus == 0 && changed == 1 && paused == 1);
    description = DADiskCopyDescription(volume);
    REQUIRE(description && CFDictionaryContainsKey(description, kDADiskDescriptionVolumePathKey));
    CFRelease(description);
    DADiskRef mounted = DADiskCreateFromVolumePath(NULL, session, mountURL);
    REQUIRE(mounted && CFEqual(volume, mounted));
    description = DADiskCopyDescription(mounted);
    REQUIRE(description && CFEqual(CFDictionaryGetValue(description, kDADiskDescriptionVolumePathKey), mountURL));
    REQUIRE(CFDictionaryGetValue(description, kDADiskDescriptionMediaWritableKey) == kCFBooleanFalse);
    CFRelease(description);
    DADiskRef linked = DADiskCreateFromVolumePath(NULL, session, linkURL);
    REQUIRE(linked && CFEqual(mounted, linked));
    description = DADiskCopyDescription(linked);
    REQUIRE(description && CFEqual(CFDictionaryGetValue(description,
        kDADiskDescriptionVolumePathKey), mountURL));
    CFRelease(description);
    char payload[4096];
    REQUIRE(snprintf(payload, sizeof(payload), "%s/%s", argv[2], argv[3]) < sizeof(payload));
    compareFiles(payload, argv[4]);
    char linkedPayload[4096];
    length = snprintf(linkedPayload, sizeof(linkedPayload), "%s/%s", linkPath, argv[3]);
    REQUIRE(length > 0 && (size_t)length < sizeof(linkedPayload));
    compareFiles(linkedPayload, argv[4]);
    REQUIRE(stat(payload, &st) == 0);
    struct stat linkedStat;
    REQUIRE(stat(linkedPayload, &linkedStat) == 0);
    REQUIRE(st.st_dev == linkedStat.st_dev && st.st_ino == linkedStat.st_ino);

    DADiskMountWithArguments(mounted, mountURL, 0, onOperation, NULL, readOnly);
    runUntil(&completions, 3);
    REQUIRE(completionStatus == kDAReturnBusy);
    DADiskEject(whole, 0, onOperation, NULL);
    runUntil(&completions, 4);
    REQUIRE(completionStatus == kDAReturnNotPermitted && approvals == 1 && disappeared == 0);
    REQUIRE(completionReason && CFEqual(completionReason, CFSTR("image proof veto")));
    description = DADiskCopyDescription(volume);
    REQUIRE(description && CFDictionaryContainsKey(description, kDADiskDescriptionVolumePathKey));
    CFRelease(description);
    compareFiles(payload, argv[4]);
    REQUIRE(detachWhilePumping(argv[2]) != 0);
    REQUIRE(approvals == 2 && completions == 4 && disappeared == 0);
    DADiskRef retained = DADiskCreateFromVolumePath(NULL, session, linkURL);
    REQUIRE(retained && CFEqual(retained, mounted));
    CFRelease(retained);
    compareFiles(payload, argv[4]);
    compareFiles(linkedPayload, argv[4]);
    vetoEject = false;
    DASessionUnscheduleFromRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    completionSemaphore = dispatch_semaphore_create(0);
    dispatch_queue_t queue = dispatch_queue_create("image-backend-proof", DISPATCH_QUEUE_SERIAL);
    DASessionSetDispatchQueue(session, queue);
    DADiskEject(whole, 0, onOperation, NULL);
    CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 30;
    long waiting;
    while ((waiting = dispatch_semaphore_wait(completionSemaphore, DISPATCH_TIME_NOW)) != 0 &&
        CFAbsoluteTimeGetCurrent() < deadline)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    REQUIRE(waiting == 0);
    DASessionSetDispatchQueue(session, NULL);
    dispatch_sync_f(queue, NULL, barrier);
    REQUIRE(completions == 5 && completionStatus == 0 && disappeared == 1 && approvals == 3);
    REQUIRE(DADiskCreateFromVolumePath(NULL, session, mountURL) == NULL);
    REQUIRE(DADiskCreateFromVolumePath(NULL, session, linkURL) == NULL);
    REQUIRE(lstat(payload, &st) != 0);
    REQUIRE(DADiskCopyDescription(mounted) == NULL);
    REQUIRE(DADiskCopyDescription(volume) == NULL);
    REQUIRE(DADiskCopyDescription(linked) == NULL);
    REQUIRE(lstat(linkedPayload, &st) != 0);
    REQUIRE(statfs(argv[2], &afterEject) == 0);
    REQUIRE(memcmp(&underlying.f_fsid, &afterEject.f_fsid, sizeof(underlying.f_fsid)) == 0);
    DAUnregisterCallback(session, onAppeared, NULL);
    DAUnregisterCallback(session, onChanged, NULL);
    DAUnregisterCallback(session, onChangedPause, NULL);
    DAUnregisterApprovalCallback(approvalSession, onUnmountApproval, approvalSession);
    DASessionUnscheduleFromRunLoop(approvalSession, approvalRunLoop, kCFRunLoopDefaultMode);
    DAUnregisterCallback(session, onDisappeared, NULL);
    dispatch_release(queue);
    dispatch_release(completionSemaphore);
    if (completionReason) CFRelease(completionReason);
    CFRelease(mounted); CFRelease(whole); CFRelease(volume); CFRelease(mountURL);
    CFRelease(linked); CFRelease(linkURL); CFRelease(approvalSession);
    REQUIRE(unlink(linkPath) == 0);
    REQUIRE(rmdir(argv[2]) == 0);
    CFRelease(match); CFRelease(session);
    puts("DISK_ARBITRATION_IMAGE_SMOKE=PASS");
    return 0;
}
