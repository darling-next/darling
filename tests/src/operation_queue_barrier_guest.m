#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* The same consumer must compile with the old installed SDK for runtime RED. */
@interface NSOperationQueue (BarrierConsumer)
- (void)addBarrierBlock:(void (^)(void))block;
@end

static void require(BOOL condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL operation queue barrier: %s\n", message);
        exit(1);
    }
}

static void await(dispatch_semaphore_t semaphore, const char *message)
{
    require(dispatch_semaphore_wait(semaphore,
        dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) == 0, message);
}

static void watchdog(int signalNumber)
{
    (void)signalNumber;
    static const char message[] = "FAIL operation queue barrier: watchdog expired\n";
    write(STDERR_FILENO, message, sizeof(message) - 1);
    _exit(124);
}

// start deliberately returns while isExecuting is still YES. Only the consumer's
// finish message publishes isFinished, including after cancellation.
@interface BarrierAsyncOperation : NSOperation {
    pthread_mutex_t _lock;
    BOOL _executing;
    BOOL _finished;
    dispatch_semaphore_t _started;
}
- (void)awaitStart;
- (void)finish;
@end

@implementation BarrierAsyncOperation
- (id)init
{
    self = [super init];
    if (self) {
        pthread_mutex_init(&_lock, NULL);
        _started = dispatch_semaphore_create(0);
    }
    return self;
}
- (void)dealloc
{
    dispatch_release(_started);
    pthread_mutex_destroy(&_lock);
    [super dealloc];
}
- (BOOL)isConcurrent { return YES; }
- (BOOL)isAsynchronous { return YES; }
- (BOOL)isExecuting
{
    pthread_mutex_lock(&_lock);
    BOOL result = _executing;
    pthread_mutex_unlock(&_lock);
    return result;
}
- (BOOL)isFinished
{
    pthread_mutex_lock(&_lock);
    BOOL result = _finished;
    pthread_mutex_unlock(&_lock);
    return result;
}
- (void)start
{
    if ([self isCancelled]) {
        [self finish];
        return;
    }
    [self willChangeValueForKey:@"isExecuting"];
    pthread_mutex_lock(&_lock);
    _executing = YES;
    pthread_mutex_unlock(&_lock);
    [self didChangeValueForKey:@"isExecuting"];
    dispatch_semaphore_signal(_started);
}
- (void)awaitStart { await(_started, "concurrent asynchronous operation did not start"); }
- (void)finish
{
    [self willChangeValueForKey:@"isExecuting"];
    [self willChangeValueForKey:@"isFinished"];
    pthread_mutex_lock(&_lock);
    _executing = NO;
    _finished = YES;
    pthread_mutex_unlock(&_lock);
    [self didChangeValueForKey:@"isFinished"];
    [self didChangeValueForKey:@"isExecuting"];
}
@end

// A cancelled operation still receives start, but must not execute main. Check
// its start as well as ordinary block bodies so cancellation cannot evade a fence.
@interface BarrierStartProbe : NSBlockOperation {
    void (^_check)(void);
}
- (id)initWithCheck:(void (^)(void))check body:(void (^)(void))body;
@end

@implementation BarrierStartProbe
- (id)initWithCheck:(void (^)(void))check body:(void (^)(void))body
{
    self = [super init];
    if (self) {
        _check = [check copy];
        [self addExecutionBlock:body];
    }
    return self;
}
- (void)dealloc
{
    [_check release];
    [super dealloc];
}
- (void)start
{
    _check();
    [super start];
}
@end

typedef struct {
    pthread_mutex_t lock;
    unsigned flags;
} BarrierState;

enum {
    Before = 1 << 0,
    FirstEntered = 1 << 1,
    FirstFinished = 1 << 2,
    After = 1 << 3,
    AddedDuringBarrier = 1 << 4,
    CancelledStarted = 1 << 5,
    SecondFinished = 1 << 6,
    LastFinished = 1 << 7
};

static void mark(BarrierState *state, unsigned prerequisites, unsigned event, const char *message)
{
    pthread_mutex_lock(&state->lock);
    require((state->flags & prerequisites) == prerequisites && !(state->flags & event), message);
    state->flags |= event;
    pthread_mutex_unlock(&state->lock);
}

