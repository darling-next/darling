/*
 * AppServerSupport over the existing Darling launchd.
 *
 * The measured consumer is CoreDeviceUtilities: it asks for OSLaunchdDomain
 * (+systemDomain / +domainForRoleAccountUser:), creates an OSLaunchdJob from a
 * launchd plist, submits and starts it, and monitors the returned
 * OSLaunchdJobInfo until the job exits. Those operations are implemented here
 * with ServiceManagement's existing user-launchd client for submission/removal
 * and liblaunch's existing caller-UID connection for job lookup. No job state
 * is invented: every reported state, pid, exit status and error comes from the
 * loaded launchd job dictionary or from a real request failure.
 *
 * The native binary is unavailable: the Xcode SDK ships only AppServerSupport.tbd,
 * the vendored/current Darling and Vibe trees have no implementation, and no
 * binary exists in the target or prefix. This file therefore supplies the
 * measured client surface, not a copy of Apple's implementation.
 */

#import <AppServerSupport/AppServerSupport.h>

#import <ServiceManagement/ServiceManagement.h>
#include <launch.h>
#include <launch_priv.h>

#include <Block.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static NSError *ASPOSIXError(int code, NSString *description)
{
    NSDictionary *userInfo = description ? @{ NSLocalizedDescriptionKey: description } : nil;
    return [NSError errorWithDomain:NSPOSIXErrorDomain code:code userInfo:userInfo];
}

static void ASSetError(NSError **error, NSError *value)
{
    if (error) {
        *error = value;
    }
}

static NSError *ASCFError(CFErrorRef error)
{
    if (!error) {
        return nil;
    }
    /* CFError out-parameters are +1. -autorelease transfers that reference to
     * the caller's pool; releasing it here as well would deallocate it before
     * the caller can read it. */
    return [(NSError *)error autorelease];
}

static NSString *ASStringFromLaunchData(launch_data_t dictionary, const char *key)
{
    launch_data_t value = launch_data_dict_lookup(dictionary, key);
    if (!value || launch_data_get_type(value) != LAUNCH_DATA_STRING) {
        return nil;
    }
    const char *string = launch_data_get_string(value);
    return string ? [NSString stringWithUTF8String:string] : nil;
}

static BOOL ASGetIntegerFromLaunchData(launch_data_t dictionary, const char *key, long long *value)
{
    launch_data_t data = launch_data_dict_lookup(dictionary, key);
    if (!data || launch_data_get_type(data) != LAUNCH_DATA_INTEGER) {
        return NO;
    }
    *value = launch_data_get_integer(data);
    return YES;
}

static BOOL ASGetBoolFromLaunchData(launch_data_t dictionary, const char *key)
{
    launch_data_t data = launch_data_dict_lookup(dictionary, key);
    return data && launch_data_get_type(data) == LAUNCH_DATA_BOOL && launch_data_get_bool(data);
}

/* Returns an owned launch_data dictionary, or NULL with *error set. */
static launch_data_t ASGetJobData(NSString *label, int *error)
{
    if (!label.length || !label.UTF8String) {
        if (error) *error = EINVAL;
        return NULL;
    }
    launch_data_t request = launch_data_alloc(LAUNCH_DATA_DICTIONARY);
    if (!request) {
        if (error) *error = ENOMEM;
        return NULL;
    }
    launch_data_t value = launch_data_new_string(label.UTF8String);
    if (!value || !launch_data_dict_insert(request, value, LAUNCH_KEY_GETJOB)) {
        if (value) launch_data_free(value);
        launch_data_free(request);
        if (error) *error = ENOMEM;
        return NULL;
    }

    errno = 0;
    launch_data_t reply = _launch_msg_user(request);
    int transportError = errno;
    launch_data_free(request);
    if (!reply) {
        if (error) *error = transportError ? transportError : EIO;
        return NULL;
    }
    if (launch_data_get_type(reply) == LAUNCH_DATA_DICTIONARY) {
        if (error) *error = 0;
        return reply;
    }
    if (launch_data_get_type(reply) == LAUNCH_DATA_ERRNO) {
        int launchdError = launch_data_get_errno(reply);
        launch_data_free(reply);
        if (error) *error = launchdError ? launchdError : EPROTO;
        return NULL;
    }
    launch_data_free(reply);
    if (error) *error = EPROTO;
    return NULL;
}

