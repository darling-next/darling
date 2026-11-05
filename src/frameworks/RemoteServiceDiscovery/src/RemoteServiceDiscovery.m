/*
 * RemoteServiceDiscovery.framework -- measured broker client and device/service objects.
 *
 * The provider is the native x86_64 RSD 3514.160.4 contract (UUID
 * 52169C73-70F0-3A27-BC23-862B89B80785) measured in the retained rsd-lookup,
 * rsd-native-contract, rsd-device-construction, rsd-service-construction,
 * rsd-listen-broker and rsd-loopback-contract studies:
 *
 *   - catalog/device lookup is a synchronous {"cmd":"get_device",...} or
 *     {"cmd":"get_unique_device","device_type":...} request on the cached
 *     com.apple.remoted connection;
 *   - a device description carries endpoint, properties and device_state;
 *     remote_device_copy_property returns the retained XPC dictionary value;
 *   - service lookup is {"cmd":"get_service","name":...} on the device's own
 *     endpoint connection; a service description carries endpoint and name;
 *   - connect is {"cmd":"connect",...} on the service endpoint connection and
 *     the broker transfers an XPC fd;
 *   - browsing is {"cmd":"browse","device_type":...,"present_only":false} on a
 *     per-browser com.apple.remoted connection; the event command is
 *     "found_device";
 *   - attach/connect/detach_loopback are fresh synchronous
 *     com.apple.remoted.control connections carrying only {"cmd":...}; success
 *     is a non-NULL, non-error reply. They carry no fd or callback.
 *
 * No com.apple.remoted producer exists in this guest today. The clients below
 * therefore return the real XPC error as false/NULL/-1; they do not synthesize
 * a connected device, a successful reply, or an empty device catalog.
 */

#import <objc/NSObject.h>
#import <objc/runtime.h>

#include <Block.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <xpc/xpc.h>

/* The SDK declaration is behind __BLOCKS__; this translation unit is compiled as Objective-C. */
extern void xpc_set_event_stream_handler(const char *stream, dispatch_queue_t queue,
    void (^handler)(xpc_object_t event));

#define RSD_BROKER_SERVICE "com.apple.remoted"
#define RSD_CONTROL_SERVICE "com.apple.remoted.control"
#define RSD_SERVICE_EVENT_STREAM "com.apple.remoted.service"
#define RSD_CONNECTION_FLAGS XPC_CONNECTION_MACH_SERVICE_PRIVILEGED
#define RSD_VERSION_FLAGS 0x0100000000000006ULL

typedef const void *remote_device_t;
typedef const void *remote_service_t;
typedef int remote_device_state_t;

typedef void (^RSDDeviceCallback)(id device, BOOL removed);
typedef void (^RSDDeviceStateCallback)(id device);
typedef void (^RSDServiceConnectCallback)(int fd, int error);
typedef void (^RSDServiceListenCallback)(id device, int fd);

typedef struct {
    char *name;
    char *alias;
    uint32_t type;
    uint32_t state;
    uint64_t device_id;
    uint64_t messaging_protocol_version;
    BOOL tls_enabled;
    BOOL has_uuid;
    uuid_t uuid;
    xpc_object_t properties;
    xpc_connection_t connection;
    dispatch_queue_t queue;
    RSDDeviceStateCallback connected_callback;
    RSDDeviceStateCallback disconnected_callback;
    dispatch_queue_t connected_queue;
    dispatch_queue_t disconnected_queue;
    BOOL cancelling;
} RSDDeviceStorage;

typedef struct {
    char *name;
    xpc_object_t properties;
    xpc_connection_t connection;
    id device;
    dispatch_queue_t queue;
    uint32_t connect_timeout;
    xpc_object_t keepalive;
    uint32_t keepalive_time;
    uint32_t keepcount;
    uint32_t keepinterval;
    uint64_t version;
    uint64_t version_flags;
} RSDServiceStorage;

typedef struct {
    uint32_t type;
    dispatch_queue_t queue;
    RSDDeviceCallback callback;
    xpc_connection_t connection;
    BOOL cancelling;
    BOOL cancelled;
} RSDBrowserStorage;

@interface OS_remote_device : NSObject {
@public
    RSDDeviceStorage *storage;
}
@end

@interface OS_remote_service : NSObject {
@public
    RSDServiceStorage *storage;
}
@end

@interface OS_remote_device_browser : NSObject {
@public
    RSDBrowserStorage *storage;
}
@end

static void rsd_device_storage_free(RSDDeviceStorage *storage);
static void rsd_service_storage_free(RSDServiceStorage *storage);
static void rsd_browser_storage_free(RSDBrowserStorage *storage);

@implementation OS_remote_device
- (void)dealloc
{
    rsd_device_storage_free(storage);
    storage = NULL;
    [super dealloc];
}
@end

