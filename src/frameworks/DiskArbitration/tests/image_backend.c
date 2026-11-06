/* Run only in an isolated guest lane with an owned HFS+ image. No host disks.
 * Arguments: image-path fresh-mountpoint relative-payload expected-payload-file.
 * Both fresh-mountpoint and fresh-mountpoint.link must not exist. The expected
 * file must be outside the image. The parent supplies the image using its
 * existing fixture producer; this program never formats a disk. */
#include <DiskArbitration/DiskArbitration.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <IOKit/IOKitLib.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/disk.h>

#define RAW_SAMPLE_BYTES 1026

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

static void requireDescriptionString(CFDictionaryRef description, CFStringRef key,
    const char *expected)
{
    CFTypeRef value = CFDictionaryGetValue(description, key);
    REQUIRE(value && CFGetTypeID(value) == CFStringGetTypeID());
    char buffer[256];
    REQUIRE(CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8));
    REQUIRE(strcmp(buffer, expected) == 0);
}

static uint64_t requireDescriptionNumber(CFDictionaryRef description, CFStringRef key)
{
    CFTypeRef value = CFDictionaryGetValue(description, key);
    REQUIRE(value && CFGetTypeID(value) == CFNumberGetTypeID());
    int64_t number = -1;
    REQUIRE(CFNumberGetValue(value, kCFNumberSInt64Type, &number));
    REQUIRE(number > 0);
    return (uint64_t)number;
}

static int openDeviceNode(const char *bsdName)
{
    char path[8192];
    int length = snprintf(path, sizeof(path), "/dev/%s", bsdName);
    REQUIRE(length > 0 && (size_t)length < sizeof(path));
    int fd = open(path, O_RDONLY);
    REQUIRE(fd >= 0);
    struct stat st;
    REQUIRE(fstat(fd, &st) == 0);
    REQUIRE(S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode));
    return fd;
}

static uint32_t inspectDevice(int fd, uint64_t expectedSize)
{
    uint32_t blockSize = 0, physicalBlockSize = 0, writable = 1;
    uint64_t blockCount = 0;
    REQUIRE(ioctl(fd, DKIOCGETBLOCKSIZE, &blockSize) == 0 && blockSize > 0);
    REQUIRE(ioctl(fd, DKIOCGETPHYSICALBLOCKSIZE, &physicalBlockSize) == 0);
    REQUIRE(physicalBlockSize == blockSize);
    REQUIRE(ioctl(fd, DKIOCGETBLOCKCOUNT, &blockCount) == 0);
    REQUIRE(expectedSize % blockSize == 0 && blockCount == expectedSize / blockSize);
    REQUIRE(ioctl(fd, DKIOCISWRITABLE, &writable) == 0 && writable == 0);
    errno = 0;
    REQUIRE(ioctl(fd, DKIOCGETBLOCKSIZE, NULL) != 0 && errno == EFAULT);
    return blockSize;
}

static void readDeviceSample(int fd, unsigned char *sample, Boolean requireHFS)
{
    REQUIRE(pread(fd, sample, RAW_SAMPLE_BYTES, 0) == RAW_SAMPLE_BYTES);
    if (requireHFS) REQUIRE(sample[1024] == 'H' && sample[1025] == '+');
}

static void verifyDeviceNamespace(const char *bsdName, int device,
    const unsigned char *sample)
{
    int directory = open("/dev", O_RDONLY | O_DIRECTORY);
    REQUIRE(directory >= 0);
    int relative = openat(directory, bsdName, O_RDONLY);
    REQUIRE(relative >= 0);
    struct stat original, status;
    REQUIRE(fstat(device, &original) == 0);
    REQUIRE(fstatat(directory, bsdName, &status, AT_SYMLINK_NOFOLLOW) == 0);
    REQUIRE(S_ISBLK(status.st_mode) && status.st_rdev == original.st_rdev &&
        status.st_size == original.st_size);
    unsigned char actual[RAW_SAMPLE_BYTES];
    REQUIRE(pread(relative, actual, sizeof(actual), 0) == sizeof(actual) &&
        memcmp(actual, sample, sizeof(actual)) == 0);
    close(relative);
    char rawName[128];
    REQUIRE(snprintf(rawName, sizeof(rawName), "r%s", bsdName) < (int)sizeof(rawName));
    int raw = openat(directory, rawName, O_RDONLY);
    REQUIRE(raw >= 0 && fstat(raw, &status) == 0 && S_ISCHR(status.st_mode));
    REQUIRE(pread(raw, actual, sizeof(actual), 0) == sizeof(actual) &&
        memcmp(actual, sample, sizeof(actual)) == 0);
    close(raw);
    int nullDevice = openat(directory, "null", O_RDONLY);
    int hostNull = open("/Volumes/SystemRoot/dev/null", O_RDONLY);
    REQUIRE(nullDevice >= 0 && hostNull >= 0);
    REQUIRE(fstat(nullDevice, &status) == 0 && fstat(hostNull, &original) == 0 &&
        S_ISCHR(status.st_mode) && status.st_rdev == original.st_rdev);
    REQUIRE(read(nullDevice, actual, sizeof(actual)) == 0);
    close(hostNull);
    close(nullDevice);
    close(directory);
    DIR *listing = opendir("/dev");
    REQUIRE(listing != NULL);
    int devices = 0, nulls = 0;
    struct dirent *entry;
    while ((entry = readdir(listing)) != NULL) {
        devices += strcmp(entry->d_name, bsdName) == 0;
        nulls += strcmp(entry->d_name, "null") == 0;
    }
    REQUIRE(closedir(listing) == 0 && devices == 1 && nulls == 1);
}

