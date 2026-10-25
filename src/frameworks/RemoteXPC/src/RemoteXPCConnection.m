/*
 * RemoteXPC.framework -- the low-level remote XPC connection.
 *
 * WHAT THIS IMAGE IS. /System/Library/PrivateFrameworks/RemoteXPC.framework, the provider of the
 * xpc_remote_connection_* family. Its consumers in this tree are measured, not assumed:
 *
 *   CoreDevice: create_with_connected_fd, set_event_handler, activate, send_message,
 *               send_message_with_reply, send_message_with_reply_sync
 *   Mercury   : the same six plus cancel, create_connected_pair, create_remote_service_listener,
 *               create_with_remote_service, get_version_flags, set_target_queue,
 *               __xpc_type_remote_connection and the class OS_xpc_remote_connection
 *
 * The exported connected-fd entry is four arguments, as disassembled from the real provider:
 *
 *   id xpc_remote_connection_create_with_connected_fd(int fd, dispatch_queue_t queue,
 *                                                      uint64_t version_flags,
 *                                                      uint64_t endpoint_flags)
 *
 * and it returns OWNED/+1 (the pair constructor autoreleases each of its own returns; the export does
 * not). The fd is stored as given -- no dup -- and endpoint_flags bit 0 selects the even-numbered
 * listener role and starts the message counter at 2 instead of 1.
 *
 * WHAT IS IMPLEMENTED HERE, AND WHAT IS NOT YET. The transport is implemented over the two verified
 * layers: xr_wire (XPC wrapper codec) and xr_h2 (HTTP/2 framing), both cross-checked byte for byte
 * against the real provider's captured streams. create_with_remote_service and
 * create_remote_service_listener are NOT implemented in this revision: their argument lists and
 * semantics are being measured from the native provider, and a guessed body would be worse than an
 * absent one. This image must not be deployed as a complete provider until they are present.
 *
 * Identity: this is a genuine libxpc object. The class below subclasses libxpc's OS_xpc_object and
 * defines __xpc_type_remote_connection as the class object, so xpc_get_type() on a connection returns
 * the type Mercury compares against.
 */

#import <xpc/objects/base.h>
#import <xpc/util.h>
#import <xpc/objects/dictionary.h>
#include <xpc/private.h>

#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <Block.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "RemoteXPCConnection.h"
#include "xr_wire.h"

#include <pthread.h>
#include <nghttp2/nghttp2.h>
#include <sys/socket.h>
#include <xpc/xpc.h>

/* The version flags the real provider reports (-get_version_flags) and the RSD peers use. */
#define REMOTE_XPC_VERSION_FLAGS 0x0100000000000006ULL
/* endpoint_flags bit 0: this endpoint is the listener (even message ids). */
#define REMOTE_XPC_ENDPOINT_LISTENER 0x1ULL

struct xpc_remote_connection_s {
	struct xpc_object_s base;
	int fd;
	dispatch_queue_t target_queue;
	dispatch_queue_t handler_queue;
	dispatch_queue_t reply_queue;
	void *event_handler;
	void *pending_replies; /* CFMutableDictionary keyed by message id, or NULL */
	dispatch_source_t read_source;
	uint64_t version_flags;
	uint64_t endpoint_flags;
	uint64_t next_message_id;
	unsigned char* input;
	size_t input_len;
	size_t input_cap;
	unsigned char* wrapper_bytes;
	size_t wrapper_len;
	size_t wrapper_cap;
	nghttp2_session* session;
	bool is_server;
	bool stream1_answered;
	bool stream3_answered;
	void* pending_sends;
	bool preface_seen;
	bool activated;
	bool cancelled;
};

#define XR_STREAM_ROOT 1
#define XR_STREAM_REPLY 3

typedef struct send_pending_s {
	unsigned char* bytes;
	size_t length;
	size_t offset;
	struct send_pending_s* next;
} send_pending_t;

static void connection_release_source(struct xpc_remote_connection_s* this);

XPC_CLASS_DECL(remote_connection);

@interface XPC_CLASS_INTERFACE(remote_connection)
- (int)fileDescriptor;
- (void)setFileDescriptor: (int)fd;
- (uint64_t)versionFlags;
- (uint64_t)endpointFlags;
- (void)setVersionFlags: (uint64_t)flags;
- (void)setEndpointFlags: (uint64_t)flags;
@end

XPC_CLASS_SYMBOL_DECL(remote_connection);

OS_OBJECT_NONLAZY_CLASS
@implementation XPC_CLASS(remote_connection)

XPC_CLASS_HEADER(remote_connection);

- (char*)xpcDescription
{
	char* output = NULL;
	asprintf(&output, "<remote connection: fd %d>", XPC_THIS(remote_connection)->fd);
	return output;
}