@implementation OS_remote_service
- (void)dealloc
{
    rsd_service_storage_free(storage);
    storage = NULL;
    [super dealloc];
}
@end

@implementation OS_remote_device_browser
- (void)dealloc
{
    rsd_browser_storage_free(storage);
    storage = NULL;
    [super dealloc];
}
@end

static void rsd_log_error(const char *what)
{
    fprintf(stderr, "RemoteServiceDiscovery: %s\n", what);
}

static void rsd_device_storage_free(RSDDeviceStorage *s)
{
    if (!s) return;
    if (s->connection) {
        xpc_connection_set_event_handler(s->connection, ^(xpc_object_t event) { (void)event; });
        xpc_connection_cancel(s->connection);
        xpc_release(s->connection);
    }
    if (s->queue) dispatch_release(s->queue);
    if (s->connected_queue) dispatch_release(s->connected_queue);
    if (s->disconnected_queue) dispatch_release(s->disconnected_queue);
    if (s->connected_callback) Block_release(s->connected_callback);
    if (s->disconnected_callback) Block_release(s->disconnected_callback);
    if (s->properties) xpc_release(s->properties);
    free(s->name);
    free(s->alias);
    free(s);
}

static void rsd_service_storage_free(RSDServiceStorage *s)
{
    if (!s) return;
    if (s->connection) {
        xpc_connection_set_event_handler(s->connection, ^(xpc_object_t event) { (void)event; });
        xpc_connection_cancel(s->connection);
        xpc_release(s->connection);
    }
    if (s->queue) dispatch_release(s->queue);
    if (s->properties) xpc_release(s->properties);
    if (s->keepalive) xpc_release(s->keepalive);
    if (s->device) [(id)s->device release];
    free(s->name);
    free(s);
}

static void rsd_browser_storage_free(RSDBrowserStorage *s)
{
    if (!s) return;
    if (s->connection) {
        xpc_connection_set_event_handler(s->connection, ^(xpc_object_t event) { (void)event; });
        xpc_connection_cancel(s->connection);
        xpc_release(s->connection);
    }
    if (s->queue) dispatch_release(s->queue);
    if (s->callback) Block_release(s->callback);
    free(s);
}

static BOOL rsd_get_uint64(xpc_object_t dictionary, const char *key, uint64_t *value)
{
    xpc_object_t object = xpc_dictionary_get_value(dictionary, key);
    if (!object || xpc_get_type(object) != XPC_TYPE_UINT64) return NO;
    *value = xpc_uint64_get_value(object);
    return YES;
}

static BOOL rsd_get_bool(xpc_object_t dictionary, const char *key, BOOL *value)
{
    xpc_object_t object = xpc_dictionary_get_value(dictionary, key);
    if (!object || xpc_get_type(object) != XPC_TYPE_BOOL) return NO;
    *value = xpc_bool_get_value(object) ? YES : NO;
    return YES;
}

static const char *rsd_string(xpc_object_t dictionary, const char *key)
{
    xpc_object_t object = xpc_dictionary_get_value(dictionary, key);
    return object && xpc_get_type(object) == XPC_TYPE_STRING
        ? xpc_string_get_string_ptr(object) : NULL;
}

static OS_remote_device *rsd_device_alloc(void)
{
    OS_remote_device *device = [[OS_remote_device alloc] init];
    if (!device) return nil;
    device->storage = calloc(1, sizeof(*device->storage));
    if (!device->storage) {
        [device release];
        return nil;
    }
    device->storage->queue = dispatch_queue_create("com.apple.RemoteServiceDiscovery.device", NULL);
    return device;
}

static OS_remote_service *rsd_service_alloc(void)
{
    OS_remote_service *service = [[OS_remote_service alloc] init];
    if (!service) return nil;
    service->storage = calloc(1, sizeof(*service->storage));
    if (!service->storage) {
        [service release];
        return nil;
    }
    service->storage->queue = dispatch_queue_create("com.apple.RemoteServiceDiscovery.service", NULL);
    return service;
}

static OS_remote_device_browser *rsd_browser_alloc(void)
{
    OS_remote_device_browser *browser = [[OS_remote_device_browser alloc] init];
    if (!browser) return nil;
    browser->storage = calloc(1, sizeof(*browser->storage));
    if (!browser->storage) {
        [browser release];
        return nil;
    }
    return browser;
}

/* ---------------------------------------------------------------------------------------------
 * Broker transport.
 * ------------------------------------------------------------------------------------------- */

static xpc_connection_t g_broker_connection;
static dispatch_once_t g_broker_once;