/* Returns YES only when launchd accepted a real StartJob request for `label`. */
static BOOL ASStartJob(NSString *label, NSError **error)
{
    if (!label.length || !label.UTF8String) {
        ASSetError(error, ASPOSIXError(EINVAL, @"The launchd job has no valid label."));
        return NO;
    }
    launch_data_t request = launch_data_alloc(LAUNCH_DATA_DICTIONARY);
    if (!request) {
        ASSetError(error, ASPOSIXError(ENOMEM, @"Could not allocate the StartJob request."));
        return NO;
    }
    launch_data_t value = launch_data_new_string(label.UTF8String);
    if (!value || !launch_data_dict_insert(request, value, LAUNCH_KEY_STARTJOB)) {
        if (value) launch_data_free(value);
        launch_data_free(request);
        ASSetError(error, ASPOSIXError(ENOMEM, @"Could not build the StartJob request."));
        return NO;
    }

    errno = 0;
    launch_data_t reply = _launch_msg_user(request);
    int transportError = errno;
    launch_data_free(request);
    if (!reply) {
        ASSetError(error, ASPOSIXError(transportError ? transportError : EIO,
            @"The StartJob request could not be delivered."));
        return NO;
    }
    if (launch_data_get_type(reply) != LAUNCH_DATA_ERRNO) {
        launch_data_free(reply);
        ASSetError(error, ASPOSIXError(EPROTO,
            @"The launchd StartJob reply has an unexpected type."));
        return NO;
    }
    int launchdError = launch_data_get_errno(reply);
    launch_data_free(reply);
    if (launchdError) {
        ASSetError(error, ASPOSIXError(launchdError,
            @"launchd rejected the StartJob request."));
        return NO;
    }
    return YES;
}

static CFStringRef ASSubmitDomain(OSLaunchdDomain *domain)
{
    return domain.type == OSLaunchdDomainTypeSystem ? kSMDomainSystemLaunchd : kSMDomainUserLaunchd;
}

static BOOL ASDomainAllowsSubmission(OSLaunchdDomain *domain, NSError **error)
{
    if (!domain) {
        ASSetError(error, ASPOSIXError(EINVAL, @"No launchd domain was supplied."));
        return NO;
    }
    if (domain.type == OSLaunchdDomainTypeSystem) {
        /*
         * Submission uses the system ServiceManagement broker, while StartJob
         * and GetJob have no system-domain launchd route in this client. Do not
         * submit into one domain and then start/query another; refuse before
         * submission until a domain-correct system path exists.
         */
        ASSetError(error, ASPOSIXError(ENOTSUP,
            @"This launchd client cannot start or query system-domain jobs."));
        return NO;
    }
    if (domain.type == OSLaunchdDomainTypeRoleAccount && domain.handle != (uint64_t)geteuid()) {
        ASSetError(error, ASPOSIXError(EPERM,
            @"This launchd client cannot submit into another role account's domain."));
        return NO;
    }
    if (domain.type == OSLaunchdDomainTypeUser && domain.handle != (uint64_t)geteuid()) {
        ASSetError(error, ASPOSIXError(EPERM,
            @"The user launchd domain belongs to another UID; this client cannot route there."));
        return NO;
    }
    if (domain.type == OSLaunchdDomainTypePid) {
        ASSetError(error, ASPOSIXError(ENOTSUP,
            @"A per-PID launchd domain is not a submission domain."));
        return NO;
    }
    if (domain.type != OSLaunchdDomainTypeRoleAccount &&
        domain.type != OSLaunchdDomainTypeUser) {
        ASSetError(error, ASPOSIXError(EINVAL, @"The launchd domain type is not a submission domain."));
        return NO;
    }
    return YES;
}

@implementation OSLaunchdDomain

@synthesize type = _type;
@synthesize handle = _handle;

+ (instancetype)systemDomain
{
    return [[[self alloc] initWithType:OSLaunchdDomainTypeSystem handle:0] autorelease];
}

+ (instancetype)currentDomain
{
    return [[[self alloc] initWithType:OSLaunchdDomainTypeRoleAccount handle:(uint64_t)geteuid()] autorelease];
}