- (void)dealloc
{
	XPC_THIS_DECL(remote_connection);
	/* The measured provider keeps the descriptor open immediately after cancel and releases it
	 * with the connection, so the single close belongs here, on the owning object's teardown. The
	 * source is torn down first: no handler may run while the descriptor is being closed, and the
	 * block's reference to this object is what kept the source alive. */
	connection_release_source(this);
	if (this->fd >= 0) {
		close(this->fd);
		this->fd = -1;
	}
	if (this->target_queue) {
		dispatch_release(this->target_queue);
		this->target_queue = NULL;
	}
	if (this->handler_queue) {
		dispatch_release(this->handler_queue);
		this->handler_queue = NULL;
	}
	if (this->event_handler) {
		Block_release(this->event_handler);
		this->event_handler = NULL;
	}
	nghttp2_session_del(this->session);
	this->session = NULL;
	send_pending_t* pending = this->pending_sends;
	while (pending) {
		send_pending_t* next = pending->next;
		free(pending->bytes);
		free(pending);
		pending = next;
	}
	free(this->input);
	free(this->wrapper_bytes);
	[super dealloc];
}

- (int)fileDescriptor { return XPC_THIS(remote_connection)->fd; }
- (void)setFileDescriptor: (int)fd { XPC_THIS(remote_connection)->fd = fd; }
- (uint64_t)versionFlags { return XPC_THIS(remote_connection)->version_flags; }
- (uint64_t)endpointFlags { return XPC_THIS(remote_connection)->endpoint_flags; }
- (void)setVersionFlags: (uint64_t)flags { XPC_THIS(remote_connection)->version_flags = flags; }
- (void)setEndpointFlags: (uint64_t)flags { XPC_THIS(remote_connection)->endpoint_flags = flags; }

@end

/* ---------------------------------------------------------------------------------------------
 * Value conversion between libxpc objects and the wire value tree.
 *
 * The wire codec (xr_wire) is the single authority for encoding; this file only walks an xpc object
 * into xr_value nodes and back, so no second copy of the encoding rules exists.
 * ------------------------------------------------------------------------------------------- */

static xr_value* xr_value_new(xr_kind kind)
{
	xr_value* value = calloc(1, sizeof(*value));
	if (value) {
		value->kind = kind;
	}
	return value;
}

static bool xr_value_from_xpc(xpc_object_t object, xr_value** out);

static bool xr_value_from_xpc_container(xpc_object_t object, xr_value* out, bool dictionary)
{
	const size_t count = dictionary ? xpc_dictionary_get_count(object) : xpc_array_get_count(object);
	if (dictionary) {
		const char** keys = calloc(count ? count : 1, sizeof(*keys));
		const xr_value** values = calloc(count ? count : 1, sizeof(*values));
		if (!keys || !values) {
			free(keys);
			free(values);
			return false;
		}
		out->u.dict.count = (uint32_t)count;
		out->u.dict.keys = keys;
		out->u.dict.values = values;
		__block size_t index = 0;
		__block bool ok = true;
		xpc_dictionary_apply(object, ^bool(const char* key, xpc_object_t value) {
			xr_value* child = NULL;
			if (!xr_value_from_xpc(value, &child)) {
				ok = false;
				return false;
			}
			keys[index] = strdup(key);
			((xr_value**)values)[index] = child;
			index++;
			return true;
		});
		return ok;
	}
	const xr_value** items = calloc(count ? count : 1, sizeof(*items));
	if (!items) {
		return false;
	}
	out->u.array.count = (uint32_t)count;
	out->u.array.items = items;
	for (size_t i = 0; i < count; i++) {
		xr_value* child = NULL;
		if (!xr_value_from_xpc(xpc_array_get_value(object, i), &child)) {
			return false;
		}
		((xr_value**)items)[i] = child;
	}
	return true;
}

static bool xr_value_from_xpc(xpc_object_t object, xr_value** out)
{
	const xpc_type_t type = xpc_get_type(object);
	xr_value* value = NULL;
	if (type == XPC_TYPE_DICTIONARY) {
		value = xr_value_new(XR_DICTIONARY);
		if (!value || !xr_value_from_xpc_container(object, value, true)) {
			xr_value_free(value);
			return false;
		}
	} else if (type == XPC_TYPE_ARRAY) {
		value = xr_value_new(XR_ARRAY);
		if (!value || !xr_value_from_xpc_container(object, value, false)) {
			xr_value_free(value);
			return false;
		}
	} else if (type == XPC_TYPE_STRING) {
		const char* string = xpc_string_get_string_ptr(object);
		value = xr_value_new(XR_STRING);
		if (!value) {
			return false;
		}
		const size_t len = strlen(string) + 1;
		unsigned char* copy = malloc(len);
		if (!copy) {
			free(value);
			return false;
		}
		memcpy(copy, string, len);
		value->u.bytes.bytes = copy;
		value->u.bytes.len = (uint32_t)len;
	} else if (type == XPC_TYPE_BOOL) {
		value = xr_value_new(XR_BOOL);
		if (value) {
			value->u.boolean = xpc_bool_get_value(object) ? 1 : 0;
		}
	} else if (type == XPC_TYPE_UINT64) {
		value = xr_value_new(XR_UINT64);
		if (value) {
			value->u.u64 = xpc_uint64_get_value(object);
		}
	} else if (type == XPC_TYPE_INT64) {
		value = xr_value_new(XR_INT64);
		if (value) {
			value->u.i64 = xpc_int64_get_value(object);
		}
	} else if (type == XPC_TYPE_DOUBLE) {
		value = xr_value_new(XR_DOUBLE);
		if (value) {
			value->u.f64 = xpc_double_get_value(object);
		}
	} else if (type == XPC_TYPE_DATA) {
		const void* bytes = xpc_data_get_bytes_ptr(object);
		const size_t len = xpc_data_get_length(object);
		value = xr_value_new(XR_DATA);
		if (!value) {
			return false;
		}
		unsigned char* copy = malloc(len ? len : 1);
		if (!copy) {
			free(value);
			return false;
		}
		memcpy(copy, bytes, len);
		value->u.bytes.bytes = copy;
		value->u.bytes.len = (uint32_t)len;
	} else if (type == XPC_TYPE_UUID) {
		value = xr_value_new(XR_UUID);
		if (!value) {
			return false;
		}
		unsigned char* copy = malloc(16);
		if (!copy) {
			free(value);
			return false;
		}
		memcpy(copy, xpc_uuid_get_bytes(object), 16);
		value->u.bytes.bytes = copy;
		value->u.bytes.len = 16;
	} else if (type == XPC_TYPE_DATE) {
		value = xr_value_new(XR_DATE);
		if (value) {
			value->u.i64 = (int64_t)xpc_date_get_value(object);
		}
	} else if (type == XPC_TYPE_NULL) {
		value = xr_value_new(XR_NULL);
	} else {
		/* A type the measured codec has no encoding for. Refuse the message rather than emit a
		 * frame the peer cannot parse. */
		return false;
	}
	if (!value) {
		return false;
	}
	*out = value;
	return true;
}