static void rsd_broker_init(void *context)
{
    (void)context;
    dispatch_queue_t queue = dispatch_queue_create("com.apple.RemoteServiceDiscovery.broker", NULL);
    xpc_connection_t connection = xpc_connection_create_mach_service(
        RSD_BROKER_SERVICE, queue, RSD_CONNECTION_FLAGS);
    dispatch_release(queue);
    if (!connection) return;
    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
        if (xpc_get_type(event) == XPC_TYPE_ERROR) {
            fprintf(stderr, "RemoteServiceDiscovery: com.apple.remoted connection error\n");
        }
    });
    xpc_connection_activate(connection);
    g_broker_connection = connection;
}

static xpc_connection_t rsd_broker_connection(void)
{
    dispatch_once_f(&g_broker_once, NULL, rsd_broker_init);
    return g_broker_connection;
}

static xpc_object_t rsd_broker_send(xpc_connection_t connection, xpc_object_t request, int *error)
{
    if (!connection) {
        if (error) *error = 35;
        return NULL;
    }
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(connection, request);
    if (!reply) {
        if (error) *error = 35;
        return NULL;
    }
    if (xpc_get_type(reply) == XPC_TYPE_ERROR) {
        fprintf(stderr, "RemoteServiceDiscovery: com.apple.remoted XPC error reply\n");
        xpc_release(reply);
        if (error) *error = 35;
        return NULL;
    }
    return reply;
}

static BOOL rsd_result_ok(xpc_object_t reply, int *error)
{
    const char *result = rsd_string(reply, "result");
    if (result && strcmp(result, "OK") != 0) {
        if (error) *error = 3;
        return NO;
    }
    return YES;
}

/* ---------------------------------------------------------------------------------------------
 * Device state and object construction.
 * ------------------------------------------------------------------------------------------- */

static void rsd_device_invoke_connected(OS_remote_device *device);
static void rsd_device_invoke_disconnected(OS_remote_device *device);

static void rsd_device_advance_state_locked(OS_remote_device *device, uint32_t new_state)
{
    RSDDeviceStorage *s = device->storage;
    if (s->state == new_state) return;
    s->state = new_state;
    if (new_state == 2 || new_state == 3) rsd_device_invoke_connected(device);
    if (new_state == 3) rsd_device_invoke_disconnected(device);
}

static void rsd_device_invoke_connected(OS_remote_device *device)
{
    RSDDeviceStorage *s = device->storage;
    RSDDeviceStateCallback callback = s->connected_callback;
    dispatch_queue_t queue = s->connected_queue;
    if (!callback || !queue) return;
    s->connected_callback = NULL;
    s->connected_queue = NULL;
    dispatch_retain(queue);
    RSDDeviceStateCallback copy = Block_copy(callback);
    Block_release(callback);
    dispatch_async(queue, ^{
        copy(device);
        Block_release(copy);
        dispatch_release(queue);
    });
}

static void rsd_device_invoke_disconnected(OS_remote_device *device)
{
    RSDDeviceStorage *s = device->storage;
    RSDDeviceStateCallback callback = s->disconnected_callback;
    dispatch_queue_t queue = s->disconnected_queue;
    if (!callback || !queue) return;
    s->disconnected_callback = NULL;
    s->disconnected_queue = NULL;
    dispatch_retain(queue);
    RSDDeviceStateCallback copy = Block_copy(callback);
    Block_release(callback);
    dispatch_async(queue, ^{
        copy(device);
        Block_release(copy);
        dispatch_release(queue);
    });
}

static OS_remote_device *rsd_device_from_description(xpc_object_t description, int *error);

static void rsd_device_handle_event(OS_remote_device *device, xpc_object_t event)
{
    if (!device || !device->storage) return;
    if (xpc_get_type(event) == XPC_TYPE_ERROR) {
        /* This handler already runs on the device's serial queue. */
        if (device->storage->state == 2) {
            rsd_device_advance_state_locked(device, 3);
        }
        return;
    }
    if (xpc_get_type(event) != XPC_TYPE_DICTIONARY) return;
    const char *command = rsd_string(event, "cmd");
    if (!command || strcmp(command, "found_device") != 0) return;
    xpc_object_t nested = xpc_dictionary_get_dictionary(event, "device");
    if (!nested) return;
    int update_error = 0;
    OS_remote_device *updated = rsd_device_from_description(nested, &update_error);
    if (!updated) return;
    /* The native event updates the existing object; copy the fields we measured into it. */
    RSDDeviceStorage *s = device->storage;
    RSDDeviceStorage *u = updated->storage;
    free(s->name);
    s->name = u->name ? strdup(u->name) : NULL;
    u->name = NULL;
    free(s->alias);
    s->alias = u->alias ? strdup(u->alias) : NULL;
    u->alias = NULL;
    s->type = u->type;
    s->device_id = u->device_id;
    s->messaging_protocol_version = u->messaging_protocol_version;
    s->tls_enabled = u->tls_enabled;
    s->has_uuid = u->has_uuid;
    memcpy(s->uuid, u->uuid, sizeof(uuid_t));
    if (s->properties) xpc_release(s->properties);
    s->properties = u->properties ? xpc_retain(u->properties) : NULL;
    if (u->state) rsd_device_advance_state_locked(device, u->state);
    [updated release];
}