+ (instancetype)domainForUser:(uid_t)uid
{
    return [[[self alloc] initWithType:OSLaunchdDomainTypeUser handle:(uint64_t)uid] autorelease];
}

+ (instancetype)domainForRoleAccountUser:(uid_t)uid
{
    return [[[self alloc] initWithType:OSLaunchdDomainTypeRoleAccount handle:(uint64_t)uid] autorelease];
}

+ (instancetype)domainForPid:(pid_t)pid
{
    return [[[self alloc] initWithType:OSLaunchdDomainTypePid handle:(uint64_t)pid] autorelease];
}

- (instancetype)initWithType:(OSLaunchdDomainType)type handle:(uint64_t)handle
{
    self = [super init];
    if (self) {
        _type = type;
        _handle = handle;
    }
    return self;
}

- (id)copyWithZone:(NSZone *)zone
{
    (void)zone;
    return [self retain];
}

- (BOOL)isEqual:(id)object
{
    if (object == self) return YES;
    if (![object isKindOfClass:[OSLaunchdDomain class]]) return NO;
    OSLaunchdDomain *other = object;
    return _type == other->_type && _handle == other->_handle;
}

- (NSUInteger)hash
{
    return (NSUInteger)(_handle ^ ((uint64_t)_type << 56));
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"<OSLaunchdDomain type=%ld handle=%llu>",
            (long)_type, (unsigned long long)_handle];
}

@end

@implementation OSLaunchdJobExitStatus

@synthesize wait4Status = _wait4Status;
@synthesize os_reason_namespace = _os_reason_namespace;
@synthesize os_reason_code = _os_reason_code;
@synthesize os_reason_flags = _os_reason_flags;

- (instancetype)initWithWait4Status:(int)wait4Status
                 os_reason_namespace:(uint32_t)os_reason_namespace
                       os_reason_code:(uint64_t)os_reason_code
                      os_reason_flags:(uint64_t)os_reason_flags
{
    self = [super init];
    if (self) {
        _wait4Status = wait4Status;
        _os_reason_namespace = os_reason_namespace;
        _os_reason_code = os_reason_code;
        _os_reason_flags = os_reason_flags;
    }
    return self;
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"<OSLaunchdJobExitStatus wait4=%d reason=%u/%llu flags=%llu>",
            _wait4Status, _os_reason_namespace,
            (unsigned long long)_os_reason_code, (unsigned long long)_os_reason_flags];
}

@end

@implementation OSLaunchdJobInfo

@synthesize state = _state;
@synthesize pid = _pid;
@synthesize lastSpawnError = _lastSpawnError;
@synthesize lastExitStatus = _lastExitStatus;
@synthesize additionalPropertiesDictionary = _additionalPropertiesDictionary;
@synthesize removing = _removing;
@synthesize instance = _instance;
@synthesize program = _program;

- (instancetype)initWithState:(OSLaunchdJobState)state
                          pid:(pid_t)pid
               lastSpawnError:(int)lastSpawnError
               lastExitStatus:(OSLaunchdJobExitStatus *)lastExitStatus
   additionalPropertiesDict:(id)additionalPropertiesDictionary
                     removing:(BOOL)removing
                     instance:(NSUUID *)instance
                      program:(NSString *)program
{
    self = [super init];
    if (self) {
        _state = state;
        _pid = pid;
        _lastSpawnError = lastSpawnError;
        _lastExitStatus = [lastExitStatus retain];
        _additionalPropertiesDictionary = [additionalPropertiesDictionary retain];
        _removing = removing;
        _instance = [instance copy];
        _program = [program copy];
    }
    return self;
}

- (void)dealloc
{
    [_lastExitStatus release];
    [_additionalPropertiesDictionary release];
    [_instance release];
    [_program release];
    [super dealloc];
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"<OSLaunchdJobInfo state=%ld pid=%d spawn=%d program=%@>",
            (long)_state, _pid, _lastSpawnError, _program];
}

@end

@interface OSLaunchdJob ()
- (BOOL)domainAllowsSubmission:(NSError **)error;
- (OSLaunchdJobInfo *)queryInfoWithError:(int *)error;
- (OSLaunchdJobInfo *)pendingInfo;
- (void)startMonitorLocked;
- (void)monitorTick;
@end

