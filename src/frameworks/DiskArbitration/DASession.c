#include "DAImageInternal.h"
#include <DiskArbitration/DiskArbitrationPrivate.h>
#include <CoreFoundation/CFRuntime.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdlib.h>
#include "../../external/darling-dmg/src/DAImageProtocol.h"
#include <fcntl.h>

/* Apple's session delivery depends on its Mach server. The sibling fork has
 * no producer. Poll the opened-image producer's actual snapshot here. Scheduled
 * delivery starts operation workers, leaving the executor available to answer
 * approvals; completion callbacks remain on that scheduled context. No disk
 * is inferred from an arbitrary host mount. */
enum { DA_APPEARED, DA_CHANGED, DA_DISAPPEARED, DA_UNMOUNT_APPROVAL, DA_IDLE };

typedef struct DACallback {
    unsigned long serial;
    int kind;
    void *function;
    void *context;
    CFDictionaryRef match;
    CFArrayRef watch;
    CFMutableSetRef seen;
    CFIndex cursor;
    CFArrayRef deliveryKeys;
    struct DACallback *next;
} DACallback;

typedef struct DAOperation {
    CFStringRef identity;
    unsigned operation, options;
    CFURLRef path;
    CFArrayRef arguments;
    DADiskMountCallback callback;
    void *context;
    DAReturn status;
    Boolean executed;
    CFStringRef message;
    struct DAOperation *next;
    DASessionRef session;
} DAOperation;

typedef struct DASchedule {
    CFRunLoopRef loop;
    CFStringRef mode;
    CFRunLoopTimerRef timer;
    struct DASchedule *next;
} DASchedule;

struct __DASession {
    CFRuntimeBase base;
    pthread_mutex_t lock;
    DACallback *callbacks;
    unsigned long nextSerial;
    unsigned long dispatchGeneration;
    DAOperation *operations, *lastOperation;
    DASchedule *schedules;
    dispatch_source_t dispatchTimer;
    CFDictionaryRef previous;
    DACallback *pendingCallbacks;
    CFDictionaryRef pendingPrevious, pendingCurrent;
    Boolean delivering;
    int approvalListener;
    char approvalPath[256];
    Boolean operationRunning;
    int approvalClient;
    size_t approvalReceived;
    struct DAImageReply approvalState;
    time_t approvalDeadline;
    unsigned long approvalGeneration, approvalClientGeneration;
    int ownerPID;
    struct __DASession *cleanupNext;
};

static CFTypeID sessionType;
static void DAPump(DASessionRef session);
static void DAUpdateApprovalEndpoint(DASessionRef session);
static pthread_mutex_t cleanupLock = PTHREAD_MUTEX_INITIALIZER;
static DASessionRef cleanupSessions;

static void DACleanupApprovalEndpoints(void)
{
    int owner = DAImageNativePID();
    pthread_mutex_lock(&cleanupLock);
    for (DASessionRef session = cleanupSessions; session; session = session->cleanupNext)
        if (session->ownerPID == owner && session->approvalPath[0])
            unlink(session->approvalPath);
    pthread_mutex_unlock(&cleanupLock);
}

static void DAFreeCallback(DACallback *entry)
{
    if (entry->match) CFRelease(entry->match);
    if (entry->watch) CFRelease(entry->watch);
    if (entry->seen) CFRelease(entry->seen);
    if (entry->deliveryKeys) CFRelease(entry->deliveryKeys);
    free(entry);
}

static void DAFreeOperation(DAOperation *operation)
{
    CFRelease(operation->identity);
    if (operation->path) CFRelease(operation->path);
    if (operation->arguments) CFRelease(operation->arguments);
    if (operation->message) CFRelease(operation->message);
    free(operation);
}

static void DASessionFinalize(CFTypeRef object)
{
    DASessionRef session = (DASessionRef)object;
    /* Scheduled contexts retain the session until explicitly unscheduled. */
    pthread_mutex_lock(&cleanupLock);
    DASessionRef *link = &cleanupSessions;
    while (*link && *link != session) link = &(*link)->cleanupNext;
    if (*link) *link = session->cleanupNext;
    if (session->approvalListener >= 0) close(session->approvalListener);
    if (session->approvalPath[0] && session->ownerPID == DAImageNativePID()) unlink(session->approvalPath);
    if (session->approvalClient >= 0) close(session->approvalClient);
    pthread_mutex_unlock(&cleanupLock);
    DACallback *entry = session->callbacks;
    while (entry) { DACallback *next = entry->next; DAFreeCallback(entry); entry = next; }
    DAOperation *operation = session->operations;
    while (operation) { DAOperation *next = operation->next; DAFreeOperation(operation); operation = next; }
    entry = session->pendingCallbacks;
    while (entry) { DACallback *next = entry->next; DAFreeCallback(entry); entry = next; }
    if (session->pendingPrevious) CFRelease(session->pendingPrevious);
    if (session->pendingCurrent) CFRelease(session->pendingCurrent);
    if (session->previous) CFRelease(session->previous);
    pthread_mutex_destroy(&session->lock);
}