static void asynchronousOrdering(void)
{
    BarrierState state = { PTHREAD_MUTEX_INITIALIZER, 0 };
    BarrierState *events = &state;
    dispatch_semaphore_t entered = dispatch_semaphore_create(0);
    dispatch_semaphore_t releaseBarrier = dispatch_semaphore_create(0);
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    dispatch_queue_t executor = dispatch_queue_create("barrier.consumer.async", DISPATCH_QUEUE_SERIAL);
    NSOperationQueue *queue = [[NSOperationQueue alloc] init];
    [queue setUnderlyingQueue:executor];
    [queue setMaxConcurrentOperationCount:2];
    [queue setSuspended:YES];
    BarrierAsyncOperation *first = [[BarrierAsyncOperation alloc] init];
    BarrierAsyncOperation *second = [[BarrierAsyncOperation alloc] init];
    NSBlockOperation *before = [NSBlockOperation blockOperationWithBlock:^{
        require([first isFinished], "ordinary dependency was not completed");
        mark(events, 0, Before, "pre-barrier dependent operation ran twice");
    }];
    [before addDependency:first];
    [queue addOperations:@[first, second, before] waitUntilFinished:NO];
    [queue addBarrierBlock:^{
        require([first isFinished] && [second isFinished] && [first isCancelled],
                "barrier preceded asynchronous completion, including cancelled work");
        require([NSOperationQueue currentQueue] == queue, "barrier lost currentQueue context");
        mark(events, Before, FirstEntered, "barrier preceded an earlier pending operation");
        dispatch_semaphore_signal(entered);
        await(releaseBarrier, "first barrier was not released");
        mark(events, FirstEntered, FirstFinished, "first barrier completion order");
    }];
    require([[queue operations] count] == 3 && [queue operationCount] == 3,
            "private barrier appeared in public operations or operationCount");
    NSBlockOperation *after = [NSBlockOperation blockOperationWithBlock:^{
        mark(events, FirstFinished, After, "later high-priority operation crossed the barrier");
    }];
    [after setQueuePriority:NSOperationQueuePriorityVeryHigh];
    [queue addOperation:after];
    [queue setSuspended:NO];
    [first awaitStart];
    [second awaitStart];
    [first cancel];
    // The serial executor acknowledges both start returns without completing
    // either operation. A dispatch-only start-return fence cannot pass this phase.
    dispatch_sync(executor, ^{});
    require(![first isFinished] && ![second isFinished], "asynchronous fixture finished prematurely");
    [second finish];
    [first finish];
    await(entered, "barrier did not follow asynchronous and dependent completion");

    [queue addOperationWithBlock:^{
        mark(events, FirstFinished, AddedDuringBarrier, "operation added during barrier started too early");
    }];
    BarrierStartProbe *cancelled = [[BarrierStartProbe alloc] initWithCheck:^{
        mark(events, FirstFinished, CancelledStarted, "cancelled later start crossed the barrier");
    } body:^{
        require(NO, "cancelled operation executed its block");
    }];
    [cancelled cancel];
    [queue addOperation:cancelled];
    [queue addBarrierBlock:^{
        require([cancelled isFinished], "second barrier preceded cancelled operation completion");
        mark(events, After | AddedDuringBarrier | CancelledStarted, SecondFinished,
             "second barrier did not wait for its complete preceding group");
    }];
    [queue addOperationWithBlock:^{
        mark(events, SecondFinished, LastFinished, "last operation crossed the second barrier");
        dispatch_semaphore_signal(completed);
    }];
    dispatch_semaphore_signal(releaseBarrier);
    await(completed, "operations did not resume after both barriers");
    [queue waitUntilAllOperationsAreFinished];
    require(state.flags == (Before | FirstEntered | FirstFinished | After |
            AddedDuringBarrier | CancelledStarted | SecondFinished | LastFinished),
            "asynchronous ordering oracle missed an event");
    [cancelled release];
    [first release];
    [second release];
    [queue release];
    dispatch_release(executor);
    dispatch_release(entered);
    dispatch_release(releaseBarrier);
    dispatch_release(completed);
    pthread_mutex_destroy(&state.lock);
}