static OS_remote_device *rsd_device_from_description(xpc_object_t description, int *error)
{
    if (!description || xpc_get_type(description) != XPC_TYPE_DICTIONARY) {
        if (error) *error = EINVAL;
        return nil;
    }
    uint64_t type = 0;
    uint64_t device_id = 0;
    const char *name = rsd_string(description, "device_name");
    if (!rsd_get_uint64(description, "device_type", &type) || type == 0 || !name) {
        if (error) *error = EINVAL;
        return nil;
    }
    (void)rsd_get_uint64(description, "device_id", &device_id);

    OS_remote_device *device = rsd_device_alloc();
    if (!device) {
        if (error) *error = ENOMEM;
        return nil;
    }
    RSDDeviceStorage *s = device->storage;
    s->type = (uint32_t)type;
    s->device_id = (uint32_t)device_id; /* the measured x86 path truncates this field */
    s->name = strdup(name);
    const char *alias = rsd_string(description, "device_alias");
    if (alias) s->alias = strdup(alias);
    uint64_t protocol = 0;
    if (rsd_get_uint64(description, "device_messaging_protocol_version", &protocol)) {
        s->messaging_protocol_version = protocol;
    }
    BOOL tls = NO;
    if (rsd_get_bool(description, "device_tls_enabled", &tls)) s->tls_enabled = tls;
    xpc_object_t uuid = xpc_dictionary_get_value(description, "UUID");
    if (uuid && xpc_get_type(uuid) == XPC_TYPE_UUID) {
        memcpy(s->uuid, xpc_uuid_get_bytes(uuid), sizeof(uuid_t));
        s->has_uuid = YES;
    }
    xpc_object_t properties = xpc_dictionary_get_dictionary(description, "properties");
    if (properties) s->properties = xpc_retain(properties);

    uint64_t state = 1;
    if (!rsd_get_uint64(description, "device_state", &state) || state == 0) state = 1;
    s->state = (uint32_t)state;

    xpc_object_t endpoint = xpc_dictionary_get_value(description, "endpoint");
    if (endpoint) {
        xpc_connection_t connection = xpc_connection_create_from_endpoint(endpoint);
        if (connection) {
            s->connection = connection;
            xpc_connection_set_target_queue(connection, s->queue);
            __unsafe_unretained OS_remote_device *weak_device = device;
            xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
                rsd_device_handle_event(weak_device, event);
            });
            xpc_connection_activate(connection);
            xpc_object_t hello = xpc_dictionary_create(NULL, NULL, 0);
            xpc_dictionary_set_string(hello, "cmd", "helo_device");
            xpc_dictionary_set_uint64(hello, "device_state", (uint64_t)s->state);
            xpc_connection_send_message(connection, hello);
            xpc_release(hello);
        }
    }
    if (error) *error = 0;
    return device;
}

/* ---------------------------------------------------------------------------------------------
 * Device client surface.
 * ------------------------------------------------------------------------------------------- */

remote_device_t remote_device_copy_device_with_name(const char *name)
{
    if (!name) {
        rsd_log_error("device name must be a C string");
        return NULL;
    }
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "cmd", "get_device");
    xpc_dictionary_set_string(request, "device_name", name);
    int error = 0;
    xpc_object_t reply = rsd_broker_send(rsd_broker_connection(), request, &error);
    xpc_release(request);
    if (!reply) {
        errno = error;
        return NULL;
    }
    if (!rsd_result_ok(reply, &error)) {
        xpc_release(reply);
        errno = error;
        return NULL;
    }
    xpc_object_t description = xpc_dictionary_get_dictionary(reply, "device");
    OS_remote_device *device = description
        ? rsd_device_from_description(description, &error) : NULL;
    xpc_release(reply);
    if (!device) {
        errno = error ? error : EIO;
        return NULL;
    }
    return device;
}

remote_device_t remote_device_copy_unique_of_type(uint32_t type)
{
    if (type == 0) {
        rsd_log_error("API misuse: Invalid type value passed to remote_device_copy_unique_of_type");
        errno = EINVAL;
        return NULL;
    }
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "cmd", "get_unique_device");
    xpc_dictionary_set_uint64(request, "device_type", type);
    int error = 0;
    xpc_object_t reply = rsd_broker_send(rsd_broker_connection(), request, &error);
    xpc_release(request);
    if (!reply) {
        errno = error;
        return NULL;
    }
    xpc_object_t description = xpc_dictionary_get_dictionary(reply, "device");
    OS_remote_device *device = description
        ? rsd_device_from_description(description, &error) : NULL;
    xpc_release(reply);
    if (!device) {
        errno = error ? error : EIO;
        return NULL;
    }
    return device;
}

