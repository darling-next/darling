#include <dispatch/dispatch.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned sequence;

/* Older supported SDKs omit the workloop header; require the runtime API. */
typedef struct dispatch_workloop_s *workloop_handle_t;
typedef workloop_handle_t (*workloop_create_fn)(const char *);

int main(void)
{
    workloop_create_fn create_workloop = (workloop_create_fn)dlsym(RTLD_DEFAULT, "dispatch_workloop_create");
    assert(create_workloop != NULL);
    workloop_handle_t loop = create_workloop("darling.workloop.target");
    dispatch_queue_t queue = dispatch_queue_create_with_target("darling.workloop.serial",
            DISPATCH_QUEUE_SERIAL, (dispatch_queue_t)loop);
    dispatch_group_t group = dispatch_group_create();
    for (unsigned i = 0; i < 64; ++i) {
        dispatch_group_async(group, queue, ^{
            dispatch_assert_queue(queue);
            if (sequence != i) {
                fprintf(stderr, "workloop FIFO mismatch: expected %u, got %u\n", i, sequence);
                abort();
            }
            ++sequence;
        });
    }
    if (dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, 10LL * NSEC_PER_SEC)) != 0 || sequence != 64) {
        fprintf(stderr, "workloop target incomplete: %u/64\n", sequence);
        return 1;
    }
    dispatch_group_async(group, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        dispatch_sync(queue, ^{ dispatch_assert_queue(queue); ++sequence; });
    });
    if (dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, 10LL * NSEC_PER_SEC)) != 0 || sequence != 65) {
        fprintf(stderr, "workloop reentry incomplete: %u/65\n", sequence);
        return 1;
    }
    dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    dispatch_group_enter(group);
    dispatch_source_set_event_handler(timer, ^{
        dispatch_assert_queue(queue);
        if (sequence != 65) abort();
        ++sequence;
        dispatch_source_cancel(timer);
    });
    dispatch_source_set_cancel_handler(timer, ^{ dispatch_group_leave(group); });
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_MSEC),
            DISPATCH_TIME_FOREVER, 0);
    dispatch_resume(timer);
    if (dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, 10LL * NSEC_PER_SEC)) != 0 || sequence != 66) {
        fprintf(stderr, "workloop timer incomplete: %u/66\n", sequence);
        return 1;
    }
    dispatch_release(timer);
    dispatch_release(group);
    dispatch_release(queue);
    dispatch_release((dispatch_queue_t)loop);
    puts("DISPATCH_WORKLOOP_TARGET_OK");
    return 0;
}