static const CFRuntimeClass sessionClass = {
    0, "DASession", NULL, NULL, DASessionFinalize, NULL, NULL, NULL, NULL
};

__attribute__((constructor)) static void DASessionInitialize(void)
{
    sessionType = _CFRuntimeRegisterClass(&sessionClass);
    atexit(DACleanupApprovalEndpoints);
}

CFTypeID DASessionGetTypeID(void) { return sessionType; }
DASessionRef DASessionCreate(CFAllocatorRef allocator)
{
    DASessionRef session = (DASessionRef)_CFRuntimeCreateInstance(allocator, sessionType,
        sizeof(*session) - sizeof(CFRuntimeBase), NULL);
    if (!session) return NULL;
    session->callbacks = NULL;
    session->operations = session->lastOperation = NULL;
    session->schedules = NULL;
    session->dispatchTimer = NULL;
    session->previous = NULL;
    session->pendingCallbacks = NULL;
    session->pendingPrevious = session->pendingCurrent = NULL;
    session->nextSerial = 0;
    session->dispatchGeneration = 0;
    session->delivering = false;
    session->approvalListener = -1;
    session->approvalPath[0] = 0;
    session->operationRunning = false;
    session->approvalClient = -1;
    session->approvalReceived = 0;
    session->approvalGeneration = session->approvalClientGeneration = 0;
    session->ownerPID = DAImageNativePID();
    session->cleanupNext = NULL;
    pthread_mutex_init(&session->lock, NULL);
    session->previous = CFDictionaryCreate(NULL, NULL, NULL, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!session->previous) { CFRelease(session); return NULL; }
    pthread_mutex_lock(&cleanupLock);
    session->cleanupNext = cleanupSessions;
    cleanupSessions = session;
    pthread_mutex_unlock(&cleanupLock);
    return session;
}

static Boolean DAIsScheduled(DASessionRef session)
{
    pthread_mutex_lock(&session->lock);
    Boolean scheduled = session->schedules || session->dispatchTimer;
    pthread_mutex_unlock(&session->lock);
    return scheduled;
}

static void DAAddCallback(DASessionRef session, int kind, CFDictionaryRef match,
    CFArrayRef watch, void *function, void *context)
{
    if (!session || !function) return;
    DACallback *entry = calloc(1, sizeof(*entry));
    if (!entry) return;
    entry->kind = kind;
    entry->function = function;
    entry->context = context;
    entry->match = match ? CFDictionaryCreateCopy(NULL, match) : NULL;
    entry->watch = watch ? CFArrayCreateCopy(NULL, watch) : NULL;
    entry->seen = CFSetCreateMutable(NULL, 0, &kCFTypeSetCallBacks);
    if (!entry->seen || (match && !entry->match) || (watch && !entry->watch)) {
        DAFreeCallback(entry); return;
    }
    pthread_mutex_lock(&session->lock);
    entry->serial = ++session->nextSerial;
    entry->next = session->callbacks;
    session->callbacks = entry;
    DAUpdateApprovalEndpoint(session);
    pthread_mutex_unlock(&session->lock);
}

static void DARemoveCallback(DASessionRef session, void *function, void *context,
    Boolean approvalsOnly)
{
    if (!session) return;
    pthread_mutex_lock(&session->lock);
    DACallback **link = &session->callbacks;
    while (*link) {
        DACallback *entry = *link;
        if (entry->function == function && entry->context == context &&
            (!approvalsOnly || entry->kind == DA_UNMOUNT_APPROVAL)) {
            *link = entry->next;
            DAFreeCallback(entry);
        } else link = &entry->next;
    }
    DAUpdateApprovalEndpoint(session);
    pthread_mutex_unlock(&session->lock);
}