@implementation OSLaunchdJob

@synthesize handle = _handle;

static void ASMonitorTick(void *context)
{
    [(OSLaunchdJob *)context monitorTick];
}

- (instancetype)initWithPlist:(NSDictionary<NSString *, id> *)plist
                       domain:(OSLaunchdDomain *)domain
{
    self = [super init];
    if (self) {
        _plist = [plist copy] ?: [[NSDictionary alloc] init];
        _domain = [domain retain];
        _handle = [[NSUUID alloc] init];
    }
    return self;
}

- (void)dealloc
{
    if (_monitorSource) {
        dispatch_source_cancel(_monitorSource);
        dispatch_release(_monitorSource);
    }
    if (_monitorQueue) dispatch_release(_monitorQueue);
    if (_monitorHandler) Block_release(_monitorHandler);
    [_lastInfo release];
    [_plist release];
    [_domain release];
    [_label release];
    [_handle release];
    [super dealloc];
}

- (OSLaunchdJobInfo *)pendingInfo
{
    return [[[OSLaunchdJobInfo alloc] initWithState:OSLaunchdJobStatePending
                                                pid:0
                                     lastSpawnError:0
                                     lastExitStatus:nil
                         additionalPropertiesDict:nil
                                           removing:NO
                                           instance:_handle
                                            program:_label] autorelease];
}

- (OSLaunchdJobInfo *)queryInfoWithError:(int *)error
{
    int lookupError = 0;
    launch_data_t job = ASGetJobData(_label, &lookupError);
    if (!job) {
        if (error) *error = lookupError;
        return nil;
    }

    long long pidValue = 0;
    long long exitValue = 0;
    long long spawnError = 0;
    BOOL hasPid = ASGetIntegerFromLaunchData(job, LAUNCH_JOBKEY_PID, &pidValue);
    BOOL hasExit = ASGetBoolFromLaunchData(job, LAUNCH_JOBKEY_LASTEXITSTATUSVALID) &&
                   ASGetIntegerFromLaunchData(job, LAUNCH_JOBKEY_LASTEXITSTATUS, &exitValue);
    BOOL hasSpawnError = ASGetIntegerFromLaunchData(job, LAUNCH_JOBKEY_LASTSPAWNERROR, &spawnError);
    NSString *program = ASStringFromLaunchData(job, LAUNCH_JOBKEY_PROGRAM);
    if (!program) {
        launch_data_t arguments = launch_data_dict_lookup(job, LAUNCH_JOBKEY_PROGRAMARGUMENTS);
        if (arguments && launch_data_get_type(arguments) == LAUNCH_DATA_ARRAY &&
            launch_data_array_get_count(arguments) > 0) {
            launch_data_t first = launch_data_array_get_index(arguments, 0);
            if (first && launch_data_get_type(first) == LAUNCH_DATA_STRING) {
                const char *value = launch_data_get_string(first);
                if (value) program = [NSString stringWithUTF8String:value];
            }
        }
    }
    launch_data_free(job);

    /*
     * Every state below is a field the launchd job dictionary actually
     * reports. A missing PID is not an exit, and a lookup failure is not a
     * zero-status termination:
     *   LastSpawnError != 0       -> spawn failure (state 3)
     *   LastExitStatus present    -> terminated, raw wait4 status (state 4)
     *   PID > 0                   -> running (state 2)
     *   otherwise                 -> pending (state 1)
     * The LastExitStatus key's presence matters: exit status 0 is a real
     * terminated child, not a substitute for a missing lookup.
     */
    OSLaunchdJobState state;
    int lastSpawnError = 0;
    if (hasSpawnError && spawnError != 0) {
        state = OSLaunchdJobStateSpawnFailure;
        lastSpawnError = (int)spawnError;
    } else if (hasExit) {
        state = OSLaunchdJobStateTerminated;
    } else if (hasPid && pidValue > 0) {
        state = OSLaunchdJobStateRunning;
    } else {
        state = OSLaunchdJobStatePending;
    }

    OSLaunchdJobExitStatus *exitStatus = nil;
    if (state == OSLaunchdJobStateTerminated) {
        exitStatus = [[[OSLaunchdJobExitStatus alloc] initWithWait4Status:(int)exitValue
                                                      os_reason_namespace:0
                                                            os_reason_code:0
                                                           os_reason_flags:0] autorelease];
    }
    if (error) *error = 0;
    return [[[OSLaunchdJobInfo alloc] initWithState:state
                                                pid:(state == OSLaunchdJobStateRunning) ? (pid_t)pidValue : 0
                                     lastSpawnError:lastSpawnError
                                     lastExitStatus:exitStatus
                         additionalPropertiesDict:nil
                                           removing:NO
                                           instance:_handle
                                            program:program ?: _label] autorelease];
}