static void verifySeekAndDup(int fd, uint32_t blockSize,
    const unsigned char *sample)
{
    off_t offset = blockSize;
    unsigned char expected[RAW_SAMPLE_BYTES], actual[RAW_SAMPLE_BYTES];
    REQUIRE(pread(fd, expected, sizeof(expected), offset) == (ssize_t)sizeof(expected));
    REQUIRE(lseek(fd, offset, SEEK_SET) == offset);
    REQUIRE(read(fd, actual, sizeof(actual)) == (ssize_t)sizeof(actual));
    REQUIRE(memcmp(expected, actual, sizeof(expected)) == 0);
    int duplicated = dup(fd);
    REQUIRE(duplicated >= 0);
    REQUIRE(pread(duplicated, actual, sizeof(actual), offset) == (ssize_t)sizeof(actual));
    REQUIRE(memcmp(expected, actual, sizeof(expected)) == 0);
    close(duplicated);
    REQUIRE(lseek(fd, 0, SEEK_SET) == 0);
    REQUIRE(read(fd, actual, RAW_SAMPLE_BYTES) == RAW_SAMPLE_BYTES);
    REQUIRE(memcmp(actual, sample, RAW_SAMPLE_BYTES) == 0);
}

static void verifyFork(int fd, const unsigned char *sample)
{
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);
    fflush(NULL);
    pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        close(pipefd[0]);
        unsigned char bytes[RAW_SAMPLE_BYTES];
        ssize_t got = pread(fd, bytes, sizeof(bytes), 0);
        if (got != (ssize_t)sizeof(bytes)) _exit(1);
        if (write(pipefd[1], bytes, sizeof(bytes)) != (ssize_t)sizeof(bytes)) _exit(1);
        _exit(0);
    }
    close(pipefd[1]);
    unsigned char child[RAW_SAMPLE_BYTES];
    size_t received = 0;
    while (received < sizeof(child)) {
        ssize_t got = read(pipefd[0], child + received, sizeof(child) - received);
        if (got <= 0) break;
        received += (size_t)got;
    }
    close(pipefd[0]);
    int status;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    REQUIRE(received == sizeof(child) && memcmp(child, sample, sizeof(child)) == 0);
}

static void verifyExec(int fd, const unsigned char *sample, const char *scratch)
{
    char self[4096];
    uint32_t selfSize = sizeof(self);
    REQUIRE(_NSGetExecutablePath(self, &selfSize) == 0);
    char descriptor[32];
    REQUIRE(snprintf(descriptor, sizeof(descriptor), "%d", fd) < (int)sizeof(descriptor));
    /* Prove the descriptor survives exec; the child only performs pread. */
    REQUIRE(fcntl(fd, F_SETFD, 0) == 0);
    unlink(scratch);
    fflush(NULL);
    pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        execl(self, self, "--raw-device-child", descriptor, scratch, (char *)NULL);
        _exit(127);
    }
    int status;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    FILE *file = fopen(scratch, "rb");
    REQUIRE(file != NULL);
    unsigned char child[RAW_SAMPLE_BYTES];
    REQUIRE(fread(child, 1, sizeof(child), file) == sizeof(child));
    fclose(file);
    REQUIRE(unlink(scratch) == 0);
    REQUIRE(memcmp(child, sample, sizeof(child)) == 0);
}

static void verifyEjectedDevice(int fd, const char *bsdName)
{
    unsigned char bytes[32];
    errno = 0;
    REQUIRE(pread(fd, bytes, sizeof(bytes), 0) < 0);
    uint64_t blocks = 0;
    errno = 0;
    REQUIRE(ioctl(fd, DKIOCGETBLOCKCOUNT, &blocks) != 0 && errno == ENXIO);
    char path[8192];
    int length = snprintf(path, sizeof(path), "/dev/%s", bsdName);
    REQUIRE(length > 0 && (size_t)length < sizeof(path));
    errno = 0;
    int reopened = open(path, O_RDONLY);
    REQUIRE(reopened < 0);
    REQUIRE(errno == ENOENT || errno == ENODEV || errno == ENXIO || errno == ENOTCONN);
}