static xpc_object_t xr_value_to_xpc(const xr_value* value)
{
	switch (value->kind) {
	case XR_DICTIONARY: {
		xpc_object_t dictionary = xpc_dictionary_create(NULL, NULL, 0);
		for (uint32_t i = 0; i < value->u.dict.count; i++) {
			xpc_object_t child = xr_value_to_xpc(value->u.dict.values[i]);
			if (!child) {
				continue;
			}
			xpc_dictionary_set_value(dictionary, value->u.dict.keys[i], child);
			xpc_release(child);
		}
		return dictionary;
	}
	case XR_ARRAY: {
		xpc_object_t array = xpc_array_create(NULL, 0);
		for (uint32_t i = 0; i < value->u.array.count; i++) {
			xpc_object_t child = xr_value_to_xpc(value->u.array.items[i]);
			if (!child) {
				continue;
			}
			xpc_array_append_value(array, child);
			xpc_release(child);
		}
		return array;
	}
	case XR_STRING:
		return xpc_string_create((const char*)value->u.bytes.bytes);
	case XR_BOOL:
		return xpc_bool_create(value->u.boolean != 0);
	case XR_UINT64:
		return xpc_uint64_create(value->u.u64);
	case XR_INT64:
		return xpc_int64_create(value->u.i64);
	case XR_DOUBLE:
		return xpc_double_create(value->u.f64);
	case XR_DATA:
		return xpc_data_create(value->u.bytes.bytes, value->u.bytes.len);
	case XR_UUID:
		return xpc_uuid_create(value->u.bytes.bytes);
	case XR_DATE:
		return xpc_date_create(value->u.i64);
	case XR_FD:
		/* A wire FD number names a descriptor in the sender's namespace. Without a real descriptor
		 * transfer mapping it must not become a local descriptor; reject rather than fabricate. */
		return NULL;
	case XR_NULL:
	default:
		return xpc_null_create();
	}
}

/* ---------------------------------------------------------------------------------------------
 * Transport.
 * ------------------------------------------------------------------------------------------- */

#include <stdarg.h>

