/*
 * xr_h2 -- see xr_h2.h. Constants and ordering come from the captured native streams.
 */

#include "xr_h2.h"

#include <string.h>

static void put_u24_be(xr_buffer *b, uint32_t v)
{
	unsigned char out[3] = { (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v };
	xr_buffer_append(b, out, sizeof(out));
}

static void put_u32_be(xr_buffer *b, uint32_t v)
{
	unsigned char out[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16),
		(unsigned char)(v >> 8), (unsigned char)v };
	xr_buffer_append(b, out, sizeof(out));
}

void xr_h2_write_frame(xr_buffer *out, uint8_t type, uint8_t flags, uint32_t stream_id,
	const void *payload, size_t len)
{
	put_u24_be(out, (uint32_t)len);
	xr_buffer_append(out, &type, 1);
	xr_buffer_append(out, &flags, 1);
	put_u32_be(out, stream_id & 0x7fffffffu);
	xr_buffer_append(out, payload, len);
}

void xr_h2_write_handshake(xr_buffer *out, const void *initial_wrapper, size_t initial_len,
	const void *init_handshake_wrapper, size_t init_handshake_len)
{
	xr_buffer_append(out, XR_H2_PREFACE, XR_H2_PREFACE_LEN);

	{
		/* max_concurrent_streams = 100, initial_window_size = 1048576 */
		unsigned char settings[12] = {
			0x00, 0x03, 0x00, 0x00, 0x00, 0x64,
			0x00, 0x04, 0x00, 0x10, 0x00, 0x00,
		};
		xr_h2_write_frame(out, XR_H2_SETTINGS, 0, 0, settings, sizeof(settings));
	}
	{
		unsigned char increment[4] = {
			(unsigned char)(XR_H2_WINDOW_INCREMENT >> 24), (unsigned char)(XR_H2_WINDOW_INCREMENT >> 16),
			(unsigned char)(XR_H2_WINDOW_INCREMENT >> 8), (unsigned char)XR_H2_WINDOW_INCREMENT,
		};
		xr_h2_write_frame(out, XR_H2_WINDOW_UPDATE, 0, 0, increment, sizeof(increment));
	}
	xr_h2_write_frame(out, XR_H2_HEADERS, XR_H2_FLAG_END_HEADERS, XR_H2_ROOT_STREAM, NULL, 0);
	xr_h2_write_frame(out, XR_H2_DATA, 0, XR_H2_ROOT_STREAM, initial_wrapper, initial_len);
	xr_h2_write_frame(out, XR_H2_HEADERS, XR_H2_FLAG_END_HEADERS, XR_H2_REPLY_STREAM, NULL, 0);
	xr_h2_write_frame(out, XR_H2_DATA, 0, XR_H2_REPLY_STREAM, init_handshake_wrapper,
		init_handshake_len);
	xr_h2_write_frame(out, XR_H2_SETTINGS, XR_H2_FLAG_ACK, 0, NULL, 0);
}
