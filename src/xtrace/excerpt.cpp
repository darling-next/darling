#include "excerpt.h"

#include <string.h>

// WHY THIS FILE EXISTS AT ALL (measured, follow-up 19).
//
// The excerpt used to render a run with
//
//     log->append_format(" \"%.*s\"", (int) (i - start), scratch + start);
//
// and `xtrace::String::append_format` goes through `vsnprintf_wrapper` -> `__simple_vsnprintf`, which
// implements NO width and NO precision field: its format loop reads an optional size modifier (l/h/j/z/t)
// and then switches on the next character, so for "%.*s" it consumed nothing, emitted nothing for the
// '.', and echoed the literal '*' and 's'. EVERY excerpt therefore rendered as the two characters `*s`,
// and the trace never showed the bytes it claimed to show.
//
// MEASURED, and not a corner case: in the parked arm's own capture (run fcd-fix18-bytes, state
// work-fcd-fix18-bytes-181616-1727760, the traced arm's thread tid 1733426) there are 33 records that
// say "bytes of inline data" and ALL 33 end in `"*s"` -- including bodies whose name sits at offset 8
// behind an 8-byte NDR header (`00 00 00 00 | 01 00 00 00 | com.apple.system.notification_center ...`),
// which a faithful copy of the same buffer reads back as plain ASCII. The same defect cost four rounds
// of a build investigation, because a name-in-payload reading looked falsified: a diagnostic that
// silently disagrees with its own input is worse than one that prints nothing.
//
// The repair is NOT to teach that formatter precision (it is a runtime wide-area change, and the record
// format of every other conversion would move with it). It is to stop using a length-limited conversion
// here: the run is built into `state->token` and printed with plain "%s", which the formatter implements.

void xtrace_excerpt_state_init(struct xtrace_excerpt_state* state)
{
	state->token_len = 0;
	state->in_run = false;
	state->emitted = 0;
	state->token[0] = '\0';
}

static void xtrace_excerpt_close_run(struct xtrace_excerpt_state* state, xtrace::String* log)
{
	if (!state->in_run)
		return;

	state->in_run = false;
	if (state->token_len >= XTRACE_EXCERPT_MIN_RUN && state->emitted < XTRACE_EXCERPT_EMIT_MAX) {
		state->token[state->token_len] = '\0';
		log->append_format(" \"%s\"", state->token);
		state->emitted += state->token_len + 1;
	}

	state->token_len = 0;
	state->token[0] = '\0';
}

void xtrace_excerpt_render(struct xtrace_excerpt_state* state, xtrace::String* log, const char* bytes, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		const unsigned char c = (unsigned char) bytes[i];
		if (c >= 32 && c < 127) {
			state->in_run = true;
			if (state->token_len < XTRACE_EXCERPT_EMIT_MAX)
				state->token[state->token_len++] = (char) c;
			continue;
		}

		xtrace_excerpt_close_run(state, log);
	}
}

void xtrace_excerpt_finish(struct xtrace_excerpt_state* state, xtrace::String* log)
{
	xtrace_excerpt_close_run(state, log);
}