static int g_trace_enabled = -1;
static int g_trace_lines;
static void xr_tracef(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void xr_tracef(const char* format, ...)
{
	if (g_trace_enabled < 0) {
		g_trace_enabled = getenv("XRP_TRACE") != NULL;
	}
	if (!g_trace_enabled) {
		return;
	}
	if (__sync_fetch_and_add(&g_trace_lines, 1) >= 200) {
		return;
	}
	char buffer[192];
	va_list args;
	va_start(args, format);
	const int length = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (length > 0) {
		write(2, buffer, (size_t)(length < (int)sizeof(buffer) ? length : (int)sizeof(buffer) - 1));
		write(2, "\n", 1); /* one bounded line; the newline is part of the same probe record */
	}
}
#define xr_trace(text) xr_tracef("%s", (text))

/* Releases the read source and the reference its event block holds on the connection. After this the
 * source can never invoke the handler again, so no read is in flight when the fd is closed. */
static void connection_release_source(struct xpc_remote_connection_s* this)
{
	if (!this->read_source) {
		return;
	}
	dispatch_source_t source = this->read_source;
	this->read_source = NULL;
	dispatch_source_set_event_handler(source, NULL);
	dispatch_source_cancel(source);
	dispatch_release(source);
}

static bool write_all(int fd, const void* bytes, size_t len);

static void connection_handle_wrapper_bytes(id connection, int32_t stream, const unsigned char* bytes, size_t length);

static ssize_t session_send_callback(nghttp2_session* session, const uint8_t* data, size_t length,
	int flags, void* user_data)
{
	(void)session;
	(void)flags;
	id connection = (id)user_data;
	struct xpc_remote_connection_s* this = (struct xpc_remote_connection_s*)connection;
	if (!write_all(this->fd, data, length)) {
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}
	return (ssize_t)length;
}

static int session_data_chunk_callback(nghttp2_session* session, uint8_t flags, int32_t stream_id,
	const uint8_t* data, size_t length, void* user_data)
{
	(void)session;
	(void)flags;
	(void)stream_id;
	connection_handle_wrapper_bytes((id)user_data, stream_id, data, length);
	return 0;
}

static int session_frame_callback(nghttp2_session* session, const nghttp2_frame* frame,
	void* user_data);

static int session_invalid_frame_callback(nghttp2_session* session, const nghttp2_frame* frame,
	int lib_error_code, void* user_data)
{
	(void)session;
	(void)user_data;
	xr_tracef("N-invalid-frame type=%d error=%d stream=%d", frame ? (int)frame->hd.type : -1,
		lib_error_code, frame ? frame->hd.stream_id : -1);
	return 0;
}

static int session_frame_send_callback(nghttp2_session* session, const nghttp2_frame* frame,
	void* user_data)
{
	(void)session;
	(void)user_data;
	xr_tracef("N-frame-send type=%d stream=%d flags=0x%x len=%zu", (int)frame->hd.type,
		frame->hd.stream_id, (unsigned)frame->hd.flags, (size_t)frame->hd.length);
	return 0;
}

static int session_frame_not_send_callback(nghttp2_session* session, const nghttp2_frame* frame,
	int lib_error_code, void* user_data)
{
	(void)session;
	(void)user_data;
	xr_tracef("N-frame-not-sent type=%d stream=%d code=%d", (int)frame->hd.type,
		frame->hd.stream_id, lib_error_code);
	return 0;
}

static int session_stream_close_callback(nghttp2_session* session, int32_t stream_id,
	uint32_t error_code, void* user_data)
{
	(void)session;
	(void)user_data;
	xr_tracef("N-stream-close stream=%d error=%u", stream_id, error_code);
	return 0;
}

static ssize_t pending_read_callback(nghttp2_session* session, int32_t stream_id, uint8_t* buf,
	size_t length, uint32_t* data_flags, nghttp2_data_source* source, void* user_data)
{
	(void)session;
	(void)stream_id;
	(void)user_data;
	send_pending_t* pending = source->ptr;
	size_t remaining = pending->length - pending->offset;
	if (remaining > length) {
		remaining = length;
	}
	memcpy(buf, pending->bytes + pending->offset, remaining);
	pending->offset += remaining;
	if (pending->offset == pending->length) {
		/* Channels 1 and 3 stay open: the measured native DATA frames carry no END_STREAM. EOF
		 * alone would make nghttp2 close the stream after the body. */
		*data_flags |= NGHTTP2_DATA_FLAG_EOF | NGHTTP2_DATA_FLAG_NO_END_STREAM;
		pending->bytes = NULL; /* freed with the session's source by the connection */
	}
	return (ssize_t)remaining;
}

static void connection_submit_wrapper(id connection, uint32_t flags, uint64_t message_id,
	const xr_value* payload, int32_t stream, bool flush);

static int session_frame_callback(nghttp2_session* session, const nghttp2_frame* frame,
	void* user_data)
{
	(void)session;
	id connection = (id)user_data;
	struct xpc_remote_connection_s* this = (struct xpc_remote_connection_s*)connection;
	if (!this->is_server || !frame || frame->hd.type != NGHTTP2_HEADERS) {
		return 0;
	}
	const int32_t stream = frame->hd.stream_id;
	const uint32_t init_flags = (stream == XR_STREAM_REPLY)
		? (XR_FLAG_ALWAYS_SET | XR_FLAG_INIT_HANDSHAKE) : XR_FLAG_ALWAYS_SET;
	bool* answered = (stream == XR_STREAM_REPLY) ? &this->stream3_answered
		: (stream == XR_STREAM_ROOT ? &this->stream1_answered : NULL);
	if (!answered || *answered) {
		return 0;
	}
	*answered = true;
	/* The client opened this channel; the server answers with empty HEADERS and the channel's
	 * initialization wrapper, which is the order the captured peer used. */
	const int hrc = nghttp2_submit_headers(session, NGHTTP2_FLAG_END_HEADERS, stream, NULL, NULL, 0,
		NULL);
	xr_tracef("N-response-headers stream=%d rc=%d", stream, hrc);
	if (stream == XR_STREAM_REPLY) {
		connection_submit_wrapper(connection, init_flags, 0, NULL, stream, false);
	} else {
		xr_value empty = { .kind = XR_DICTIONARY };
		connection_submit_wrapper(connection, init_flags, 0, &empty, stream, false);
	}
	const int src = nghttp2_session_send(session);
	xr_tracef("N-frame-flush rc=%d", src);
	return 0;
}

static struct xpc_remote_connection_s* xr_state(id object)
{
	return (struct xpc_remote_connection_s*)object;
}

typedef struct pending_reply_s {
	uint64_t message_id;
	dispatch_queue_t queue;
	void (^handler)(xpc_object_t);
	void* semaphore;
	xpc_object_t* slot;
	struct pending_reply_s* next;
} pending_reply_t;

static pthread_mutex_t g_pending_lock = PTHREAD_MUTEX_INITIALIZER;

static bool write_all(int fd, const void* bytes, size_t len)
{
	const unsigned char* cursor = bytes;
	while (len > 0) {
		ssize_t written = write(fd, cursor, len);
		if (written < 0) {
			if (errno == EINTR) {
				continue;
			}
			return false;
		}
		cursor += written;
		len -= (size_t)written;
	}
	return true;
}

static void connection_submit_wrapper(id connection, uint32_t flags, uint64_t message_id,
	const xr_value* payload, int32_t stream, bool flush)
{
	struct xpc_remote_connection_s* this = xr_state(connection);
	unsigned char* wrapper = NULL;
	const size_t wrapper_len = xr_frame_encode(flags, message_id, payload, &wrapper);
	if (!wrapper_len) {
		return;
	}
	send_pending_t* pending = calloc(1, sizeof(*pending));
	if (!pending) {
		free(wrapper);
		return;
	}
	pending->bytes = wrapper;
	pending->length = wrapper_len;
	pending->next = this->pending_sends;
	this->pending_sends = pending;
	nghttp2_data_provider provider;
	provider.source.ptr = pending;
	provider.read_callback = pending_read_callback;
	const int drc = nghttp2_submit_data(this->session, NGHTTP2_FLAG_NONE, stream, &provider);
	xr_tracef("N-submit-data stream=%d rc=%d", stream, drc);
	if (flush) {
		const int src = nghttp2_session_send(this->session);
		xr_tracef("N-send rc=%d", src);
	}
}

/* Opens a client channel with HEADERS and its first DATA in one submission: submit_data on a stream
 * that exists only in the outbound queue returns NGHTTP2_ERR_STREAM_CLOSED (-510). */
static int32_t connection_submit_request(id connection, uint32_t flags, uint64_t message_id,
	const xr_value* payload, bool flush)
{
	struct xpc_remote_connection_s* this = xr_state(connection);
	unsigned char* wrapper = NULL;
	const size_t wrapper_len = xr_frame_encode(flags, message_id, payload, &wrapper);
	if (!wrapper_len) {
		return NGHTTP2_ERR_INVALID_ARGUMENT;
	}
	send_pending_t* pending = calloc(1, sizeof(*pending));
	if (!pending) {
		free(wrapper);
		return NGHTTP2_ERR_NOMEM;
	}
	pending->bytes = wrapper;
	pending->length = wrapper_len;
	pending->next = this->pending_sends;
	this->pending_sends = pending;
	nghttp2_data_provider provider;
	provider.source.ptr = pending;
	provider.read_callback = pending_read_callback;
	const int32_t stream = nghttp2_submit_request(this->session, NULL, NULL, 0, &provider, NULL);
	xr_tracef("N-submit-request rc=%d", stream);
	if (flush) {
		const int src = nghttp2_session_send(this->session);
		xr_tracef("N-send rc=%d", src);
	}
	return stream;
}

static void connection_write_wrapper(id connection, uint32_t flags, uint64_t message_id,
	const xr_value* payload, uint32_t stream)
{
	connection_submit_wrapper(connection, flags, message_id, payload, (int32_t)stream, true);
}

static void deliver_event(id connection, xpc_object_t event)
{
	struct xpc_remote_connection_s* this = xr_state(connection);
	if (!this->event_handler || !this->handler_queue) {
		xr_trace("P6-deliver-missing");
		return;
	}
	void (^handler)(xpc_object_t) = this->event_handler;
	/* The caller releases its reference after this call returns; the delivered block must own one
	 * of its own for as long as it holds the object. */
	xpc_retain(event);
	xr_trace("P6-deliver-queued");
	dispatch_async(this->handler_queue, ^{
		handler(event);
		xpc_release(event);
	});
}

static void dispatch_reply(id connection, uint64_t message_id, xpc_object_t reply)
{
	pthread_mutex_lock(&g_pending_lock);
	pending_reply_t* entry = NULL;
	/* The pending entries live in the connection's own list; the lock only guards list integrity
	 * against the reader thread and senders. */
	pending_reply_t** cursor = (pending_reply_t**)&xr_state(connection)->pending_replies;
	while (*cursor) {
		if ((*cursor)->message_id == message_id) {
			entry = *cursor;
			*cursor = entry->next;
			break;
		}
		cursor = &(*cursor)->next;
	}
	pthread_mutex_unlock(&g_pending_lock);
	if (!entry) {
		return;
	}
	if (entry->slot) {
		xpc_retain(reply);
		*entry->slot = reply;
	}
	if (entry->semaphore) {
		dispatch_semaphore_signal((dispatch_semaphore_t)entry->semaphore);
	}
	if (entry->handler) {
		void (^handler)(xpc_object_t) = entry->handler;
		dispatch_async(entry->queue, ^{
			handler(reply);
		});
	}
	free(entry);
}

static void connection_handle_data(id connection, const unsigned char* bytes, size_t len)
{
	size_t consumed = 0;
	xr_frame frame = { 0 };
	bool need_more = false;
	if (!xr_frame_parse(bytes, len, &consumed, &frame, &need_more) || !frame.payload) {
		return;
	}
	xpc_object_t object = xr_value_to_xpc(frame.payload);
	const bool is_reply = (frame.flags & XR_FLAG_REPLY) != 0;
	xr_value_free(frame.payload);
	if (!object) {
		return;
	}
	if (is_reply) {
		dispatch_reply(connection, frame.message_id, object);
	} else {
		/* A request that wants a reply carries no mach send-once right: record the connection it
		 * arrived on and its message id, which is what xpc_dictionary_create_reply() and the reply
		 * send path below need. */
		if ((frame.flags & XR_FLAG_WANTING_REPLY) && xpc_get_type(object) == XPC_TYPE_DICTIONARY) {
			XPC_CLASS(dictionary)* dictionary = (XPC_CLASS(dictionary)*)object;
			_xpc_dictionary_set_remote_connection(object, connection);
			_xpc_dictionary_set_remote_reply_msg_id(object, frame.message_id);
			dictionary.remoteReplyExpected = true;
		}
		deliver_event(connection, object);
	}
	xpc_release(object);
}

static void connection_handle_wrapper_bytes(id connection, int32_t stream, const unsigned char* bytes, size_t length)
{
	struct xpc_remote_connection_s* this = xr_state(connection);
	size_t available = this->wrapper_len + length;
	if (available > this->wrapper_cap) {
		size_t cap = this->wrapper_cap ? this->wrapper_cap : 16384;
		while (cap < available) {
			cap *= 2;
		}
		unsigned char* grown = realloc(this->wrapper_bytes, cap);
		if (!grown) {
			return;
		}
		this->wrapper_bytes = grown;
		this->wrapper_cap = cap;
	}
	memcpy(this->wrapper_bytes + this->wrapper_len, bytes, length);
	this->wrapper_len += length;
	size_t consumed = 0;
	xr_frame frame_value = { 0 };
	bool need_more = false;
	for (;;) {
		if (!xr_frame_parse(this->wrapper_bytes, this->wrapper_len, &consumed, &frame_value,
			&need_more)) {
			break;
		}
		this->wrapper_len -= consumed;
		memmove(this->wrapper_bytes, this->wrapper_bytes + consumed, this->wrapper_len);
		if (!frame_value.payload) {
			continue;
		}
		/* Handshake wrappers carry neither INIT_HANDSHAKE nor DATA_PRESENT: they belong to the
		 * connection, not to the application. A valid empty application dictionary does carry
		 * DATA_PRESENT and is still delivered. */
		xr_tracef("N-wrapper stream=%d flags=0x%x id=%llu", stream, frame_value.flags,
			(unsigned long long)frame_value.message_id);
		if ((frame_value.flags & XR_FLAG_INIT_HANDSHAKE)
			|| !(frame_value.flags & XR_FLAG_DATA_PRESENT)) {
			xr_tracef("N-handshake-wrapper flags=0x%x id=%llu", frame_value.flags,
				(unsigned long long)frame_value.message_id);
			xr_value_free(frame_value.payload);
			continue;
		}
		xpc_object_t object = xr_value_to_xpc(frame_value.payload);
		const bool is_reply = (frame_value.flags & XR_FLAG_REPLY) != 0;
		xr_value_free(frame_value.payload);
		if (!object) {
			continue;
		}
		if (is_reply) {
			dispatch_reply(connection, frame_value.message_id, object);
		} else {
			if ((frame_value.flags & XR_FLAG_WANTING_REPLY)
				&& xpc_get_type(object) == XPC_TYPE_DICTIONARY) {
				XPC_CLASS(dictionary)* dictionary = (XPC_CLASS(dictionary)*)object;
				_xpc_dictionary_set_remote_connection(object, connection);
				_xpc_dictionary_set_remote_reply_msg_id(object, frame_value.message_id);
				dictionary.remoteReplyExpected = true;
			}
			deliver_event(connection, object);
		}
		xpc_release(object);
	}
}

static void connection_read_source(id connection, dispatch_source_t source)
{
	struct xpc_remote_connection_s* this = xr_state(connection);
	xr_tracef("R-enter fd=%d flags=0x%x", this->fd, fcntl(this->fd, F_GETFL, 0));
	for (;;) {
		unsigned char chunk[16384];
		errno = 0;
		ssize_t got = recv(this->fd, chunk, sizeof(chunk), MSG_DONTWAIT);
		xr_tracef("R-ret got=%zd errno=%d", got, errno);
		if (got < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return;
			}
			xr_tracef("R-error errno=%d", errno);
			break;
		}
		if (got == 0) {
			break;
		}
		const ssize_t consumed = nghttp2_session_mem_recv(this->session, chunk, (size_t)got);
		xr_tracef("N-mem-recv got=%zd consumed=%zd", got, consumed);
		if (consumed < 0) {
			xr_tracef("R-session-error %zd", consumed);
			break;
		}
		/* Flush whatever the session now wants to send (SETTINGS ACK, window updates, replies). */
		nghttp2_session_send(this->session);
	}
	xr_tracef("R-exit fd=%d", this->fd);
	dispatch_source_cancel(source);
}

