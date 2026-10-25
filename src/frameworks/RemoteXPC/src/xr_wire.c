/*
 * xr_wire -- see xr_wire.h for the measured wire format and its provenance.
 */

#include "xr_wire.h"

#include <stdlib.h>
#include <string.h>

static void put_u32(xr_buffer *b, uint32_t v)
{
	unsigned char out[4] = { (unsigned char)v, (unsigned char)(v >> 8),
		(unsigned char)(v >> 16), (unsigned char)(v >> 24) };
	if (b->failed) {
		return;
	}
	if (b->len + sizeof(out) > b->cap) {
		size_t cap = b->cap ? b->cap : 256;
		while (cap < b->len + sizeof(out)) {
			cap *= 2;
		}
		unsigned char *grown = realloc(b->data, cap);
		if (!grown) {
			b->failed = true;
			return;
		}
		b->data = grown;
		b->cap = cap;
	}
	memcpy(b->data + b->len, out, sizeof(out));
	b->len += sizeof(out);
}

static void put_u64(xr_buffer *b, uint64_t v)
{
	put_u32(b, (uint32_t)v);
	put_u32(b, (uint32_t)(v >> 32));
}

void xr_buffer_append(xr_buffer *b, const void *bytes, size_t len)
{
	if (b->failed || len == 0) {
		return;
	}
	if (b->len + len > b->cap) {
		size_t cap = b->cap ? b->cap : 256;
		while (cap < b->len + len) {
			cap *= 2;
		}
		unsigned char *grown = realloc(b->data, cap);
		if (!grown) {
			b->failed = true;
			return;
		}
		b->data = grown;
		b->cap = cap;
	}
	memcpy(b->data + b->len, bytes, len);
	b->len += len;
}

/* Pad to `align` relative to the start of the object stream, which begins at the top-level object. */
static void pad_to(xr_buffer *b, size_t base, size_t align)
{
	static const unsigned char zeros[8] = { 0 };
	while (!b->failed && (b->len - base) % align != 0) {
		xr_buffer_append(b, zeros, 1);
	}
}

static void write_object(xr_buffer *b, size_t base, const xr_value *v);

/* Fill in a length word once the bytes it counts are known. */
static void patch_u32(xr_buffer *b, size_t at, uint32_t v)
{
	if (b->failed) {
		return;
	}
	b->data[at] = (unsigned char)v;
	b->data[at + 1] = (unsigned char)(v >> 8);
	b->data[at + 2] = (unsigned char)(v >> 16);
	b->data[at + 3] = (unsigned char)(v >> 24);
}

static void write_blob(xr_buffer *b, size_t base, uint32_t tag, const unsigned char *bytes,
	uint32_t len)
{
	put_u32(b, tag);
	pad_to(b, base, 4);
	put_u32(b, len);
	xr_buffer_append(b, bytes, len);
	pad_to(b, base, 4);
}

static void write_object(xr_buffer *b, size_t base, const xr_value *v)
{
	switch (v->kind) {
	case XR_NULL:
		put_u32(b, XR_TYPE_NULL);
		break;
	case XR_BOOL:
		put_u32(b, XR_TYPE_BOOL);
		put_u32(b, v->u.boolean);
		break;
	case XR_INT64:
		put_u32(b, XR_TYPE_INT64);
		put_u64(b, (uint64_t)v->u.i64);
		break;
	case XR_UINT64:
		put_u32(b, XR_TYPE_UINT64);
		put_u64(b, v->u.u64);
		break;
	case XR_DOUBLE: {
		uint64_t bits;
		memcpy(&bits, &v->u.f64, sizeof(bits));
		put_u32(b, XR_TYPE_DOUBLE);
		put_u64(b, bits);
		break;
	}
	case XR_DATE:
		put_u32(b, XR_TYPE_DATE);
		put_u64(b, (uint64_t)v->u.i64);
		break;
	case XR_DATA:
		write_blob(b, base, XR_TYPE_DATA, v->u.bytes.bytes, v->u.bytes.len);
		break;
	case XR_STRING:
		write_blob(b, base, XR_TYPE_STRING, v->u.bytes.bytes, v->u.bytes.len);
		break;
	case XR_UUID:
		put_u32(b, XR_TYPE_UUID);
		xr_buffer_append(b, v->u.bytes.bytes, 16);
		break;
	case XR_FD:
		put_u32(b, XR_TYPE_FD);
		put_u32(b, v->u.fd);
		break;
	case XR_ARRAY: {
		put_u32(b, XR_TYPE_ARRAY);
		size_t length_field = b->len;
		put_u32(b, 0);
		put_u32(b, v->u.array.count);
		for (uint32_t i = 0; i < v->u.array.count; i++) {
			write_object(b, base, v->u.array.items[i]);
		}
		patch_u32(b, length_field, (uint32_t)(b->len - length_field - 4));
		break;
	}
	case XR_DICTIONARY: {
		put_u32(b, XR_TYPE_DICTIONARY);
		size_t length_field = b->len;
		put_u32(b, 0);
		put_u32(b, v->u.dict.count);
		for (uint32_t i = 0; i < v->u.dict.count; i++) {
			pad_to(b, base, 4);
			xr_buffer_append(b, v->u.dict.keys[i], strlen(v->u.dict.keys[i]) + 1);
			pad_to(b, base, 4);
			write_object(b, base, v->u.dict.values[i]);
		}
		patch_u32(b, length_field, (uint32_t)(b->len - length_field - 4));
		break;
	}
	}
}

