/*
 * xr_wire -- the RemoteXPC frame codec, as measured from a real implementation.
 *
 * WHY THIS FILE EXISTS AND WHERE ITS FACTS COME FROM. The provider Darling must supply for
 * /System/Library/PrivateFrameworks/RemoteXPC.framework is the low-level XPC transport behind
 * xpc_remote_connection_*. Its on-wire format is not in any Apple header. The reference used here is
 * the published, working implementation in pymobiledevice3, pinned at commit
 * cae019164ce7684647138dc2c4b8f73f54c0aac4 (closed over in the manifest repository as
 * evidence/remote-xpc-reference-src-cae01916/{xpc_message.py,remotexpc.py}), whose encoder and
 * decoder this file reproduces byte for byte:
 *
 *   frame   : u32 magic 0x29B00B92, u32 flags, u64 payload_size (0 when there is no payload,
 *             otherwise 8 + encoded top-level object), u64 message_id
 *   payload : u32 magic 0x42133742, u32 protocol_version 5, then the top-level object
 *   object  : u32 type tag, then the type's payload (layout below)
 *
 * MEASURED ENCODINGS (all little-endian; a 4-byte aligned block is aligned to the frame's payload
 * object stream and padded to a 4-byte boundary on both sides where noted):
 *
 *   0x1000 NULL       no payload                     0x2000 BOOL      u32 raw word (0/1)
 *   0x3000 INT64      i64                            0x4000 UINT64    u64
 *   0x5000 DOUBLE     f64                            0x7000 DATE      i64 nanoseconds since 1970
 *   0x8000 DATA       align4, u32 length, bytes, pad4
 *   0x9000 STRING     align4, u32 length (bytes incl. the NUL), bytes, pad4
 *   0xA000 UUID       16 raw bytes                   0xB000 FD        u32 descriptor number
 *   0xE000 ARRAY      u32 count, count objects back to back (each aligns itself)
 *   0xF000 DICTIONARY u32 count, then per entry: align4, key as a NUL-terminated C string, object
 *
 * WHERE IT SITS. This is the XPC wrapper layer. A real RemoteXPC connection carries these wrappers in
 * HTTP/2 DATA frames (see xr_h2.h): the completed native wire capture of 2026-10-25 shows exactly that
 * for RemoteXPC 3102.160.5, and the four wrappers it captured (initial empty dictionary,
 * INIT_HANDSHAKE, one request, one reply) are reproduced byte for byte by this codec.
 *
 * WHAT THIS FILE IS NOT. It does not decide which frames a connection emits or when, how a reply is
 * correlated, or what a cancelled connection reports; those are the connection's decisions and come
 * from the same capture, not from inference here.
 *
 * The value tree (xr_value) is deliberately independent of libxpc so the codec can be compiled and
 * cross-checked against the reference on the host; the guest-facing conversion between xr_value and
 * real xpc objects lives in the transport, not here.
 */

#ifndef XR_WIRE_H
#define XR_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XR_WRAPPER_MAGIC 0x29B00B92u
#define XR_PAYLOAD_MAGIC 0x42133742u
#define XR_PAYLOAD_VERSION 5u

/* The frame's own flag word. Only the names the measured codec uses are given. */
#define XR_FLAG_ALWAYS_SET 0x00000001u
#define XR_FLAG_PING 0x00000002u
#define XR_FLAG_DATA_PRESENT 0x00000100u
#define XR_FLAG_WANTING_REPLY 0x00010000u
#define XR_FLAG_REPLY 0x00020000u
#define XR_FLAG_INIT_HANDSHAKE 0x00400000u

/* Object type tags. */
#define XR_TYPE_NULL 0x00001000u
#define XR_TYPE_BOOL 0x00002000u
#define XR_TYPE_INT64 0x00003000u
#define XR_TYPE_UINT64 0x00004000u
#define XR_TYPE_DOUBLE 0x00005000u
#define XR_TYPE_DATE 0x00007000u
#define XR_TYPE_DATA 0x00008000u
#define XR_TYPE_STRING 0x00009000u
#define XR_TYPE_UUID 0x0000A000u
#define XR_TYPE_FD 0x0000B000u
#define XR_TYPE_ARRAY 0x0000E000u
#define XR_TYPE_DICTIONARY 0x0000F000u

typedef enum {
	XR_NULL = 0,
	XR_BOOL,
	XR_INT64,
	XR_UINT64,
	XR_DOUBLE,
	XR_DATE,
	XR_DATA,
	XR_STRING,
	XR_UUID,
	XR_FD,
	XR_ARRAY,
	XR_DICTIONARY
} xr_kind;

typedef struct xr_value xr_value;

struct xr_value {
	xr_kind kind;
	union {
		uint32_t boolean;
		int64_t i64;
		uint64_t u64;
		double f64;
		struct {
			const unsigned char *bytes;
			uint32_t len;
		} bytes; /* DATA, STRING (NUL included in len), UUID (16 bytes) */
		struct {
			const xr_value *const *items;
			uint32_t count;
		} array;
		struct {
			const char *const *keys;
			const xr_value *const *values;
			uint32_t count;
		} dict;
		uint32_t fd;
	} u;
};

typedef struct {
	unsigned char *data;
	size_t len;
	size_t cap;
	bool failed;
} xr_buffer;

/* A parsed frame. `payload` is NULL when the frame carries none. The object tree is allocated with
 * malloc/realloc and released by xr_value_free. */
typedef struct {
	uint32_t flags;
	uint64_t message_id;
	xr_value *payload;
} xr_frame;

/* Encode one frame. `payload` may be NULL, which produces a frame whose payload size is zero.
 * Returns a malloc'd buffer in *out and its length, or 0 on failure. */
size_t xr_frame_encode(uint32_t flags, uint64_t message_id, const xr_value *payload,
	unsigned char **out);

/* Parse one frame from the front of `data`. On success returns true, stores the number of bytes the
 * frame occupied in *consumed and fills *frame. A frame that is not yet complete returns false with
 * *consumed set to 0 and *need_more true; a frame that is malformed returns false with *need_more
 * false. The caller releases the payload with xr_value_free. */
bool xr_frame_parse(const unsigned char *data, size_t len, size_t *consumed, xr_frame *frame,
	bool *need_more);

void xr_value_free(xr_value *value);
void xr_buffer_free(xr_buffer *buffer);

/* Append raw bytes to a buffer, growing it; sets `failed` instead of aborting on allocation failure. */
void xr_buffer_append(xr_buffer *buffer, const void *bytes, size_t len);

#endif