@implementation XPC_CLASS(remote_connection) (Transport)

- (void)activate
{
	XPC_THIS_DECL(remote_connection);
	if (this->activated || this->cancelled) {
		return;
	}
	this->activated = true;

	nghttp2_session_callbacks* callbacks = NULL;
	nghttp2_option* option = NULL;
	nghttp2_session_callbacks_new(&callbacks);
	nghttp2_session_callbacks_set_send_callback(callbacks, session_send_callback);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, session_data_chunk_callback);
	nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, session_frame_callback);
	nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(callbacks,
		session_invalid_frame_callback);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks,
		session_stream_close_callback);
	nghttp2_session_callbacks_set_on_frame_send_callback(callbacks, session_frame_send_callback);
	nghttp2_session_callbacks_set_on_frame_not_send_callback(callbacks,
		session_frame_not_send_callback);
	nghttp2_option_new(&option);
	/* Documented for a non-HTTP binary protocol: the channels are empty HEADERS plus DATA. This
	 * disables HTTP message semantics, not frame or TLS integrity. */
	nghttp2_option_set_no_http_messaging(option, 1);
	struct xpc_remote_connection_s* state = xr_state(self);
	state->is_server = (state->endpoint_flags & REMOTE_XPC_ENDPOINT_LISTENER) != 0;
	const int session_rc = state->is_server
		? nghttp2_session_server_new2(&this->session, callbacks, self, option)
		: nghttp2_session_client_new2(&this->session, callbacks, self, option);
	nghttp2_option_del(option);
	nghttp2_session_callbacks_del(callbacks);
	if (session_rc != 0 || !this->session) {
		xr_tracef("P2-session-failed rc=%d", session_rc);
		deliver_event(self, XPC_ERROR_CONNECTION_INVALID);
		return;
	}
	{
		nghttp2_settings_entry settings[2] = {
			{ NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100 },
			{ NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 1048576 },
		};
		nghttp2_submit_settings(this->session, NGHTTP2_FLAG_NONE, settings, 2);
	}
	nghttp2_submit_window_update(this->session, NGHTTP2_FLAG_NONE, 0, 983041);
	if (!state->is_server) {
		xr_value empty = { .kind = XR_DICTIONARY };
		connection_submit_request(self, XR_FLAG_ALWAYS_SET, 0, &empty, false);
		connection_submit_request(self, XR_FLAG_ALWAYS_SET | XR_FLAG_INIT_HANDSHAKE, 0, NULL, false);
	}
	xr_trace("P2-activate-write");
	nghttp2_session_send(this->session);

	dispatch_queue_t queue = this->target_queue ? this->target_queue : dispatch_get_main_queue();
	dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t)this->fd, 0,
		queue);
	if (!source) {
		return;
	}
	this->read_source = source;
	dispatch_source_set_event_handler(source, ^{
		connection_read_source(self, NULL);
	});
	dispatch_resume(source);
}