size_t xr_frame_encode(uint32_t flags, uint64_t message_id, const xr_value *payload,
	unsigned char **out)
{
	xr_buffer b = { 0 };
	put_u32(&b, XR_WRAPPER_MAGIC);
	put_u32(&b, flags);
	size_t size_field = b.len;
	put_u64(&b, 0);
	put_u64(&b, message_id);
	if (payload) {
		size_t base = b.len; /* the object stream starts after the 8-byte payload header */
		put_u32(&b, XR_PAYLOAD_MAGIC);
		put_u32(&b, XR_PAYLOAD_VERSION);
		write_object(&b, base, payload);
		uint64_t size = b.len - 24;
		for (int i = 0; i < 8; i++) {
			b.data[size_field + i] = (unsigned char)(size >> (8 * i));
		}
	}
	if (b.failed) {
		free(b.data);
		return 0;
	}
	*out = b.data;
	return b.len;
}

/* ---- parsing ---- */

typedef struct {
	const unsigned char *data;
	size_t len;
	size_t pos;
	bool truncated;
	bool malformed;
} reader;

static bool need(reader *r, size_t n)
{
	if (r->pos + n > r->len) {
		r->truncated = true;
		return false;
	}
	return true;
}

static uint32_t read_u32(reader *r)
{
	if (!need(r, 4)) {
		return 0;
	}
	uint32_t v = (uint32_t)r->data[r->pos] | ((uint32_t)r->data[r->pos + 1] << 8)
		| ((uint32_t)r->data[r->pos + 2] << 16) | ((uint32_t)r->data[r->pos + 3] << 24);
	r->pos += 4;
	return v;
}

static uint64_t read_u64(reader *r)
{
	uint64_t lo = read_u32(r);
	uint64_t hi = read_u32(r);
	return lo | (hi << 32);
}

static void align_reader(reader *r, size_t base, size_t align)
{
	while ((r->pos - base) % align != 0) {
		if (!need(r, 1)) {
			return;
		}
		r->pos++;
	}
}

static xr_value *read_object(reader *r, size_t base);

static xr_value *new_value(xr_kind kind)
{
	xr_value *v = calloc(1, sizeof(*v));
	if (v) {
		v->kind = kind;
	}
	return v;
}

static xr_value *read_blob(reader *r, size_t base, xr_kind kind)
{
	align_reader(r, base, 4);
	uint32_t len = read_u32(r);
	if (r->truncated) {
		return NULL;
	}
	if (!need(r, len)) {
		return NULL;
	}
	xr_value *v = new_value(kind);
	if (!v) {
		r->malformed = true;
		return NULL;
	}
	unsigned char *copy = malloc(len ? len : 1);
	if (!copy) {
		free(v);
		r->malformed = true;
		return NULL;
	}
	memcpy(copy, r->data + r->pos, len);
	r->pos += len;
	align_reader(r, base, 4);
	v->u.bytes.bytes = copy;
	v->u.bytes.len = len;
	return v;
}