- (OSLaunchdJobInfo *)getCurrentJobInfo
{
    @synchronized (self) {
        if (!_submitted || !_label.length) return nil;
        int error = 0;
        return [self queryInfoWithError:&error];
    }
}

- (OSLaunchdJobInfo *)submitAndStart:(NSError **)error
{
    ASSetError(error, nil);
    @synchronized (self) {
        if (_submitted) {
            int queryError = 0;
            OSLaunchdJobInfo *info = [self queryInfoWithError:&queryError];
            if (!info) {
                ASSetError(error, ASPOSIXError(queryError ? queryError : EIO,
                    @"The submitted launchd job could not be queried."));
            }
            return info;
        }
        if (![self domainAllowsSubmission:error]) {
            return nil;
        }

        NSMutableDictionary *submittedPlist = [[_plist mutableCopy] autorelease];
        if (!submittedPlist) {
            ASSetError(error, ASPOSIXError(ENOMEM, @"Could not copy the launchd job plist."));
            return nil;
        }
        id labelValue = submittedPlist[@"Label"];
        NSString *label = nil;
        if ([labelValue isKindOfClass:[NSString class]] && [labelValue length]) {
            label = labelValue;
        } else {
            label = [NSString stringWithFormat:@"com.darling.appserversupport.%@",
                     [[NSUUID UUID] UUIDString]];
            submittedPlist[@"Label"] = label;
        }
        if ([label length] >= 255) {
            ASSetError(error, ASPOSIXError(ENAMETOOLONG, @"The launchd job label is too long."));
            return nil;
        }
        [_label release];
        _label = [label copy];

        CFErrorRef cfError = NULL;
        BOOL submitted = SMJobSubmit(ASSubmitDomain(_domain),
                                     (CFDictionaryRef)submittedPlist, NULL, &cfError);
        if (!submitted) {
            NSError *submitError = ASCFError(cfError);
            if (!submitError) {
                submitError = ASPOSIXError(EINVAL, @"launchd rejected the job submission.");
            }
            ASSetError(error, submitError);
            return nil;
        }
        if (cfError) CFRelease(cfError);

        _submitted = YES;
        _reportedTerminal = NO;

        NSError *startError = nil;
        if (!ASStartJob(_label, &startError)) {
            /* StartJob failed after submission. Remove exactly the job we
             * loaded, in the same domain, and do not leave a retry in the
             * "already submitted" query branch. The original start error is
             * the caller-visible error; rollback failure is not made a success. */
            CFErrorRef rollbackError = NULL;
            BOOL rolledBack = SMJobRemove(ASSubmitDomain(_domain),
                                          (CFStringRef)_label, NULL, false, &rollbackError);
            if (!rolledBack && rollbackError) {
                CFIndex rollbackCode = CFErrorGetCode(rollbackError);
                if (rollbackCode == ENOENT || rollbackCode == ESRCH) {
                    rolledBack = YES;
                }
            }
            if (rollbackError) {
                CFRelease(rollbackError);
            }
            if (rolledBack) {
                _submitted = NO;
            }
            ASSetError(error, startError ?: ASPOSIXError(EIO,
                @"The submitted launchd job could not be started."));
            return nil;
        }

        [self startMonitorLocked];
        int queryError = 0;
        OSLaunchdJobInfo *info = [self queryInfoWithError:&queryError];
        if (!info) {
            ASSetError(error, ASPOSIXError(queryError ? queryError : EIO,
                @"The submitted launchd job could not be queried."));
            return nil;
        }
        return info;
    }
}