- (void)cancelConnection
{
	XPC_THIS_DECL(remote_connection);
	if (this->cancelled) {
		return;
	}
	this->cancelled = true;
	/* Measured: RST_STREAM with code 5 on both channels, then one terminal error per handler
	 * queue. The fd is released with the object, not synchronously here. */
	if (this->session) {
		nghttp2_submit_rst_stream(this->session, NGHTTP2_FLAG_NONE, XR_STREAM_ROOT,
			NGHTTP2_REFUSED_STREAM);
		nghttp2_submit_rst_stream(this->session, NGHTTP2_FLAG_NONE, XR_STREAM_REPLY,
			NGHTTP2_REFUSED_STREAM);
		nghttp2_session_send(this->session);
	}
	connection_release_source(this);
	deliver_event(self, XPC_ERROR_CONNECTION_INVALID);
}

- (void)sendMessage: (xpc_object_t)message wantingReply: (BOOL)wanting_reply
	handler: (void (^)(xpc_object_t))handler replyQueue: (dispatch_queue_t)reply_queue
	semaphore: (void*)semaphore slot: (xpc_object_t*)slot
{
	XPC_THIS_DECL(remote_connection);
	xr_value* payload = NULL;
	if (!xr_value_from_xpc(message, &payload)) {
		return;
	}
	uint64_t message_id = this->next_message_id;
	uint32_t stream = XR_STREAM_ROOT;
	/* A dictionary created by xpc_dictionary_create_reply() carries the connection the request
	 * arrived on and that request's message id: send it back as a REPLY on the reply channel with
	 * the same id, exactly as the measured provider did. */
	if (xpc_get_type(message) == XPC_TYPE_DICTIONARY) {
		XPC_CLASS(dictionary)* dictionary = (XPC_CLASS(dictionary)*)message;
		const uint64_t reply_id = _xpc_dictionary_get_remote_reply_msg_id(message);
		if ((xpc_object_t)dictionary.associatedConnection == self && dictionary.remoteIsReply
			&& reply_id != 0) {
			message_id = reply_id;
			stream = XR_STREAM_REPLY;
			uint32_t reply_flags = XR_FLAG_ALWAYS_SET | XR_FLAG_DATA_PRESENT | XR_FLAG_REPLY;
			connection_write_wrapper(self, reply_flags, message_id, payload, stream);
			xr_value_free(payload);
			return;
		}
	}
	this->next_message_id += 2;
	if (wanting_reply) {
		pending_reply_t* entry = calloc(1, sizeof(*entry));
		if (!entry) {
			xr_value_free(payload);
			return;
		}
		entry->message_id = message_id;
		entry->handler = handler ? Block_copy(handler) : NULL;
		if (reply_queue) {
			dispatch_retain(reply_queue);
			entry->queue = reply_queue;
		}
		entry->semaphore = semaphore;
		entry->slot = slot;
		pthread_mutex_lock(&g_pending_lock);
		entry->next = this->pending_replies;
		this->pending_replies = entry;
		pthread_mutex_unlock(&g_pending_lock);
	}
	uint32_t flags = XR_FLAG_ALWAYS_SET | XR_FLAG_DATA_PRESENT;
	if (wanting_reply) {
		flags |= XR_FLAG_WANTING_REPLY;
	}
	connection_write_wrapper(self, flags, message_id, payload, stream);
	xr_value_free(payload);
}