void DARegisterDiskAppearedCallback(DASessionRef s, CFDictionaryRef m, DADiskAppearedCallback f, void *c)
{ DAAddCallback(s, DA_APPEARED, m, NULL, (void *)f, c); }
void DARegisterDiskDescriptionChangedCallback(DASessionRef s, CFDictionaryRef m, CFArrayRef w,
    DADiskDescriptionChangedCallback f, void *c)
{ DAAddCallback(s, DA_CHANGED, m, w, (void *)f, c); }
void DARegisterDiskDisappearedCallback(DASessionRef s, CFDictionaryRef m, DADiskDisappearedCallback f, void *c)
{ DAAddCallback(s, DA_DISAPPEARED, m, NULL, (void *)f, c); }
void DARegisterDiskUnmountApprovalCallback(DASessionRef s, CFDictionaryRef m,
    DADiskUnmountApprovalCallback f, void *c)
{ DAAddCallback(s, DA_UNMOUNT_APPROVAL, m, NULL, (void *)f, c); }
void DARegisterIdleCallback(DASessionRef s, DAIdleCallback f, void *c)
{ DAAddCallback(s, DA_IDLE, NULL, NULL, (void *)f, c); }
void DAUnregisterCallback(DASessionRef s, void *f, void *c)
{ DARemoveCallback(s, f, c, false); }
void DAUnregisterApprovalCallback(DASessionRef s, void *f, void *c)
{ DARemoveCallback(s, f, c, true); }

void _DASessionQueueOperation(DASessionRef session, DADiskRef disk, unsigned operation,
    CFURLRef path, unsigned options, DADiskMountCallback callback, void *context,
    CFArrayRef arguments)
{
    DAOperation *entry = calloc(1, sizeof(*entry));
    if (!entry) return;
    entry->identity = CFStringCreateWithCString(NULL, _DADiskGetID(disk), kCFStringEncodingUTF8);
    if (!entry->identity) { free(entry); return; }
    entry->operation = operation;
    entry->options = options;
    entry->path = path ? (CFURLRef)CFRetain(path) : NULL;
    entry->arguments = arguments ? CFArrayCreateCopy(NULL, arguments) : NULL;
    if (arguments && !entry->arguments) {
        entry->status = kDAReturnNoResources;
        entry->executed = true;
    }
    entry->callback = callback;
    entry->context = context;
    pthread_mutex_lock(&session->lock);
    if (session->lastOperation) session->lastOperation->next = entry;
    else session->operations = entry;
    session->lastOperation = entry;
    pthread_mutex_unlock(&session->lock);
}

typedef struct {
    CFDictionaryRef description;
    Boolean matches;
} DAMatchContext;
static void DAMatchValue(const void *key, const void *expected, void *opaque)
{
    DAMatchContext *context = opaque;
    CFTypeRef value = CFDictionaryGetValue(context->description, key);
    if (!value || !CFEqual(value, expected)) context->matches = false;
}
static Boolean DAMatches(CFDictionaryRef match, CFDictionaryRef description)
{
    if (!match) return true;
    DAMatchContext context = { description, true };
    CFDictionaryApplyFunction(match, DAMatchValue, &context);
    return context.matches;
}

static CFArrayRef DAChangedKeys(CFDictionaryRef previous, CFDictionaryRef current)
{
    CFMutableArrayRef changed = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    if (!changed) return NULL;
    CFDictionaryRef dictionaries[] = { previous, current };
    for (int d = 0; d < 2; ++d) {
        CFIndex count = CFDictionaryGetCount(dictionaries[d]);
        const void **keys = malloc((size_t)count * sizeof(void *));
        if (count && !keys) { CFRelease(changed); return NULL; }
        CFDictionaryGetKeysAndValues(dictionaries[d], keys, NULL);
        for (CFIndex i = 0; i < count; ++i) {
            CFTypeRef a = CFDictionaryGetValue(previous, keys[i]);
            CFTypeRef b = CFDictionaryGetValue(current, keys[i]);
            if ((!a || !b || !CFEqual(a, b)) &&
                !CFArrayContainsValue(changed, CFRangeMake(0, CFArrayGetCount(changed)), keys[i]))
                CFArrayAppendValue(changed, keys[i]);
        }
        free(keys);
    }
    return changed;
}

/* Copy registrations, not pointers into the mutable list: callbacks may
 * unregister themselves, register new callbacks, or unschedule the session. */