const char *remote_device_get_name(remote_device_t device)
{
    if (!device) return NULL;
    return ((OS_remote_device *)device)->storage->name;
}

uint32_t remote_device_get_type(remote_device_t device)
{
    if (!device) return 0;
    return ((OS_remote_device *)device)->storage->type;
}

remote_device_state_t remote_device_get_state(remote_device_t device)
{
    if (!device) return 0;
    __block uint32_t state = 0;
    OS_remote_device *object = (OS_remote_device *)device;
    dispatch_sync(object->storage->queue, ^{ state = object->storage->state; });
    return (remote_device_state_t)state;
}

void remote_device_copy_uuid(remote_device_t device, uuid_t out_uuid)
{
    if (!out_uuid) return;
    uuid_clear(out_uuid);
    if (!device) return;
    OS_remote_device *object = (OS_remote_device *)device;
    dispatch_sync(object->storage->queue, ^{
        if (object->storage->has_uuid) memcpy(out_uuid, object->storage->uuid, sizeof(uuid_t));
    });
}

xpc_object_t remote_device_copy_property(remote_device_t device, const char *key)
{
    if (!device || !key) return NULL;
    OS_remote_device *object = (OS_remote_device *)device;
    __block xpc_object_t result = NULL;
    dispatch_sync(object->storage->queue, ^{
        if (object->storage->properties) {
            result = xpc_dictionary_get_value(object->storage->properties, key);
            if (result) xpc_retain(result);
        }
    });
    return result;
}

/* Native source: the current macOS provider reads these values from the device's retained XPC
 * properties on the device's serial queue. copy_os_build and copy_product_type return a caller-owned
 * copy of the string (free it), or NULL when the property is absent or not a string; the
 * version-flags getter returns the uint64 property and 0 when it is absent or another type. A NULL
 * device keeps this provider's defensive contract (NULL/0) instead of the native invalid-client
 * crash, matching the getters above. */
char *remote_device_copy_os_build(remote_device_t device)
{
    if (!device) return NULL;
    OS_remote_device *object = (OS_remote_device *)device;
    __block char *result = NULL;
    dispatch_sync(object->storage->queue, ^{
        if (object->storage->properties) {
            const char *value =
                xpc_dictionary_get_string(object->storage->properties, "BuildVersion");
            if (value) result = strdup(value);
        }
    });
    return result;
}

char *remote_device_copy_product_type(remote_device_t device)
{
    if (!device) return NULL;
    OS_remote_device *object = (OS_remote_device *)device;
    __block char *result = NULL;
    dispatch_sync(object->storage->queue, ^{
        if (object->storage->properties) {
            const char *value =
                xpc_dictionary_get_string(object->storage->properties, "ProductType");
            if (value) result = strdup(value);
        }
    });
    return result;
}

uint64_t remote_device_get_xpc_remote_connection_version_flags(remote_device_t device)
{
    uint64_t flags = 0;
    xpc_object_t value = remote_device_copy_property(device, "RemoteXPCVersionFlags");
    if (value) {
        if (xpc_get_type(value) == XPC_TYPE_UINT64) flags = xpc_uint64_get_value(value);
        xpc_release(value);
    }
    return flags;
}

void remote_device_set_connected_callback(remote_device_t device, dispatch_queue_t queue,
    void (^callback)(id device))
{
    if (!device || !callback) return;
    OS_remote_device *object = (OS_remote_device *)device;
    dispatch_sync(object->storage->queue, ^{
        RSDDeviceStorage *s = object->storage;
        if (s->connected_callback) Block_release(s->connected_callback);
        if (s->connected_queue) dispatch_release(s->connected_queue);
        s->connected_callback = Block_copy(callback);
        s->connected_queue = queue ? (dispatch_retain(queue), queue) : dispatch_get_main_queue();
        if (s->state == 2 || s->state == 3) rsd_device_invoke_connected(object);
    });
}

void remote_device_set_disconnected_callback(remote_device_t device, dispatch_queue_t queue,
    void (^callback)(id device))
{
    if (!device || !callback) return;
    OS_remote_device *object = (OS_remote_device *)device;
    dispatch_sync(object->storage->queue, ^{
        RSDDeviceStorage *s = object->storage;
        if (s->disconnected_callback) Block_release(s->disconnected_callback);
        if (s->disconnected_queue) dispatch_release(s->disconnected_queue);
        s->disconnected_callback = Block_copy(callback);
        s->disconnected_queue = queue ? (dispatch_retain(queue), queue) : dispatch_get_main_queue();
        if (s->state == 3) rsd_device_invoke_disconnected(object);
    });
}

/* ---------------------------------------------------------------------------------------------
 * Browsing.
 * ------------------------------------------------------------------------------------------- */

