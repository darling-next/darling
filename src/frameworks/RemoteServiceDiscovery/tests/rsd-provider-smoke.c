/*
 * Bounded negative broker smoke for the RSD client surface.
 *
 * There is no com.apple.remoted or com.apple.remoted.control producer in this
 * guest. Every catalog/control call must therefore return the real XPC error
 * (NULL/errno 35 or false), never a synthetic device, reply, or empty catalog.
 * Browser construction/cancellation is exercised because it is local to the
 * client and must not hang or crash without a producer.
 */

#include <dispatch/dispatch.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <xpc/xpc.h>

typedef const void *remote_device_t;
typedef const void *remote_service_t;

extern const char *remote_device_type_get_description(uint32_t type);
extern remote_device_t remote_device_copy_device_with_name(const char *name);
extern remote_device_t remote_device_copy_unique_of_type(uint32_t type);
extern remote_device_t remote_device_start_browsing(uint32_t type, dispatch_queue_t queue,
    void (^callback)(id device, BOOL removed));
extern remote_device_t remote_device_start_browsing_matching(xpc_object_t matching,
    dispatch_queue_t queue, void (^callback)(id device, BOOL removed));
extern void remote_device_browser_cancel(remote_device_t browser);
extern int remote_service_create_connected_socket(remote_service_t service);
extern bool remote_control_attach_loopback(void);
extern bool remote_control_connect_loopback(void);
extern bool remote_control_detach_loopback(void);

static int failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        failures++;
        printf("RSD_SMOKE_CHECK FAIL %s\n", what);
    } else {
        printf("RSD_SMOKE_CHECK PASS %s\n", what);
    }
}

int main(void)
{
    const char *description = remote_device_type_get_description(1);
    check(description && strcmp(description, "loopback") == 0,
        "type 1 description is loopback");

    errno = 0;
    remote_device_t device = remote_device_copy_device_with_name("no-such-device");
    check(device == NULL && errno == 35,
        "missing broker get_device returns NULL/errno 35");

    errno = 0;
    device = remote_device_copy_unique_of_type(1);
    check(device == NULL && errno == 35,
        "missing broker get_unique_device returns NULL/errno 35");

    errno = 0;
    check(remote_service_create_connected_socket(NULL) == -1 && errno == EINVAL,
        "NULL service connect fails rather than fabricating an fd");

    check(!remote_control_attach_loopback(),
        "missing control broker attach returns false");
    check(!remote_control_connect_loopback(),
        "missing control broker connect returns false");
    check(!remote_control_detach_loopback(),
        "missing control broker detach returns false");

    dispatch_queue_t queue = dispatch_queue_create("rsd-provider-smoke", NULL);
    dispatch_semaphore_t callback_received = dispatch_semaphore_create(0);
    __block int callbacks = 0;

    remote_device_t browser = remote_device_start_browsing(1, queue,
        ^(id found, BOOL removed) {
            (void)found;
            (void)removed;
            callbacks++;
            dispatch_semaphore_signal(callback_received);
        });
    check(browser != NULL, "browser construction without broker returns an object");
    if (browser) {
        remote_device_browser_cancel(browser);
        long waited = dispatch_semaphore_wait(callback_received,
            dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
        check(waited == 0 && callbacks == 1,
            "browser cancel delivers one terminal callback");
    }

    xpc_object_t matching = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(matching, "DeviceType", "loopback");
    browser = remote_device_start_browsing_matching(matching, queue,
        ^(id found, BOOL removed) {
            (void)found;
            (void)removed;
            callbacks++;
            dispatch_semaphore_signal(callback_received);
        });
    check(browser != NULL, "matching browser construction without broker returns an object");
    if (browser) {
        remote_device_browser_cancel(browser);
        long waited = dispatch_semaphore_wait(callback_received,
            dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
        check(waited == 0 && callbacks == 2,
            "matching browser cancel delivers one terminal callback");
    }
    xpc_release(matching);
    dispatch_release(callback_received);
    dispatch_release(queue);

    printf("RSD_SMOKE_FAILURES=%d\n", failures);
    printf("RSD_SMOKE_RC=%d\n", failures == 0 ? 0 : 1);
    return failures == 0 ? 0 : 1;
}