- (BOOL)domainAllowsSubmission:(NSError **)error
{
    return ASDomainAllowsSubmission(_domain, error);
}

- (BOOL)remove:(NSError **)error
{
    ASSetError(error, nil);
    @synchronized (self) {
        if (!_submitted || !_label.length) {
            ASSetError(error, ASPOSIXError(ESRCH, @"The launchd job is not loaded."));
            return NO;
        }
        CFErrorRef cfError = NULL;
        BOOL removed = SMJobRemove(ASSubmitDomain(_domain), (CFStringRef)_label,
                                   NULL, false, &cfError);
        if (!removed) {
            NSError *removeError = ASCFError(cfError);
            if (!removeError) {
                removeError = ASPOSIXError(EINVAL, @"launchd rejected the job removal.");
            }
            ASSetError(error, removeError);
            return NO;
        }
        if (cfError) CFRelease(cfError);
        _submitted = NO;
        return YES;
    }
}

- (void)monitorOnQueue:(dispatch_queue_t)queue
           withHandler:(void (^)(OSLaunchdJobInfo *, int))handler
{
    if (!handler) return;
    if (!queue) queue = dispatch_get_main_queue();
    @synchronized (self) {
        if (_monitorQueue != queue) {
            dispatch_retain(queue);
            if (_monitorQueue) dispatch_release(_monitorQueue);
            _monitorQueue = queue;
        }
        if (_monitorHandler) Block_release(_monitorHandler);
        _monitorHandler = Block_copy(handler);
        if (_submitted) [self startMonitorLocked];
    }
}

- (void)startMonitorLocked
{
    if (_monitorSource || !_monitorHandler || !_submitted) return;
    dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                                     _monitorQueue);
    if (!source) return;
    _monitorSource = source;
    dispatch_set_context(source, self);
    dispatch_source_set_event_handler_f(source, ASMonitorTick);
    dispatch_source_set_timer(source, dispatch_time(DISPATCH_TIME_NOW, 0),
                              50 * NSEC_PER_MSEC, 10 * NSEC_PER_MSEC);
    dispatch_resume(source);
}

- (void)monitorTick
{
    OSLaunchdJobInfo *info = nil;
    void (^handler)(OSLaunchdJobInfo *, int) = nil;
    int status = 0;
    BOOL stop = NO;

    @synchronized (self) {
        if (!_submitted || !_monitorHandler) return;

        int queryError = 0;
        info = [[self queryInfoWithError:&queryError] retain];
        if (!info) {
            /* A lookup/transport/protocol failure is not a job exit. Deliver the
             * error without inventing a zero-status termination. */
            handler = Block_copy(_monitorHandler);
            status = queryError ? queryError : EIO;
            stop = YES;
        } else {
            int oldWait = _lastInfo.lastExitStatus ? _lastInfo.lastExitStatus.wait4Status : INT_MIN;
            int newWait = info.lastExitStatus ? info.lastExitStatus.wait4Status : INT_MIN;
            BOOL changed = (_lastInfo == nil) ||
                           (_lastInfo.state != info.state) ||
                           (_lastInfo.pid != info.pid) ||
                           (_lastInfo.lastSpawnError != info.lastSpawnError) ||
                           (oldWait != newWait);
            if (!changed) {
                [info release];
                return;
            }
            [_lastInfo release];
            _lastInfo = [info retain];

            if (info.state == OSLaunchdJobStateSpawnFailure ||
                info.state == OSLaunchdJobStateTerminated) {
                _reportedTerminal = YES;
                stop = YES;
            }
            handler = Block_copy(_monitorHandler);
        }
    }

    if (handler) {
        handler(info, status);
        Block_release(handler);
    }
    if (info) [info release];
    if (stop) {
        [self cancelMonitor];
    }
}

- (void)cancelMonitor
{
    @synchronized (self) {
        if (_monitorSource) {
            dispatch_source_cancel(_monitorSource);
            dispatch_release(_monitorSource);
            _monitorSource = NULL;
        }
        if (_monitorHandler) {
            Block_release(_monitorHandler);
            _monitorHandler = NULL;
        }
        if (_monitorQueue) {
            dispatch_release(_monitorQueue);
            _monitorQueue = NULL;
        }
    }
}

@end