static const char *const g_device_type_names[] = {
    "unknown", "loopback", "eos", "bridge", "host", "bridge-external", "host-external",
    "bonjour-peer", "ncm-device", "ncm-host", "coredevice-device", "display-device",
    "display-host", "coredevice-host", "network-peer", "compute-controller", "compute-node",
    "virtualmachine", "virtualmachine-host"
};

const char *remote_device_type_get_description(uint32_t type)
{
    if (type < (sizeof(g_device_type_names) / sizeof(g_device_type_names[0]))) {
        return g_device_type_names[type];
    }
    return "unknown";
}

static uint32_t rsd_type_from_description(const char *description)
{
    if (!description) return 0;
    for (uint32_t i = 0; i < (sizeof(g_device_type_names) / sizeof(g_device_type_names[0])); i++) {
        if (strcmp(description, g_device_type_names[i]) == 0) return i;
    }
    return 0;
}

static void rsd_browser_handle_event(OS_remote_device_browser *browser, xpc_object_t event)
{
    if (!browser || !browser->storage) return;
    RSDBrowserStorage *s = browser->storage;
    if (xpc_get_type(event) == XPC_TYPE_ERROR) return;
    if (xpc_get_type(event) != XPC_TYPE_DICTIONARY) return;
    const char *command = rsd_string(event, "cmd");
    if (!command || strcmp(command, "found_device") != 0) return;
    xpc_object_t description = xpc_dictionary_get_dictionary(event, "device");
    if (!description) return;
    int error = 0;
    OS_remote_device *device = rsd_device_from_description(description, &error);
    if (!device) return;
    RSDDeviceCallback callback = s->callback;
    dispatch_queue_t queue = s->queue;
    if (callback && queue) {
        dispatch_retain(queue);
        RSDDeviceCallback copy = Block_copy(callback);
        dispatch_async(queue, ^{
            copy(device, NO);
            Block_release(copy);
            dispatch_release(queue);
        });
    }
    [device release];
}

static OS_remote_device_browser *rsd_start_browser(uint32_t type, dispatch_queue_t queue,
    void (^callback)(id device, BOOL removed), xpc_object_t request)
{
    if (!callback) return nil;
    OS_remote_device_browser *browser = rsd_browser_alloc();
    if (!browser) {
        xpc_release(request);
        return nil;
    }
    RSDBrowserStorage *s = browser->storage;
    s->type = type;
    s->queue = queue ? (dispatch_retain(queue), queue) : dispatch_get_main_queue();
    s->callback = Block_copy(callback);
    xpc_connection_t connection = xpc_connection_create_mach_service(
        RSD_BROKER_SERVICE, s->queue, RSD_CONNECTION_FLAGS);
    if (!connection) {
        xpc_release(request);
        [browser release];
        return nil;
    }
    s->connection = connection;
    __unsafe_unretained OS_remote_device_browser *weak_browser = browser;
    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
        rsd_browser_handle_event(weak_browser, event);
    });
    xpc_connection_activate(connection);
    xpc_connection_send_message(connection, request);
    xpc_release(request);
    return [browser autorelease];
}

remote_device_t remote_device_start_browsing(uint32_t type, dispatch_queue_t queue,
    void (^callback)(id device, BOOL removed))
{
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "cmd", "browse");
    xpc_dictionary_set_uint64(request, "device_type", type);
    xpc_dictionary_set_bool(request, "present_only", false);
    return rsd_start_browser(type, queue, callback, request);
}

remote_device_t remote_device_start_browsing_matching(xpc_object_t matching, dispatch_queue_t queue,
    void (^callback)(id device, BOOL removed))
{
    if (!matching) {
        rsd_log_error("API called with NULL matching_dict argument");
        return NULL;
    }
    uint32_t type = rsd_type_from_description(rsd_string(matching, "DeviceType"));
    uint64_t numeric_type = 0;
    if (rsd_get_uint64(matching, "device_type", &numeric_type)) type = (uint32_t)numeric_type;
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "cmd", "browse");
    xpc_dictionary_set_uint64(request, "device_type", type);
    xpc_dictionary_set_bool(request, "present_only", false);
    return rsd_start_browser(type, queue, callback, request);
}

void remote_device_browser_cancel(remote_device_t browser)
{
    if (!browser) {
        rsd_log_error("API called with NULL browser argument");
        return;
    }
    OS_remote_device_browser *object = (OS_remote_device_browser *)browser;
    [object retain];
    RSDBrowserStorage *s = object->storage;
    if (!s->cancelling) {
        s->cancelling = YES;
        if (s->connection) xpc_connection_cancel(s->connection);
        RSDDeviceCallback callback = s->callback;
        s->callback = NULL;
        if (callback) {
            RSDDeviceCallback copy = Block_copy(callback);
            Block_release(callback);
            dispatch_queue_t queue = s->queue;
            if (queue) {
                dispatch_retain(queue);
                dispatch_async(queue, ^{
                    copy(NULL, YES);
                    Block_release(copy);
                    dispatch_release(queue);
                });
            } else {
                Block_release(copy);
            }
        }
        s->cancelled = YES;
    }
    [object release];
}