@end

/* ---------------------------------------------------------------------------------------------
 * Exported C entry points.
 * ------------------------------------------------------------------------------------------- */

XPC_EXPORT
xpc_object_t xpc_remote_connection_create_with_connected_fd(int fd, dispatch_queue_t queue,
	uint64_t version_flags, uint64_t endpoint_flags)
{
	XPC_CLASS(remote_connection)* connection = [[XPC_CLASS(remote_connection) alloc] init];
	struct xpc_remote_connection_s* this = xr_state(connection);
	this->fd = fd;
	if (queue) {
		dispatch_retain(queue);
		this->target_queue = queue;
	}
	this->version_flags = version_flags;
	this->endpoint_flags = endpoint_flags;
	this->next_message_id = (endpoint_flags & REMOTE_XPC_ENDPOINT_LISTENER) ? 2 : 1;
	xr_trace("P1-created");
	return connection;
}

XPC_EXPORT
void xpc_remote_connection_set_target_queue(xpc_object_t connection, dispatch_queue_t queue)
{
	TO_OBJC_CHECKED(remote_connection, connection, object) {
		struct xpc_remote_connection_s* this = xr_state(object);
		dispatch_queue_t previous = this->target_queue;
		if (queue) {
			dispatch_retain(queue);
		}
		this->target_queue = queue;
		if (previous) {
			dispatch_release(previous);
		}
	}
}

