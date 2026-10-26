/*
 * AppServerSupport -- the launchd job client used by CoreDeviceUtilities.
 *
 * This file defines only the surface this tree has measured a consumer using.
 * The implementation is a client of the existing Darling launchd through
 * ServiceManagement/liblaunch; it is not a class-only placeholder.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSString.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSUUID.h>
#import <Foundation/NSError.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSValue.h>
#import <dispatch/dispatch.h>

#include <sys/types.h>

NS_ASSUME_NONNULL_BEGIN

/*
 * CoreDeviceUtilities consumes these numeric states directly:
 *   2 = running, 3 = spawn failure (lastSpawnError is set),
 *   4 = terminated (lastExitStatus carries the raw wait4 status).
 * A pending job that has not started is 1; 0 is the unknown/uninitialized state.
 */
typedef NS_ENUM(NSInteger, OSLaunchdJobState) {
    OSLaunchdJobStateUnknown = 0,
    OSLaunchdJobStatePending = 1,
    OSLaunchdJobStateRunning = 2,
    OSLaunchdJobStateSpawnFailure = 3,
    OSLaunchdJobStateTerminated = 4,
};

typedef NS_ENUM(NSInteger, OSLaunchdDomainType) {
    OSLaunchdDomainTypeSystem = 0,
    OSLaunchdDomainTypeUser = 1,
    OSLaunchdDomainTypeRoleAccount = 2,
    OSLaunchdDomainTypePid = 3,
};

@interface OSLaunchdDomain : NSObject <NSCopying> {
    OSLaunchdDomainType _type;
    uint64_t _handle;
}

@property (nonatomic, readonly) OSLaunchdDomainType type;
@property (nonatomic, readonly) uint64_t handle;

+ (instancetype)systemDomain;
+ (instancetype)currentDomain;
+ (instancetype)domainForUser:(uid_t)uid;
+ (instancetype)domainForRoleAccountUser:(uid_t)uid;
+ (instancetype)domainForPid:(pid_t)pid;

- (instancetype)initWithType:(OSLaunchdDomainType)type handle:(uint64_t)handle NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface OSLaunchdJobExitStatus : NSObject {
    int _wait4Status;
    uint32_t _os_reason_namespace;
    uint64_t _os_reason_code;
    uint64_t _os_reason_flags;
}

@property (nonatomic, readonly) int wait4Status;
@property (nonatomic, readonly) uint32_t os_reason_namespace;
@property (nonatomic, readonly) uint64_t os_reason_code;
@property (nonatomic, readonly) uint64_t os_reason_flags;

- (instancetype)initWithWait4Status:(int)wait4Status
                 os_reason_namespace:(uint32_t)os_reason_namespace
                       os_reason_code:(uint64_t)os_reason_code
                      os_reason_flags:(uint64_t)os_reason_flags NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface OSLaunchdJobInfo : NSObject {
    OSLaunchdJobState _state;
    pid_t _pid;
    int _lastSpawnError;
    OSLaunchdJobExitStatus *_lastExitStatus;
    id _additionalPropertiesDictionary;
    BOOL _removing;
    NSUUID *_instance;
    NSString *_program;
}

@property (nonatomic, readonly) OSLaunchdJobState state;
@property (nonatomic, readonly) pid_t pid;
@property (nonatomic, readonly) int lastSpawnError;
@property (nonatomic, readonly, nullable) OSLaunchdJobExitStatus *lastExitStatus;
@property (nonatomic, readonly, nullable) id additionalPropertiesDictionary;
@property (nonatomic, readonly) BOOL removing;
@property (nonatomic, readonly, copy) NSUUID *instance;
@property (nonatomic, readonly, copy) NSString *program;

- (instancetype)initWithState:(OSLaunchdJobState)state
                          pid:(pid_t)pid
               lastSpawnError:(int)lastSpawnError
               lastExitStatus:(nullable OSLaunchdJobExitStatus *)lastExitStatus
   additionalPropertiesDict:(nullable id)additionalPropertiesDictionary
                     removing:(BOOL)removing
                     instance:(NSUUID *)instance
                      program:(NSString *)program NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface OSLaunchdJob : NSObject {
    NSDictionary *_plist;
    OSLaunchdDomain *_domain;
    NSString *_label;
    NSUUID *_handle;
    BOOL _submitted;
    BOOL _reportedTerminal;
    dispatch_queue_t _monitorQueue;
    dispatch_source_t _monitorSource;
    void (^_monitorHandler)(OSLaunchdJobInfo *, int);
    OSLaunchdJobInfo *_lastInfo;
}

@property (nonatomic, readonly, copy) NSUUID *handle;

- (instancetype)initWithPlist:(NSDictionary<NSString *, id> *)plist
                       domain:(OSLaunchdDomain *)domain NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

/* Submit the caller's one-shot plist, then request StartJob from the existing
 * launchd. The plist is not rewritten into continuous KeepAlive. */
- (nullable OSLaunchdJobInfo *)submitAndStart:(NSError **)error;

/* Remove the loaded job. The job's monitor, if any, is cancelled only by the
 * receiver's -cancelMonitor; this method returns the actual launchd result. */
- (BOOL)remove:(NSError **)error;

- (nullable OSLaunchdJobInfo *)getCurrentJobInfo;

/* Poll the existing launchd job dictionary on `queue`. A normal state change is
 * delivered with status 0; a transport/protocol/lookup failure is delivered
 * with nil info and the error in status, never as a terminated state. */
- (void)monitorOnQueue:(dispatch_queue_t)queue
           withHandler:(void (^)(OSLaunchdJobInfo *info, int status))handler;
- (void)cancelMonitor;

@end

NS_ASSUME_NONNULL_END
