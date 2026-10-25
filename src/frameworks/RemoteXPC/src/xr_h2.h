/*
 * xr_h2 -- the HTTP/2 framing a RemoteXPC connection puts on its connected fd.
 *
 * MEASURED, NOT DESIGNED. The completed native wire capture (evidence/
 * remote-xpc-native-wire-20261025ae-monitor, streams client-to-peer.bin and peer-to-client.bin) shows a
 * real RemoteXPC 3102.160.5 connection exchanging HTTP/2 cleartext with the XPC wrappers in xr_wire.h
 * carried in DATA frames. Every field and constant below is read from those bytes:
 *
 *   preface      24 bytes "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
 *   frame header length u24 big-endian, type u8, flags u8, stream id u32 big-endian (31 bits)
 *   SETTINGS     type 4; max_concurrent_streams (3) = 100, initial_window_size (4) = 1048576;
 *                an empty SETTINGS with ACK (1) answers the peer's settings
 *   WINDOW_UPDATE type 8, stream 0, increment 983041 (= 1048576 - 65535)
 *   HEADERS      type 1, flags END_HEADERS (4), no payload, one per channel (streams 1 and 3)
 *   DATA         type 0, no flags, one wrapper per frame in this exchange
 *   RST_STREAM   type 3, code 5 (REFUSED_STREAM), streams 1 and 3 at cancellation
 *
 * WHAT THIS LAYER DOES NOT DECIDE: when a connection emits which frame, what a reply means, or how a
 * cancelled connection reports itself. Those are the connection's, and are taken from the same capture.
 */

#ifndef XR_H2_H
#define XR_H2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xr_wire.h"

#define XR_H2_FRAME_HEADER_SIZE 9
#define XR_H2_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define XR_H2_PREFACE_LEN 24

#define XR_H2_DATA 0x0
#define XR_H2_HEADERS 0x1
#define XR_H2_RST_STREAM 0x3
#define XR_H2_SETTINGS 0x4
#define XR_H2_WINDOW_UPDATE 0x8

#define XR_H2_FLAG_END_STREAM 0x1
#define XR_H2_FLAG_ACK 0x1
#define XR_H2_FLAG_END_HEADERS 0x4

#define XR_H2_ROOT_STREAM 1
#define XR_H2_REPLY_STREAM 3

#define XR_H2_SETTINGS_MAX_CONCURRENT_STREAMS 100
#define XR_H2_SETTINGS_INITIAL_WINDOW_SIZE 1048576u
#define XR_H2_WINDOW_INCREMENT (XR_H2_SETTINGS_INITIAL_WINDOW_SIZE - 65535u)
#define XR_H2_REFUSED_STREAM 5

/* Append one frame whose payload is `payload` (may be NULL when len is 0). */
void xr_h2_write_frame(xr_buffer *out, uint8_t type, uint8_t flags, uint32_t stream_id,
	const void *payload, size_t len);

/* Append the frames a connection emits when it activates, in the measured order:
 * preface, SETTINGS, WINDOW_UPDATE, HEADERS(1), DATA(1, empty dictionary wrapper), HEADERS(3),
 * DATA(3, INIT_HANDSHAKE wrapper), SETTINGS ACK. The caller supplies the two wrappers so this layer
 * stays independent of the codec's value types. */
void xr_h2_write_handshake(xr_buffer *out, const void *initial_wrapper, size_t initial_len,
	const void *init_handshake_wrapper, size_t init_handshake_len);

#endif