static DACallback *DACopyCallbacks(DASessionRef session, Boolean *success)
{
    DACallback *copies = NULL;
    *success = true;
    pthread_mutex_lock(&session->lock);
    for (DACallback *entry = session->callbacks; entry; entry = entry->next) {
        DACallback *copy = malloc(sizeof(*copy));
        if (!copy) { *success = false; break; }
        *copy = *entry;
        copy->cursor = 0;
        copy->deliveryKeys = NULL;
        if (copy->match) CFRetain(copy->match);
        if (copy->watch) CFRetain(copy->watch);
        copy->seen = CFSetCreateMutableCopy(NULL, 0, entry->seen);
        if (!copy->seen) {
            if (copy->match) CFRelease(copy->match);
            if (copy->watch) CFRelease(copy->watch);
            free(copy);
            *success = false;
            break;
        }
        copy->next = copies;
        copies = copy;
    }
    pthread_mutex_unlock(&session->lock);
    if (!*success) {
        while (copies) { DACallback *next = copies->next; DAFreeCallback(copies); copies = next; }
    }
    return copies;
}

static Boolean DACallbackRegistered(DASessionRef session, unsigned long serial)
{
    Boolean registered = false;
    pthread_mutex_lock(&session->lock);
    for (DACallback *entry = session->callbacks; entry; entry = entry->next)
        if (entry->serial == serial) { registered = true; break; }
    pthread_mutex_unlock(&session->lock);
    return registered;
}

static void DACommitSeen(DASessionRef session, DACallback *entry)
{
    pthread_mutex_lock(&session->lock);
    for (DACallback *actual = session->callbacks; actual; actual = actual->next)
        if (actual->serial == entry->serial) {
            CFRelease(actual->seen);
            actual->seen = (CFMutableSetRef)CFRetain(entry->seen);
            break;
        }
    pthread_mutex_unlock(&session->lock);
}

/* A batch retains its immutable descriptions and each registration's cursor.
 * Unscheduling inside a callback cannot consume another registration's event,
 * and resuming the same batch never repeats an already delivered event. */
static Boolean DADeliverDictionary(DASessionRef session, DACallback *entry,
    CFDictionaryRef previous, CFDictionaryRef current)
{
    CFDictionaryRef dictionary = entry->kind == DA_DISAPPEARED ? previous : current;
    CFIndex count = CFDictionaryGetCount(dictionary);
    if (!entry->deliveryKeys) {
        const void **keys = malloc((size_t)count * sizeof(void *));
        if (count && !keys) return false;
        CFDictionaryGetKeysAndValues(dictionary, keys, NULL);
        entry->deliveryKeys = CFArrayCreate(NULL, keys, count, &kCFTypeArrayCallBacks);
        free(keys);
        if (!entry->deliveryKeys) return false;
    }
    Boolean complete = true;
    while (entry->cursor < count) {
        if (!DACallbackRegistered(session, entry->serial)) break;
        if (!DAIsScheduled(session)) { complete = false; break; }
        CFStringRef identity = CFArrayGetValueAtIndex(entry->deliveryKeys, entry->cursor);
        CFDictionaryRef description = CFDictionaryGetValue(dictionary, identity);
        if (!DAMatches(entry->match, description)) { ++entry->cursor; continue; }
        CFArrayRef changes = NULL;
        if (entry->kind == DA_APPEARED) {
            if (CFSetContainsValue(entry->seen, identity)) { ++entry->cursor; continue; }
        } else if (entry->kind == DA_DISAPPEARED) {
            if (CFDictionaryContainsKey(current, identity)) { ++entry->cursor; continue; }
        } else {
            CFDictionaryRef old = CFDictionaryGetValue(previous, identity);
            if (!old || CFEqual(old, description)) { ++entry->cursor; continue; }
            changes = DAChangedKeys(old, description);
            if (!changes) { complete = false; break; }
            Boolean watched = !entry->watch;
            for (CFIndex j = 0; !watched && j < CFArrayGetCount(changes); ++j)
                watched = CFArrayContainsValue(entry->watch, CFRangeMake(0, CFArrayGetCount(entry->watch)),
                    CFArrayGetValueAtIndex(changes, j));
            if (!watched) { CFRelease(changes); ++entry->cursor; continue; }
        }
        DADiskRef disk = _DADiskCreateImageSnapshot(session, identity, description);
        if (!disk) {
            if (changes) CFRelease(changes);
            complete = false;
            break;
        }
        ++entry->cursor;
        if (entry->kind == DA_APPEARED) CFSetAddValue(entry->seen, identity);
        DADiskRef outer = _DADiskSetCallbackDisk(disk);
        if (entry->kind == DA_APPEARED) ((DADiskAppearedCallback)entry->function)(disk, entry->context);
        else if (entry->kind == DA_DISAPPEARED) ((DADiskDisappearedCallback)entry->function)(disk, entry->context);
        else ((DADiskDescriptionChangedCallback)entry->function)(disk, changes, entry->context);
        _DADiskSetCallbackDisk(outer);
        CFRelease(disk);
        if (changes) CFRelease(changes);
    }
    if (complete && entry->kind == DA_APPEARED) {
        CFMutableSetRef live = CFSetCreateMutable(NULL, 0, &kCFTypeSetCallBacks);
        if (!live) return false;
        count = CFDictionaryGetCount(current);
        const void **keys = malloc((size_t)count * sizeof(void *));
        if (count && !keys) { CFRelease(live); return false; }
        CFDictionaryGetKeysAndValues(current, keys, NULL);
        for (CFIndex i = 0; i < count; ++i)
            if (CFSetContainsValue(entry->seen, keys[i])) CFSetAddValue(live, keys[i]);
        free(keys);
        CFRelease(entry->seen);
        entry->seen = live;
        DACommitSeen(session, entry);
    }
    return complete;
}