static void concurrentExecutorOrdering(void)
{
    enum {
        HeldBarrierEntered = 1 << 0,
        InspectionComplete = 1 << 1,
        HeldBarrierFinished = 1 << 2,
        OneStarted = 1 << 3,
        TwoStarted = 1 << 4,
        OneCompleted = 1 << 5,
        TwoCompleted = 1 << 6
    };
    BarrierState state = { PTHREAD_MUTEX_INITIALIZER, 0 };
    BarrierState *events = &state;
    dispatch_semaphore_t entered = dispatch_semaphore_create(0);
    dispatch_semaphore_t releaseBarrier = dispatch_semaphore_create(0);
    dispatch_semaphore_t submitted = dispatch_semaphore_create(0);
    dispatch_semaphore_t inspected = dispatch_semaphore_create(0);
    dispatch_semaphore_t allowRelease = dispatch_semaphore_create(0);
    dispatch_semaphore_t oneReady = dispatch_semaphore_create(0);
    dispatch_semaphore_t twoReady = dispatch_semaphore_create(0);
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    dispatch_queue_t executor = dispatch_queue_create("barrier.consumer.concurrent", DISPATCH_QUEUE_CONCURRENT);
    NSOperationQueue *queue = [[NSOperationQueue alloc] init];
    [queue setUnderlyingQueue:executor];
    [queue setMaxConcurrentOperationCount:3];
    [queue addBarrierBlock:^{
        mark(events, 0, HeldBarrierEntered, "concurrent-executor barrier entered twice");
        dispatch_semaphore_signal(entered);
        await(releaseBarrier, "concurrent-executor barrier was not released");
        mark(events, InspectionComplete, HeldBarrierFinished, "barrier finished before held-phase inspection");
    }];
    await(entered, "concurrent-executor barrier did not enter");

    BarrierStartProbe *one = [[BarrierStartProbe alloc] initWithCheck:^{
        mark(events, HeldBarrierFinished, OneStarted, "first concurrent-executor start crossed held barrier");
    } body:^{
        dispatch_semaphore_signal(oneReady);
        await(twoReady, "later operations could not execute concurrently");
        mark(events, OneStarted | TwoStarted, OneCompleted, "first concurrent-executor completion order");
        dispatch_semaphore_signal(completed);
    }];
    BarrierStartProbe *two = [[BarrierStartProbe alloc] initWithCheck:^{
        mark(events, HeldBarrierFinished, TwoStarted, "second concurrent-executor start crossed held barrier");
    } body:^{
        dispatch_semaphore_signal(twoReady);
        await(oneReady, "later operations were serialized by the executor");
        mark(events, OneStarted | TwoStarted, TwoCompleted, "second concurrent-executor completion order");
        dispatch_semaphore_signal(completed);
    }];
    [two setQueuePriority:NSOperationQueuePriorityVeryHigh];
    [queue addOperation:one];
    // The controller is independent of the underlying executor: a conforming
    // implementation may itself use a dispatch barrier on that executor.
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        await(submitted, "later concurrent-executor operations were not submitted");
        require(![one isExecuting] && ![one isFinished] &&
                ![two isExecuting] && ![two isFinished],
                "later concurrent-executor operation ran while barrier was held");
        mark(events, HeldBarrierEntered, InspectionComplete, "held-phase inspection order");
        dispatch_semaphore_signal(inspected);
        await(allowRelease, "held-phase inspection was not acknowledged");
        dispatch_semaphore_signal(releaseBarrier);
    });
    [queue addOperation:two];
    dispatch_semaphore_signal(submitted);
    await(inspected, "held-phase concurrent-executor inspection did not complete");
    dispatch_semaphore_signal(allowRelease);
    await(completed, "first concurrent-executor operation did not finish");
    await(completed, "second concurrent-executor operation did not finish");
    [queue waitUntilAllOperationsAreFinished];
    require(state.flags == (HeldBarrierEntered | InspectionComplete | HeldBarrierFinished |
            OneStarted | TwoStarted | OneCompleted | TwoCompleted),
            "concurrent-executor ordering oracle missed an event");
    [one release];
    [two release];
    [queue release];
    dispatch_release(executor);
    dispatch_release(entered);
    dispatch_release(releaseBarrier);
    dispatch_release(submitted);
    dispatch_release(inspected);
    dispatch_release(allowRelease);
    dispatch_release(oneReady);
    dispatch_release(twoReady);
    dispatch_release(completed);
    pthread_mutex_destroy(&state.lock);
}