int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "--raw-device-child")) {
        /* A post-exec process reads the inherited data descriptor only. */
        int fd = atoi(argv[2]);
        unsigned char bytes[RAW_SAMPLE_BYTES];
        ssize_t got = pread(fd, bytes, sizeof(bytes), 0);
        REQUIRE(got == (ssize_t)sizeof(bytes));
        int out = open(argv[3], O_WRONLY | O_CREAT | O_EXCL, 0600);
        REQUIRE(out >= 0);
        REQUIRE(write(out, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes));
        close(out);
        return 0;
    }
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
    REQUIRE(volume != NULL);
    const char *bsdName = DADiskGetBSDName(volume);
    REQUIRE(bsdName && bsdName[0]);
    char selectedBSD[128];
    REQUIRE(strlen(bsdName) < sizeof(selectedBSD));
    strcpy(selectedBSD, bsdName);
    CFDictionaryRef description = DADiskCopyDescription(volume);
    REQUIRE(description && !CFDictionaryContainsKey(description, kDADiskDescriptionVolumePathKey));
    requireDescriptionString(description, kDADiskDescriptionMediaBSDNameKey, selectedBSD);
    REQUIRE(CFDictionaryGetValue(description, kDADiskDescriptionMediaWritableKey) == kCFBooleanFalse);
    REQUIRE(CFDictionaryGetValue(description, kDADiskDescriptionMediaEjectableKey) == kCFBooleanTrue);
    REQUIRE(CFDictionaryGetValue(description, kDADiskDescriptionVolumeMountableKey) == kCFBooleanTrue);
    uint64_t selectedSize = requireDescriptionNumber(description, kDADiskDescriptionMediaSizeKey);
    uint64_t blockSize = requireDescriptionNumber(description, kDADiskDescriptionMediaBlockSizeKey);
    CFTypeRef content = CFDictionaryGetValue(description, kDADiskDescriptionMediaContentKey);
    REQUIRE(content && CFGetTypeID(content) == CFStringGetTypeID());
    CFTypeRef filesystem = CFDictionaryGetValue(description, kDADiskDescriptionVolumeKindKey);
    REQUIRE(filesystem && CFGetTypeID(filesystem) == CFStringGetTypeID());
    requireDescriptionString(description, kDADiskDescriptionVolumeTypeKey, "HFS");
    CFRelease(description);

    io_service_t selectedMedia = DADiskCopyIOMedia(volume);
    REQUIRE(selectedMedia != MACH_PORT_NULL);
    CFTypeRef mediaBSDName = IORegistryEntryCreateCFProperty(selectedMedia, CFSTR("BSD Name"), NULL, 0);
    REQUIRE(mediaBSDName && CFGetTypeID(mediaBSDName) == CFStringGetTypeID());
    char mediaName[128];
    REQUIRE(CFStringGetCString(mediaBSDName, mediaName, sizeof(mediaName), kCFStringEncodingUTF8));
    CFRelease(mediaBSDName);
    REQUIRE(strcmp(mediaName, selectedBSD) == 0);
    uint64_t selectedRegistryID = 0;
    REQUIRE(IORegistryEntryGetRegistryEntryID(selectedMedia, &selectedRegistryID) == KERN_SUCCESS);
    REQUIRE(selectedRegistryID != 0);
    DADiskRef fromMedia = DADiskCreateFromIOMedia(NULL, session, selectedMedia);
    REQUIRE(fromMedia != NULL && CFEqual(fromMedia, volume));
    CFDictionaryRef mediaDescription = DADiskCopyDescription(fromMedia);
    REQUIRE(mediaDescription != NULL);
    requireDescriptionString(mediaDescription, kDADiskDescriptionMediaBSDNameKey, selectedBSD);
    REQUIRE(requireDescriptionNumber(mediaDescription, kDADiskDescriptionMediaSizeKey) == selectedSize);
    CFRelease(mediaDescription);
    CFRelease(fromMedia);
    IOObjectRelease(selectedMedia);

    int selectedDevice = openDeviceNode(selectedBSD);
    uint32_t selectedBlockSize = inspectDevice(selectedDevice, selectedSize);
    REQUIRE(selectedBlockSize == blockSize);
    unsigned char selectedSample[RAW_SAMPLE_BYTES];
    readDeviceSample(selectedDevice, selectedSample, true);
    verifyDeviceNamespace(selectedBSD, selectedDevice, selectedSample);
    verifySeekAndDup(selectedDevice, selectedBlockSize, selectedSample);

    DADiskRef whole = DADiskCopyWholeDisk(volume);
    REQUIRE(whole != NULL);
    description = DADiskCopyDescription(whole);
    REQUIRE(description && CFDictionaryGetValue(description, kDADiskDescriptionMediaWholeKey) == kCFBooleanTrue);
    CFTypeRef wholeName = CFDictionaryGetValue(description, kDADiskDescriptionMediaBSDNameKey);
    REQUIRE(wholeName && CFGetTypeID(wholeName) == CFStringGetTypeID());
    char wholeBSD[128];
    REQUIRE(CFStringGetCString(wholeName, wholeBSD, sizeof(wholeBSD), kCFStringEncodingUTF8));
    uint64_t wholeSize = requireDescriptionNumber(description, kDADiskDescriptionMediaSizeKey);
    Boolean wholeLeaf = CFDictionaryGetValue(description,
        kDADiskDescriptionMediaLeafKey) == kCFBooleanTrue;
    CFRelease(description);
    int wholeDevice = openDeviceNode(wholeBSD);
    inspectDevice(wholeDevice, wholeSize);
    unsigned char wholeSample[RAW_SAMPLE_BYTES];
    readDeviceSample(wholeDevice, wholeSample, wholeLeaf);
    if (wholeLeaf) REQUIRE(memcmp(wholeSample, selectedSample, RAW_SAMPLE_BYTES) == 0);
    verifyFork(selectedDevice, selectedSample);
    char rawScratch[8192];
    int scratchLength = snprintf(rawScratch, sizeof(rawScratch), "%s.rawchild", argv[2]);
    REQUIRE(scratchLength > 0 && (size_t)scratchLength < sizeof(rawScratch));
    verifyExec(selectedDevice, selectedSample, rawScratch);
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
    DADiskUnmount(mounted, 0, onOperation, NULL);
    runUntil(&completions, 5);
    REQUIRE(completionStatus == kDAReturnNotPermitted && approvals == 3 && disappeared == 0);
    compareFiles(payload, argv[4]);
    vetoEject = false;
    DADiskUnmount(mounted, kDADiskUnmountOptionForce, onOperation, NULL);
    runUntil(&completions, 6);
    REQUIRE(completionStatus == kDAReturnUnsupported && approvals == 3);
    compareFiles(payload, argv[4]);
    DADiskUnmount(whole, kDADiskUnmountOptionWhole, onOperation, NULL);
    runUntil(&completions, 7);
    REQUIRE(completionStatus == 0 && approvals == 4 && disappeared == 0);
    REQUIRE(DADiskCreateFromVolumePath(NULL, session, mountURL) == NULL);
    selectedMedia = DADiskCopyIOMedia(volume);
    REQUIRE(selectedMedia != MACH_PORT_NULL);
    IOObjectRelease(selectedMedia);
    REQUIRE(lstat(payload, &st) != 0 && errno == ENOENT);
    description = DADiskCopyDescription(volume);
    REQUIRE(description && !CFDictionaryContainsKey(description, kDADiskDescriptionVolumePathKey));
    REQUIRE(CFDictionaryGetValue(description, kDADiskDescriptionVolumeMountableKey) == kCFBooleanTrue);
    CFRelease(description);
    DADiskUnmount(volume, 0, onOperation, NULL);
    runUntil(&completions, 8);
    REQUIRE(completionStatus == kDAReturnNotMounted && approvals == 4 && disappeared == 0);
    DADiskMountWithArguments(volume, linkURL, 0, onOperation, NULL, readOnly);
    runUntil(&completions, 9);
    REQUIRE(completionStatus == 0 && changed == 3 && disappeared == 0);
    compareFiles(payload, argv[4]);
    compareFiles(linkedPayload, argv[4]);
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
    REQUIRE(completions == 10 && completionStatus == 0 && disappeared == 1 && approvals == 5);
    REQUIRE(DADiskCreateFromVolumePath(NULL, session, mountURL) == NULL);
    REQUIRE(DADiskCreateFromVolumePath(NULL, session, linkURL) == NULL);
    REQUIRE(lstat(payload, &st) != 0);
    REQUIRE(DADiskCopyDescription(mounted) == NULL);
    REQUIRE(DADiskCopyDescription(volume) == NULL);
    REQUIRE(DADiskCopyDescription(linked) == NULL);
    REQUIRE(DADiskCopyIOMedia(volume) == MACH_PORT_NULL);
    REQUIRE(DADiskCopyIOMedia(whole) == MACH_PORT_NULL);
    verifyEjectedDevice(selectedDevice, selectedBSD);
    verifyEjectedDevice(wholeDevice, wholeBSD);
    REQUIRE(close(selectedDevice) == 0);
    REQUIRE(close(wholeDevice) == 0);
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