static Boolean DADrainPending(DASessionRef session)
{
    while (session->pendingCallbacks) {
        DACallback *entry = session->pendingCallbacks;
        if (DACallbackRegistered(session, entry->serial)) {
            if (!DAIsScheduled(session)) return false;
            if (entry->kind == DA_APPEARED || entry->kind == DA_CHANGED || entry->kind == DA_DISAPPEARED) {
                if (!DADeliverDictionary(session, entry, session->pendingPrevious, session->pendingCurrent))
                    return false;
            } else if (entry->kind == DA_IDLE && !CFSetContainsValue(entry->seen, CFSTR("idle"))) {
                CFSetAddValue(entry->seen, CFSTR("idle"));
                ((DAIdleCallback)entry->function)(entry->context);
                DACommitSeen(session, entry);
            }
        }
        session->pendingCallbacks = entry->next;
        DAFreeCallback(entry);
    }
    if (session->pendingPrevious) CFRelease(session->pendingPrevious);
    if (session->pendingCurrent) CFRelease(session->pendingCurrent);
    session->pendingPrevious = session->pendingCurrent = NULL;
    return true;
}

/* Called with the session lock held at every registration/scheduling change.
 * A published socket is a required voter, including when its executor stalls:
 * missing decisions fail closed at the producer rather than becoming consent. */
static void DAUpdateApprovalEndpointLocked(DASessionRef session)
{
    int owner = DAImageNativePID();
    if (session->ownerPID != owner) {
        /* A fork child must not consume or unlink its parent's voter. */
        if (session->approvalListener >= 0) close(session->approvalListener);
        session->approvalListener = -1;
        session->approvalPath[0] = 0;
        session->ownerPID = owner;
        ++session->approvalGeneration;
    }
    Boolean wanted = false;
    for (DACallback *entry = session->callbacks; entry; entry = entry->next)
        if (entry->kind == DA_UNMOUNT_APPROVAL) wanted = true;
    wanted = wanted && (session->schedules || session->dispatchTimer);
    if (!wanted) {
        ++session->approvalGeneration;
        if (session->approvalListener >= 0) close(session->approvalListener);
        session->approvalListener = -1;
        if (session->approvalPath[0]) unlink(session->approvalPath);
        session->approvalPath[0] = 0;
        return;
    }
    if (session->approvalPath[0]) return;
    char base[80];
    DAImageDirectory(base, sizeof(base));
    if (mkdir(base, 0700) != 0 && errno != EEXIST) return;
    if (DAImageCheckDirectory(base)) return;
    snprintf(session->approvalPath, sizeof(session->approvalPath), "%s/a-XXXXXX", base);
    /* Publish a fail-closed marker before allocating the listener, then replace
     * it atomically with the listening socket. Resource failure is not consent. */
    int marker = mkstemp(session->approvalPath);
    if (marker < 0) { session->approvalPath[0] = 0; return; }
    close(marker);
    char temporary[256];
    snprintf(temporary, sizeof(temporary), "%s/.%s", base, strrchr(session->approvalPath, '/') + 1);
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    if (strlen(temporary) >= sizeof(address.sun_path) ||
        DAImageCheckSocketPath(temporary)) return;
    strcpy(address.sun_path, temporary);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 ||
        bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(fd, 8) != 0 ||
        rename(temporary, session->approvalPath) != 0) {
        close(fd);
        unlink(temporary);
        return;
    }
    session->approvalListener = fd;
}

static void DAUpdateApprovalEndpoint(DASessionRef session)
{
    pthread_mutex_lock(&cleanupLock);
    DAUpdateApprovalEndpointLocked(session);
    pthread_mutex_unlock(&cleanupLock);
}