static void emptyAndSuspended(void)
{
    dispatch_queue_t executor = dispatch_queue_create("barrier.consumer.suspension", DISPATCH_QUEUE_SERIAL);
    dispatch_semaphore_t entered = dispatch_semaphore_create(0);
    dispatch_semaphore_t releaseBarrier = dispatch_semaphore_create(0);
    dispatch_semaphore_t exited = dispatch_semaphore_create(0);
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    NSOperationQueue *queue = [[NSOperationQueue alloc] init];
    [queue setUnderlyingQueue:executor];
    [queue setSuspended:YES];
    [queue addBarrierBlock:^{
        require(![queue isSuspended], "empty-queue barrier ignored suspension");
        dispatch_semaphore_signal(entered);
        await(releaseBarrier, "empty-queue barrier was not released");
        dispatch_semaphore_signal(exited);
    }];
    require([queue operationCount] == 0 && [[queue operations] count] == 0,
            "empty-queue barrier leaked through queue inspection");
    NSBlockOperation *after = [NSBlockOperation blockOperationWithBlock:^{
        require(![queue isSuspended], "later operation ignored renewed suspension");
        dispatch_semaphore_signal(completed);
    }];
    [queue addOperation:after];
    [queue setSuspended:NO];
    await(entered, "empty-queue barrier did not start on resume");
    [queue setSuspended:YES];
    dispatch_semaphore_signal(releaseBarrier);
    await(exited, "running barrier incorrectly stopped when queue was suspended");
    dispatch_sync(executor, ^{});
    require(![after isExecuting] && ![after isFinished], "later operation started while suspended");
    [queue setSuspended:NO];
    await(completed, "later operation did not start after second resume");
    [queue waitUntilAllOperationsAreFinished];
    [queue release];
    dispatch_release(executor);
    dispatch_release(entered);
    dispatch_release(releaseBarrier);
    dispatch_release(exited);
    dispatch_release(completed);
}

static void cancellation(void)
{
    NSOperationQueue *queue = [[NSOperationQueue alloc] init];
    [queue setSuspended:YES];
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    NSBlockOperation *before = [NSBlockOperation blockOperationWithBlock:^{
        require(NO, "cancelAllOperations did not suppress an earlier block");
    }];
    NSBlockOperation *after = [NSBlockOperation blockOperationWithBlock:^{
        require(NO, "cancelAllOperations did not suppress a later block");
    }];
    [queue addOperation:before];
    [queue addBarrierBlock:^{ require(NO, "cancelAllOperations did not cancel a pending barrier"); }];
    [queue addOperation:after];
    [queue cancelAllOperations];
    require([before isCancelled] && [after isCancelled], "queued operations were not cancelled");
    [queue addBarrierBlock:^{
        require([before isFinished] && [after isFinished], "cancelled operations opened a fence before finishing");
        dispatch_semaphore_signal(completed);
    }];
    [queue setSuspended:NO];
    await(completed, "cancelled barrier stranded the following generation");
    [queue waitUntilAllOperationsAreFinished];
    [queue release];
    dispatch_release(completed);
}

int main(void)
{
    signal(SIGALRM, watchdog);
    alarm(60);
    @autoreleasepool {
        asynchronousOrdering();
        concurrentExecutorOrdering();
        emptyAndSuspended();
        cancellation();
        NSOperationQueue *queue = [[NSOperationQueue alloc] init];
        BOOL caught = NO;
        @try {
            [queue addBarrierBlock:nil];
        } @catch (NSException *exception) {
            caught = [[exception name] isEqualToString:NSInvalidArgumentException];
        }
        require(caught, "nil barrier did not raise NSInvalidArgumentException");
        [queue release];
    }
    alarm(0);
    puts("OPERATION_QUEUE_BARRIER_GUEST_OK");
    return 0;
}