/* ---------------------------------------------------------------------------------------------
 * Service client surface.
 * ------------------------------------------------------------------------------------------- */

static OS_remote_service *rsd_service_from_description(xpc_object_t description,
    OS_remote_device *device, int *error)
{
    if (!description || xpc_get_type(description) != XPC_TYPE_DICTIONARY) {
        if (error) *error = EINVAL;
        return nil;
    }
    const char *name = rsd_string(description, "name");
    xpc_object_t endpoint = xpc_dictionary_get_value(description, "endpoint");
    if (!name || !endpoint) {
        if (error) *error = EINVAL;
        return nil;
    }
    OS_remote_service *service = rsd_service_alloc();
    if (!service) {
        if (error) *error = ENOMEM;
        return nil;
    }
    RSDServiceStorage *s = service->storage;
    s->name = strdup(name);
    xpc_object_t properties = xpc_dictionary_get_dictionary(description, "properties");
    if (properties) s->properties = xpc_retain(properties);
    if (device) s->device = [device retain];
    xpc_connection_t connection = xpc_connection_create_from_endpoint(endpoint);
    if (connection) {
        s->connection = connection;
        xpc_connection_set_target_queue(connection, s->queue);
        xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
            if (xpc_get_type(event) == XPC_TYPE_ERROR) {
                fprintf(stderr, "RemoteServiceDiscovery: remote service connection error\n");
            }
        });
        xpc_connection_activate(connection);
    }
    if (error) *error = 0;
    return service;
}

remote_service_t remote_device_copy_service(remote_device_t device, const char *name)
{
    if (!device || !name) {
        errno = EINVAL;
        return NULL;
    }
    OS_remote_device *object = (OS_remote_device *)device;
    if (remote_device_get_state(device) <= 1) {
        errno = ENOTCONN;
        return NULL;
    }
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "cmd", "get_service");
    xpc_dictionary_set_string(request, "name", name);
    int error = 0;
    xpc_object_t reply = rsd_broker_send(object->storage->connection, request, &error);
    xpc_release(request);
    if (!reply) {
        errno = error;
        return NULL;
    }
    if (!rsd_result_ok(reply, &error)) {
        xpc_release(reply);
        errno = error;
        return NULL;
    }
    xpc_object_t description = xpc_dictionary_get_dictionary(reply, "service");
    OS_remote_service *service = description
        ? rsd_service_from_description(description, object, &error) : NULL;
    xpc_release(reply);
    if (!service) {
        errno = error ? error : EIO;
        return NULL;
    }
    return service;
}

const char *remote_service_get_name(remote_service_t service)
{
    if (!service) return NULL;
    return ((OS_remote_service *)service)->storage->name;
}

uint64_t remote_service_get_version(remote_service_t service)
{
    if (!service) return 0;
    return ((OS_remote_service *)service)->storage->version;
}

uint64_t remote_service_get_xpc_remote_connection_version_flags(remote_service_t service)
{
    if (!service) return 0;
    return ((OS_remote_service *)service)->storage->version_flags;
}

remote_device_t remote_service_copy_device(remote_service_t service)
{
    if (!service) return NULL;
    RSDServiceStorage *s = ((OS_remote_service *)service)->storage;
    return s->device ? [(id)s->device retain] : NULL;
}

xpc_object_t remote_service_copy_property(remote_service_t service, const char *key)
{
    if (!service || !key) return NULL;
    xpc_object_t properties = ((OS_remote_service *)service)->storage->properties;
    xpc_object_t value = properties ? xpc_dictionary_get_value(properties, key) : NULL;
    return value ? xpc_retain(value) : NULL;
}

static xpc_object_t rsd_connect_request(RSDServiceStorage *s)
{
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "cmd", "connect");
    xpc_dictionary_set_uint64(request, "connect_timeout", s->connect_timeout);
    if (s->keepalive) xpc_dictionary_set_value(request, "keepalive", s->keepalive);
    xpc_dictionary_set_uint64(request, "keepalive_time", s->keepalive_time);
    xpc_dictionary_set_uint64(request, "keepcount", s->keepcount);
    xpc_dictionary_set_uint64(request, "keepinterval", s->keepinterval);
    return request;
}