static void DADeliverApprovals(DASessionRef session)
{
    /* Read incrementally: an incomplete peer must never block this executor. */
    pthread_mutex_lock(&session->lock);
    Boolean stale = session->approvalClient >= 0 &&
        session->approvalClientGeneration != session->approvalGeneration;
    pthread_mutex_unlock(&session->lock);
    if (stale || (session->approvalClient >= 0 &&
        (DAImageNow() == (time_t)-1 || DAImageNow() >= session->approvalDeadline))) {
        close(session->approvalClient);
        session->approvalClient = -1;
    }
    if (session->approvalClient < 0) {
        pthread_mutex_lock(&session->lock);
        int accepted = session->approvalListener >= 0 ? accept(session->approvalListener, NULL, NULL) : -1;
        session->approvalClientGeneration = session->approvalGeneration;
        pthread_mutex_unlock(&session->lock);
        if (accepted < 0) return;
        if (DAImageAuthenticate(accepted) || fcntl(accepted, F_SETFD, FD_CLOEXEC) != 0) {
            close(accepted); return;
        }
        session->approvalClient = accepted;
        session->approvalReceived = 0;
        session->approvalDeadline = DAImageNow() + 15;
    }
    int fd = session->approvalClient;
    ssize_t received = recv(fd, (char *)&session->approvalState + session->approvalReceived,
        sizeof(session->approvalState) - session->approvalReceived, MSG_DONTWAIT);
    if (received > 0) session->approvalReceived += (size_t)received;
    else if (!received || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) ||
        DAImageNow() >= session->approvalDeadline) {
        close(fd); session->approvalClient = -1; return;
    }
    if (session->approvalReceived != sizeof(session->approvalState)) return;
    struct DAImageReply state = session->approvalState;
    struct DAImageDecision decision = { .version = DA_IMAGE_VERSION, .status = kDAReturnError };
    int error = 0;
    if (!error && state.version == DA_IMAGE_VERSION &&
        memchr(state.identity, 0, sizeof(state.identity)) && memchr(state.mount, 0, sizeof(state.mount))) {
        DADiskRef disk = _DAImageCreateApprovalDisk(session, &state);
        Boolean copied;
        DACallback *callbacks = DACopyCallbacks(session, &copied);
        DADiskRef outer = _DADiskSetCallbackDisk(disk);
        CFDictionaryRef description = disk ? DADiskCopyDescription(disk) : NULL;
        decision.status = disk && description && copied ? kDAReturnSuccess : kDAReturnNoResources;
        for (DACallback *entry = callbacks; entry && !decision.status; entry = entry->next) {
            if (!DAIsScheduled(session)) { decision.status = kDAReturnNotReady; break; }
            if (entry->kind != DA_UNMOUNT_APPROVAL || !DACallbackRegistered(session, entry->serial) ||
                !DAMatches(entry->match, description)) continue;
            DADissenterRef dissent = ((DADiskUnmountApprovalCallback)entry->function)(disk, entry->context);
            if (dissent) {
                decision.status = DADissenterGetStatus(dissent);
                CFStringRef reason = DADissenterGetStatusString(dissent);
                if (reason) {
                    CFIndex used = 0;
                    CFStringGetBytes(reason, CFRangeMake(0, CFStringGetLength(reason)), kCFStringEncodingUTF8,
                        '?', false, (UInt8 *)decision.reason, sizeof(decision.reason) - 1, &used);
                    decision.reason[used] = 0;
                }
                CFRelease(dissent);
            }
        }
        _DADiskSetCallbackDisk(outer);
        if (description) CFRelease(description);
        if (disk) CFRelease(disk);
        while (callbacks) { DACallback *next = callbacks->next; DAFreeCallback(callbacks); callbacks = next; }
    }
    /* Incomplete writes remain incomplete decisions, never implicit consent. */
    send(fd, &decision, sizeof(decision), MSG_DONTWAIT | MSG_NOSIGNAL);
    close(fd);
    session->approvalClient = -1;
}

static void *DAExecuteOperation(void *opaque)
{
    DAOperation *operation = opaque;
    DASessionRef session = operation->session;
    DADiskRef disk = _DADiskCreateImageSnapshot(session, operation->identity, NULL);
    operation->status = disk ? _DAImagePerformOperation(disk, operation->operation,
        operation->path, operation->options, operation->arguments, &operation->message) : kDAReturnNoResources;
    if (disk) CFRelease(disk);
    pthread_mutex_lock(&session->lock);
    operation->executed = true;
    operation->next = session->operations;
    session->operations = operation;
    if (!session->lastOperation) session->lastOperation = operation;
    session->operationRunning = false;
    pthread_mutex_unlock(&session->lock);
    CFRelease(session);
    return NULL;
}

