// created for Darling
//
// The named signpost type is VibeDarling/darling's version of this header
// (Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/os/signpost.h,
// 990 bytes in their tree): Apple declares os_signpost_type_t with its
// OS_ENUM(os_signpost_type, uint8_t, ...) in the macOS SDK's os/signpost.h:359,
// and the os Swift overlay extends OSSignpostType and passes values of it to
// os_signpost_emit_with_type.  An anonymous enum here leaves the overlay with
// "cannot find type 'OSSignpostType' in scope" and mismatched constant values.

#ifndef _OS_SIGNPOST_H
#define _OS_SIGNPOST_H

#include <stdint.h>
#include <stddef.h>

#include <os/log.h>

typedef uint64_t os_signpost_id_t;

#define OS_SIGNPOST_ID_EXCLUSIVE ((os_signpost_id_t)0xEEEEB0B5B2B2EEEE)
#define OS_SIGNPOST_ID_NULL ((os_signpost_id_t)0)
#define OS_SIGNPOST_ID_INVALID ((os_signpost_id_t)~0)

os_signpost_id_t os_signpost_id_generate(os_log_t log);
os_signpost_id_t os_signpost_id_make_with_pointer(os_log_t log, const void* ptr);

/* DARLING: named type and values as in the macOS SDK, so binaries and Swift overlays agree. */
OS_ENUM(os_signpost_type, uint8_t,
	OS_SIGNPOST_EVENT = 0x00,
	OS_SIGNPOST_INTERVAL_BEGIN = 0x01,
	OS_SIGNPOST_INTERVAL_END = 0x02,
);

#define os_signpost_emit_with_type(log, type, spid, name, ...)

#define os_signpost_interval_begin(log, interval_id, name, ...)
#define os_signpost_event_emit(log, event_id, name, ...)
#define os_signpost_interval_end(log, interval_id, name, ...)

#endif // _OS_SIGNPOST_H