int remote_service_create_connected_socket(remote_service_t service)
{
    if (!service) {
        errno = EINVAL;
        return -1;
    }
    RSDServiceStorage *s = ((OS_remote_service *)service)->storage;
    if (!s->connection) {
        errno = ENOTCONN;
        return -1;
    }
    xpc_object_t request = rsd_connect_request(s);
    int error = 0;
    xpc_object_t reply = rsd_broker_send(s->connection, request, &error);
    xpc_release(request);
    if (!reply) {
        errno = error;
        return -1;
    }
    int fd = xpc_dictionary_dup_fd(reply, "fd");
    xpc_release(reply);
    if (fd < 0) {
        errno = EIO;
        return -1;
    }
    struct pollfd pollfd = { .fd = fd, .events = POLLOUT, .revents = 0 };
    int poll_result;
    do {
        poll_result = poll(&pollfd, 1, -1);
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result <= 0 || (pollfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        close(fd);
        errno = EIO;
        return -1;
    }
    return fd;
}

void remote_service_connect_socket(remote_service_t service, dispatch_queue_t queue,
    void (^callback)(int fd, int error))
{
    if (!service || !callback) return;
    RSDServiceStorage *s = ((OS_remote_service *)service)->storage;
    if (!s->connection) {
        callback(-1, 83);
        return;
    }
    xpc_object_t request = rsd_connect_request(s);
    dispatch_queue_t reply_queue = queue ? queue : dispatch_get_main_queue();
    dispatch_retain(reply_queue);
    [(id)service retain];
    RSDServiceConnectCallback copy = Block_copy(callback);
    xpc_connection_send_message_with_reply(s->connection, request, reply_queue,
        ^(xpc_object_t reply) {
            int fd = -1;
            int error = 83;
            if (reply && xpc_get_type(reply) == XPC_TYPE_ERROR) {
                error = 83;
            } else if (reply) {
                const char *result = rsd_string(reply, "result");
                if (!result || strcmp(result, "OK") != 0) {
                    error = (result && strcmp(result, "INTERRUPTED") == 0) ? 5 : 83;
                } else {
                    fd = xpc_dictionary_dup_fd(reply, "fd");
                    if (fd < 0) error = 83;
                    else error = 0;
                }
            }
            copy(fd, error);
            Block_release(copy);
            [(id)service release];
            dispatch_release(reply_queue);
        });
    xpc_release(request);
}

void remote_service_listen_with_device(const char *name, dispatch_queue_t queue,
    void (^callback)(id device, int fd))
{
    if (!name || !queue || !callback) return;
    /* The native listener installs one com.apple.remoted.service event-stream handler. The event
     * carries the device description and an XPC fd. This is the measured transport; no listener
     * cancellation token exists in the inspected image. */
    RSDServiceListenCallback copy = Block_copy(callback);
    xpc_set_event_stream_handler(RSD_SERVICE_EVENT_STREAM, queue, ^(xpc_object_t event) {
        if (xpc_get_type(event) != XPC_TYPE_DICTIONARY) return;
        xpc_object_t description = xpc_dictionary_get_dictionary(event, "device");
        int fd = xpc_dictionary_dup_fd(event, "fd");
        if (!description || fd < 0) {
            if (fd >= 0) close(fd);
            return;
        }
        int error = 0;
        OS_remote_device *device = rsd_device_from_description(description, &error);
        if (!device) {
            close(fd);
            return;
        }
        copy(device, fd);
        [device release];
    });
    /* xpc_set_event_stream_handler copied the block for the registration lifetime. */
    Block_release(copy);
}

/* ---------------------------------------------------------------------------------------------
 * Control broker.
 * ------------------------------------------------------------------------------------------- */

static BOOL rsd_control_command(const char *command)
{
    dispatch_queue_t queue = dispatch_queue_create("com.apple.RemoteServiceDiscovery.control", NULL);
    xpc_connection_t connection = xpc_connection_create_mach_service(
        RSD_CONTROL_SERVICE, queue, RSD_CONNECTION_FLAGS);
    dispatch_release(queue);
    if (!connection) {
        fprintf(stderr, "RemoteServiceDiscovery: control %s unavailable\n", command);
        return NO;
    }
    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
        if (xpc_get_type(event) == XPC_TYPE_ERROR) {
            fprintf(stderr, "RemoteServiceDiscovery: control connection error\n");
        }
    });
    xpc_connection_activate(connection);
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "cmd", command);
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(connection, request);
    xpc_release(request);
    BOOL ok = reply != NULL && xpc_get_type(reply) != XPC_TYPE_ERROR;
    fprintf(stderr, "RemoteServiceDiscovery: control %s reply=%s\n",
        command, ok ? "non-error" : "xpc-error");
    if (reply) xpc_release(reply);
    if (ok) xpc_connection_cancel(connection);
    xpc_release(connection);
    return ok;
}

bool remote_control_attach_loopback(void)
{
    return rsd_control_command("attach_loopback");
}

bool remote_control_connect_loopback(void)
{
    return rsd_control_command("connect_loopback");
}

bool remote_control_detach_loopback(void)
{
    return rsd_control_command("detach_loopback");
}
