#ifndef _XTRACE_EXCERPT_H_
#define _XTRACE_EXCERPT_H_

#include "string.h"

#include <stddef.h>

// The printable-run budget of ONE record, and the shortest run worth quoting. A Mach message's inline
// data is where a peer's name travels -- a bootstrap look_up, an XPC check-in -- so the excerpt exists
// to show the runs and nothing else, without turning the trace into a hexdump.
#define XTRACE_EXCERPT_EMIT_MAX 1024u
#define XTRACE_EXCERPT_MIN_RUN 4u

// The state of one record's excerpt, held ACROSS the caller's copies.
//
// It exists for two measured reasons:
//   * a printable run must not be cut by a copy boundary. `print_inline_excerpt` copies a payload in
//     XTRACE_EXCERPT_CHUNK pieces, and the run that straddles two pieces is one run: closing it at
//     every chunk end printed a name as two shorter runs (or, when both halves fell under
//     XTRACE_EXCERPT_MIN_RUN, printed NOTHING for a name that is in the bytes);
//   * the run cannot be rendered before its end is known, so its bytes are held here.
struct xtrace_excerpt_state {
	// A run's bytes, plus a terminator. The budget below is what limits how much of a run is worth
	// keeping, so this is exactly the largest run the budget can pay for.
	char token[XTRACE_EXCERPT_EMIT_MAX + 1];
	size_t token_len;
	bool in_run;
	// Characters already spent in the record (a run costs its length plus the two quotes).
	size_t emitted;
};

void xtrace_excerpt_state_init(struct xtrace_excerpt_state* state);

// Render the printable runs of `bytes` into `log`, in the record's own quoting convention. PURE: it
// reads nothing but `bytes`, and it never uses a length-limited conversion such as "%.*s" -- see
// excerpt.cpp for why that mattered. `bytes` may be a piece of a larger payload; a run that continues
// into the next call is kept in `state` and rendered when (and only when) its end is seen.
void xtrace_excerpt_render(struct xtrace_excerpt_state* state, xtrace::String* log, const char* bytes, size_t n);

// Flush a run that was still open when the payload ended.
void xtrace_excerpt_finish(struct xtrace_excerpt_state* state, xtrace::String* log);

#endif // _XTRACE_EXCERPT_H_
