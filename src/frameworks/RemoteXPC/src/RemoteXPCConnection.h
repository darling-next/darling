/*
 * RemoteXPCConnection.h -- opaque connection type and the exported entry points.
 *
 * The measured signatures are declared here. The two remote-service constructors implement the
 * measured native constructor bodies; the RSD service/listener transport is resolved from the
 * tracked RemoteServiceDiscovery image rather than replaced locally.
 */

#ifndef REMOTEXPC_CONNECTION_H
#define REMOTEXPC_CONNECTION_H

#include <dispatch/dispatch.h>
#include <stdint.h>
#include <xpc/xpc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The connection is a genuine libxpc object: it is declared with XPC_CLASS_DECL in the
 * implementation, so the public surface takes plain xpc_object_t references. */

xpc_object_t xpc_remote_connection_create_with_connected_fd(int fd,
	dispatch_queue_t queue, uint64_t version_flags, uint64_t endpoint_flags);
xpc_object_t xpc_remote_connection_create_with_remote_service(const void* service,
	dispatch_queue_t queue, uint64_t flags);
xpc_object_t xpc_remote_connection_create_remote_service_listener(const char* name,
	dispatch_queue_t queue, uint64_t flags);
void xpc_remote_connection_create_connected_pair(xpc_object_t* first, xpc_object_t* second,
	dispatch_queue_t queue, uint64_t version_flags, uint64_t endpoint_flags);
void xpc_remote_connection_set_target_queue(xpc_object_t connection,
	dispatch_queue_t queue);
void xpc_remote_connection_set_event_handler(xpc_object_t connection,
	void (^handler)(xpc_object_t event));
void xpc_remote_connection_activate(xpc_object_t connection);
void xpc_remote_connection_send_message(xpc_object_t connection,
	xpc_object_t message);
void xpc_remote_connection_send_message_with_reply(xpc_object_t connection,
	xpc_object_t message, dispatch_queue_t reply_queue, void (^handler)(xpc_object_t reply));
xpc_object_t xpc_remote_connection_send_message_with_reply_sync(
	xpc_object_t connection, xpc_object_t message);
void xpc_remote_connection_cancel(xpc_object_t connection);
uint64_t xpc_remote_connection_get_version_flags(void);

#ifdef __cplusplus
}
#endif

#endif