static xr_value *read_object(reader *r, size_t base)
{
	align_reader(r, base, 4); /* every object starts at a 4-byte boundary */
	uint32_t tag = read_u32(r);
	if (r->truncated) {
		return NULL;
	}
	switch (tag) {
	case XR_TYPE_NULL:
		return new_value(XR_NULL);
	case XR_TYPE_BOOL: {
		uint32_t raw = read_u32(r);
		xr_value *v = new_value(XR_BOOL);
		if (v) {
			v->u.boolean = raw;
		}
		return v;
	}
	case XR_TYPE_INT64:
	case XR_TYPE_DATE: {
		uint64_t raw = read_u64(r);
		xr_value *v = new_value(tag == XR_TYPE_INT64 ? XR_INT64 : XR_DATE);
		if (v) {
			v->u.i64 = (int64_t)raw;
		}
		return v;
	}
	case XR_TYPE_UINT64: {
		uint64_t raw = read_u64(r);
		xr_value *v = new_value(XR_UINT64);
		if (v) {
			v->u.u64 = raw;
		}
		return v;
	}
	case XR_TYPE_DOUBLE: {
		uint64_t raw = read_u64(r);
		xr_value *v = new_value(XR_DOUBLE);
		if (v) {
			memcpy(&v->u.f64, &raw, sizeof(raw));
		}
		return v;
	}
	case XR_TYPE_DATA:
		return read_blob(r, base, XR_DATA);
	case XR_TYPE_STRING:
		return read_blob(r, base, XR_STRING);
	case XR_TYPE_UUID: {
		if (!need(r, 16)) {
			return NULL;
		}
		xr_value *v = new_value(XR_UUID);
		if (!v) {
			r->malformed = true;
			return NULL;
		}
		unsigned char *copy = malloc(16);
		if (!copy) {
			free(v);
			r->malformed = true;
			return NULL;
		}
		memcpy(copy, r->data + r->pos, 16);
		r->pos += 16;
		v->u.bytes.bytes = copy;
		v->u.bytes.len = 16;
		return v;
	}
	case XR_TYPE_FD: {
		uint32_t fd = read_u32(r);
		xr_value *v = new_value(XR_FD);
		if (v) {
			v->u.fd = fd;
		}
		return v;
	}
	case XR_TYPE_ARRAY: {
		(void)read_u32(r); /* content length; the item count follows it */
		uint32_t count = read_u32(r);
		if (r->truncated) {
			return NULL;
		}
		xr_value *v = new_value(XR_ARRAY);
		if (!v) {
			r->malformed = true;
			return NULL;
		}
		xr_value **items = calloc(count ? count : 1, sizeof(*items));
		if (!items) {
			free(v);
			r->malformed = true;
			return NULL;
		}
		v->u.array.items = (const xr_value *const *)items;
		v->u.array.count = count;
		for (uint32_t i = 0; i < count; i++) {
			items[i] = read_object(r, base);
			if (!items[i]) {
				xr_value_free(v);
				return NULL;
			}
		}
		return v;
	}
	case XR_TYPE_DICTIONARY: {
		(void)read_u32(r); /* content length; the entry count follows it */
		uint32_t count = read_u32(r);
		if (r->truncated) {
			return NULL;
		}
		xr_value *v = new_value(XR_DICTIONARY);
		if (!v) {
			r->malformed = true;
			return NULL;
		}
		char **keys = calloc(count ? count : 1, sizeof(*keys));
		xr_value **values = calloc(count ? count : 1, sizeof(*values));
		if (!keys || !values) {
			free(keys);
			free(values);
			free(v);
			r->malformed = true;
			return NULL;
		}
		v->u.dict.keys = (const char *const *)keys;
		v->u.dict.values = (const xr_value *const *)values;
		v->u.dict.count = count;
		for (uint32_t i = 0; i < count; i++) {
			align_reader(r, base, 4);
			size_t start = r->pos;
			while (r->pos < r->len && r->data[r->pos] != 0) {
				r->pos++;
			}
			if (r->pos >= r->len) {
				r->truncated = true;
				xr_value_free(v);
				return NULL;
			}
			size_t key_len = r->pos - start;
			keys[i] = malloc(key_len + 1);
			if (!keys[i]) {
				r->malformed = true;
				xr_value_free(v);
				return NULL;
			}
			memcpy(keys[i], r->data + start, key_len + 1);
			r->pos++;
			values[i] = read_object(r, base);
			if (!values[i]) {
				xr_value_free(v);
				return NULL;
			}
		}
		return v;
	}
	default:
		r->malformed = true;
		return NULL;
	}
}

bool xr_frame_parse(const unsigned char *data, size_t len, size_t *consumed, xr_frame *frame,
	bool *need_more)
{
	*consumed = 0;
	*need_more = false;
	memset(frame, 0, sizeof(*frame));
	if (len < 24) {
		*need_more = true;
		return false;
	}
	reader r = { .data = data, .len = len };
	if (read_u32(&r) != XR_WRAPPER_MAGIC) {
		return false;
	}
	frame->flags = read_u32(&r);
	uint64_t size = read_u64(&r);
	frame->message_id = read_u64(&r);
	if (size > len - 24) {
		*need_more = true;
		return false;
	}
	if (size == 0) {
		*consumed = 24;
		return true;
	}
	r.len = 24 + (size_t)size;
	if (read_u32(&r) != XR_PAYLOAD_MAGIC || read_u32(&r) != XR_PAYLOAD_VERSION) {
		return false;
	}
	size_t base = 24;
	frame->payload = read_object(&r, base);
	if (!frame->payload) {
		*need_more = r.truncated;
		return false;
	}
	*consumed = 24 + (size_t)size;
	return true;
}

void xr_value_free(xr_value *value)
{
	if (!value) {
		return;
	}
	switch (value->kind) {
	case XR_DATA:
	case XR_STRING:
	case XR_UUID:
		free((void *)value->u.bytes.bytes);
		break;
	case XR_ARRAY:
		for (uint32_t i = 0; i < value->u.array.count; i++) {
			xr_value_free((xr_value *)value->u.array.items[i]);
		}
		free((void *)value->u.array.items);
		break;
	case XR_DICTIONARY:
		for (uint32_t i = 0; i < value->u.dict.count; i++) {
			free((void *)value->u.dict.keys[i]);
			xr_value_free((xr_value *)value->u.dict.values[i]);
		}
		free((void *)value->u.dict.keys);
		free((void *)value->u.dict.values);
		break;
	default:
		break;
	}
	free(value);
}

void xr_buffer_free(xr_buffer *buffer)
{
	free(buffer->data);
	buffer->data = NULL;
	buffer->len = buffer->cap = 0;
}