static void DAPump(DASessionRef session)
{
    CFRetain(session);
    pthread_mutex_lock(&session->lock);
    if (session->ownerPID != DAImageNativePID() || session->delivering ||
        (!session->schedules && !session->dispatchTimer)) {
        pthread_mutex_unlock(&session->lock); CFRelease(session); return;
    }
    session->delivering = true;
    DAOperation *operations = session->operations;
    session->operations = session->lastOperation = NULL;
    Boolean operationRunning = session->operationRunning;
    pthread_mutex_unlock(&session->lock);
    Boolean ready = DADrainPending(session);
    DADeliverApprovals(session);

    DAOperation *finished = NULL, **end = &finished;
    if (ready && operations && DAIsScheduled(session)) {
        DAOperation *operation = operations;
        operations = operation->next;
        operation->next = NULL;
        if (!operation->executed && !operationRunning) {
            operation->session = (DASessionRef)CFRetain(session);
            pthread_t thread;
            pthread_mutex_lock(&session->lock);
            session->operationRunning = true;
            pthread_mutex_unlock(&session->lock);
            int error = pthread_create(&thread, NULL, DAExecuteOperation, operation);
            if (!error) {
                pthread_detach(thread);
                operation = NULL;
            } else {
                CFRelease(session);
                pthread_mutex_lock(&session->lock);
                session->operationRunning = false;
                pthread_mutex_unlock(&session->lock);
                operation->status = kDAReturnNoResources;
                operation->executed = true;
            }
        }
        if (operation) { *end = operation; end = &operation->next; }
    }

    CFArrayRef disks = ready && DAIsScheduled(session) ? _DAImageCopyDisks(session) : NULL;
    CFMutableDictionaryRef current = NULL;
    if (disks) {
        current = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if (current) for (CFIndex i = 0; i < CFArrayGetCount(disks); ++i) {
            DADiskRef disk = (DADiskRef)CFArrayGetValueAtIndex(disks, i);
            CFStringRef identity = CFStringCreateWithCString(NULL, _DADiskGetID(disk), kCFStringEncodingUTF8);
            DADiskRef outer = _DADiskSetCallbackDisk(disk);
            CFDictionaryRef description = DADiskCopyDescription(disk);
            _DADiskSetCallbackDisk(outer);
            if (identity && description) CFDictionarySetValue(current, identity, description);
            if (identity) CFRelease(identity);
            if (description) CFRelease(description);
            if (!identity || !description) { CFRelease(current); current = NULL; break; }
        }
        CFRelease(disks);
    }
    if (current) {
        Boolean copied;
        DACallback *callbacks = DACopyCallbacks(session, &copied);
        if (copied) {
            session->pendingCallbacks = callbacks;
            session->pendingPrevious = session->previous;
            session->pendingCurrent = (CFDictionaryRef)CFRetain(current);
            session->previous = current;
            DADrainPending(session);
        } else CFRelease(current);
    }

    while (finished && finished->executed && DAIsScheduled(session)) {
        DAOperation *operation = finished;
        DADissenterRef dissenter = operation->status ? DADissenterCreate(NULL, operation->status, operation->message) : NULL;
        DADiskRef disk = operation->callback ? _DADiskCreateImageSnapshot(session, operation->identity, NULL) : NULL;
        if (operation->callback && (!disk || (operation->status && !dissenter))) {
            if (disk) CFRelease(disk);
            if (dissenter) CFRelease(dissenter);
            break;
        }
        finished = operation->next;
        if (operation->callback) operation->callback(disk, dissenter, operation->context);
        if (disk) CFRelease(disk);
        if (dissenter) CFRelease(dissenter);
        DAFreeOperation(operation);
    }
    pthread_mutex_lock(&session->lock);
    /* Preserve not-yet-executed requests and completed callbacks on unschedule. */
    if (finished) {
        *end = operations;
        operations = finished;
    }
    if (operations) {
        DAOperation *last = operations;
        while (last->next) last = last->next;
        last->next = session->operations;
        session->operations = operations;
        if (!session->lastOperation) session->lastOperation = last;
    }
    session->delivering = false;
    pthread_mutex_unlock(&session->lock);
    CFRelease(session);
}