XPC_EXPORT
void xpc_remote_connection_set_event_handler(xpc_object_t connection, void (^handler)(xpc_object_t))
{
	TO_OBJC_CHECKED(remote_connection, connection, object) {
		struct xpc_remote_connection_s* this = xr_state(object);
		if (this->event_handler) {
			Block_release(this->event_handler);
		}
		this->event_handler = handler ? Block_copy(handler) : NULL;
		xr_trace(this->event_handler ? "P0-handler-set" : "P0-handler-null");
		/* Events and replies are delivered on the target queue, which is what the measured
		 * provider's endpoints were given. */
		dispatch_queue_t handler_queue = this->target_queue ? this->target_queue
			: dispatch_get_main_queue();
		dispatch_retain(handler_queue);
		if (this->handler_queue) {
			dispatch_release(this->handler_queue);
		}
		this->handler_queue = handler_queue;
	}
}

XPC_EXPORT
void xpc_remote_connection_activate(xpc_object_t connection)
{
	TO_OBJC_CHECKED(remote_connection, connection, object) {
		[object activate];
	}
}

XPC_EXPORT
void xpc_remote_connection_send_message(xpc_object_t connection, xpc_object_t message)
{
	TO_OBJC_CHECKED(remote_connection, connection, object) {
		[object sendMessage: message wantingReply: NO handler: NULL replyQueue: NULL semaphore: NULL
			slot: NULL];
	}
}

XPC_EXPORT
void xpc_remote_connection_send_message_with_reply(xpc_object_t connection, xpc_object_t message,
	dispatch_queue_t reply_queue, void (^handler)(xpc_object_t reply))
{
	TO_OBJC_CHECKED(remote_connection, connection, object) {
		[object sendMessage: message wantingReply: YES handler: handler replyQueue: reply_queue
			semaphore: NULL slot: NULL];
	}
}

XPC_EXPORT
xpc_object_t xpc_remote_connection_send_message_with_reply_sync(xpc_object_t connection,
	xpc_object_t message)
{
	__block xpc_object_t reply = NULL;
	TO_OBJC_CHECKED(remote_connection, connection, object) {
		dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
		[object sendMessage: message wantingReply: YES handler: NULL replyQueue: NULL
			semaphore: semaphore slot: &reply];
		dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
		dispatch_release(semaphore);
	}
	return reply;
}

XPC_EXPORT
void xpc_remote_connection_cancel(xpc_object_t connection)
{
	TO_OBJC_CHECKED(remote_connection, connection, object) {
		[object cancelConnection];
	}
}

XPC_EXPORT
uint64_t xpc_remote_connection_get_version_flags(void)
{
	return REMOTE_XPC_VERSION_FLAGS;
}