static void DATimer(CFRunLoopTimerRef timer, void *context) { DAPump(context); }
typedef struct {
    DASessionRef session;
    unsigned long generation;
} DADispatchContext;
static void DADispatchTimer(void *opaque)
{
    DADispatchContext *context = opaque;
    DASessionRef session = context->session;
    pthread_mutex_lock(&session->lock);
    Boolean current = session->dispatchTimer && session->dispatchGeneration == context->generation;
    pthread_mutex_unlock(&session->lock);
    if (current) DAPump(session);
}
static void DAReleaseContext(void *opaque)
{
    DADispatchContext *context = opaque;
    CFRelease(context->session);
    free(context);
}

void DASessionScheduleWithRunLoop(DASessionRef session, CFRunLoopRef loop, CFStringRef mode)
{
    if (!session || !loop || !mode) return;
    pthread_mutex_lock(&session->lock);
    for (DASchedule *entry = session->schedules; entry; entry = entry->next)
        if (entry->loop == loop && CFEqual(entry->mode, mode)) { pthread_mutex_unlock(&session->lock); return; }
    DASchedule *entry = calloc(1, sizeof(*entry));
    if (entry) {
        CFRunLoopTimerContext context = { 0, session, CFRetain, CFRelease, NULL };
        entry->timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent(), 0.1, 0, 0, DATimer, &context);
        if (!entry->timer) { free(entry); entry = NULL; }
        if (entry) {
            entry->mode = CFStringCreateCopy(NULL, mode);
            if (!entry->mode) {
                CFRunLoopTimerInvalidate(entry->timer);
                CFRelease(entry->timer);
                free(entry);
                entry = NULL;
            }
        }
    }
    if (entry) {
        entry->loop = (CFRunLoopRef)CFRetain(loop);
        entry->next = session->schedules;
        session->schedules = entry;
        CFRunLoopAddTimer(loop, entry->timer, mode);
        CFRunLoopWakeUp(loop);
    }
    DAUpdateApprovalEndpoint(session);
    pthread_mutex_unlock(&session->lock);
}

void DASessionUnscheduleFromRunLoop(DASessionRef session, CFRunLoopRef loop, CFStringRef mode)
{
    if (!session || !loop || !mode) return;
    CFRetain(session);
    pthread_mutex_lock(&session->lock);
    DASchedule **link = &session->schedules;
    while (*link) {
        DASchedule *entry = *link;
        if (entry->loop == loop && CFEqual(entry->mode, mode)) {
            *link = entry->next;
            CFRunLoopTimerInvalidate(entry->timer);
            CFRelease(entry->timer); CFRelease(entry->loop); CFRelease(entry->mode); free(entry);
            break;
        }
        link = &entry->next;
    }
    DAUpdateApprovalEndpoint(session);
    pthread_mutex_unlock(&session->lock);
    CFRelease(session);
}

void DASessionSetDispatchQueue(DASessionRef session, dispatch_queue_t queue)
{
    if (!session) return;
    CFRetain(session);
    pthread_mutex_lock(&session->lock);
    ++session->dispatchGeneration;
    if (session->dispatchTimer) {
        dispatch_source_cancel(session->dispatchTimer);
        dispatch_release(session->dispatchTimer);
        session->dispatchTimer = NULL;
    }
    if (queue) {
        DADispatchContext *context = malloc(sizeof(*context));
        dispatch_source_t timer = context ? dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue) : NULL;
        if (timer) {
            context->session = (DASessionRef)CFRetain(session);
            context->generation = session->dispatchGeneration;
            dispatch_set_context(timer, context);
            dispatch_source_set_event_handler_f(timer, DADispatchTimer);
            dispatch_source_set_cancel_handler_f(timer, DAReleaseContext);
            dispatch_source_set_timer(timer, DISPATCH_TIME_NOW, NSEC_PER_SEC / 10, NSEC_PER_SEC / 100);
            session->dispatchTimer = timer;
            dispatch_resume(timer);
        }
        else free(context);
    }
    DAUpdateApprovalEndpoint(session);
    pthread_mutex_unlock(&session->lock);
    CFRelease(session);
}

CFTypeID DAApprovalSessionGetTypeID(void) { return DASessionGetTypeID(); }
DAApprovalSessionRef DAApprovalSessionCreate(CFAllocatorRef allocator) { return DASessionCreate(allocator); }
void DAApprovalSessionScheduleWithRunLoop(DAApprovalSessionRef session, CFRunLoopRef loop, CFStringRef mode)
{ DASessionScheduleWithRunLoop(session, loop, mode); }
void DAApprovalSessionUnscheduleFromRunLoop(DAApprovalSessionRef session, CFRunLoopRef loop, CFStringRef mode)
{ DASessionUnscheduleFromRunLoop(session, loop, mode); }
