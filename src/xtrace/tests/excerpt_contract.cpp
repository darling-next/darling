// CONTRACT TEST for the inline excerpt: what a record renders MUST be the bytes that are there.
//
// It exists because it was not. The excerpt rendered a printable run with
//
//     log->append_format(" \"%.*s\"", (int) (i - start), scratch + start);
//
// and xtrace::String::append_format goes through __simple_vsnprintf, which implements neither a width
// nor a precision field: for "%.*s" it consumed no argument, emitted nothing for the '.', and echoed
// the literal '*' and 's'. EVERY excerpt in every record therefore read `"*s"`.
//
// MEASURED, and not a corner case: 33 of the 33 inline-data records in the parked arm's own capture
// (run fcd-fix18-bytes, state work-fcd-fix18-bytes-181616-1727760, the traced arm's tid 1733426) end
// in `"*s"`, including bodies whose service name is plain ASCII at offset 8 behind an 8-byte NDR
// header. That disagreement cost four rounds of a build investigation, because a name-in-payload
// reading looked falsified: a diagnostic that disagrees with its own input is worse than one that
// prints nothing.
//
// This test is deliberately HOST-side and links the REAL pieces: ../excerpt.cpp (the renderer),
// ../string.cpp (the String it renders into) and libsystem_kernel's own simple.c (the formatter it
// renders through). The conversion that was wrong is therefore the conversion under test, not a stub.
// The only thing this file supplies is the allocator the library takes from _elfcalls in the guest.
#include "../excerpt.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void* xtrace_malloc(size_t size) { return malloc(size); }
extern "C" void xtrace_free(void* ptr) { free(ptr); }
extern "C" void* xtrace_realloc(void* ptr, size_t size) { return realloc(ptr, size); }

static int failures = 0;

// The exact body shape measured in the arm's capture: 8 bytes of NDR header, then the name at offset
// 8, then NUL padding to 164 bytes.
static const char* const SERVICE = "com.apple.system.notification_center";

static size_t build_lookup_body(char* out, size_t cap)
{
	memset(out, 0, cap);
	out[0] = 0x00; out[1] = 0x00; out[2] = 0x00; out[3] = 0x00;
	out[4] = 0x01; out[5] = 0x00; out[6] = 0x00; out[7] = 0x00;
	memcpy(out + 8, SERVICE, strlen(SERVICE));
	return cap;
}

static void check(const char* what, bool ok, const char* rendered)
{
	printf("  %-58s %s\n", what, ok ? "PASS" : "FAIL");
	if (!ok) {
		failures++;
		printf("      rendered: %s\n", rendered);
	}
}

static const char* render(const char* bytes, size_t n, xtrace::String* log)
{
	struct xtrace_excerpt_state state;
	xtrace_excerpt_state_init(&state);
	xtrace_excerpt_render(&state, log, bytes, n);
	xtrace_excerpt_finish(&state, log);
	return log->c_str();
}

int main(void)
{
	char body[164];
	build_lookup_body(body, sizeof(body));

	// WITNESS, not an assertion: what the OLD rendering produced through this same formatter. It is
	// printed on every run so that the reason this file exists is in the record, and it is not checked,
	// because a future formatter that implements precision would (correctly) render the name here.
	{
		xtrace::String* log = xtrace::String::new_ptr();
		log->append_format(" \"%.*s\"", (int) strlen(SERVICE), SERVICE);
		printf("witness: the old conversion, through this same formatter: %s\n", log->c_str());
		xtrace::String::free_ptr(log);
	}

	// 1. THE MEASURED BODY: the name is rendered, and the debris is gone.
	{
		xtrace::String* log = xtrace::String::new_ptr();
		const char* out = render(body, sizeof(body), log);
		printf("case 1: body of %zu bytes with a name at offset 8\n    rendered:%s\n", sizeof(body), out);
		check("the excerpt contains the service name", strstr(out, SERVICE) != NULL, out);
		check("the excerpt contains no \"*s\" debris", strstr(out, "*s") == NULL, out);
		{
			// The name must appear exactly once: a run split by a boundary would appear as two tokens
			// only if the pieces were each long enough to be quoted, which is what case 2 pins.
			int occurrences = 0;
			for (const char* at = out; (at = strstr(at, SERVICE)) != NULL; at++) occurrences++;
			check("the excerpt renders the name exactly once", occurrences == 1, out);
		}
		xtrace::String::free_ptr(log);
	}

	// 2. A RUN THAT STRADDLES A COPY BOUNDARY IS ONE RUN. The caller copies a payload in
	//    XTRACE_EXCERPT_CHUNK pieces; the run is fed here as two pieces with the cut in the middle of
	//    the name, which is what the previous code closed at every chunk end.
	{
		xtrace::String* log = xtrace::String::new_ptr();
		struct xtrace_excerpt_state state;
		xtrace_excerpt_state_init(&state);
		const size_t cut = 8 + 6; // inside "notification"
		xtrace_excerpt_render(&state, log, body, cut);
		xtrace_excerpt_render(&state, log, body + cut, sizeof(body) - cut);
		xtrace_excerpt_finish(&state, log);
		const char* out = log->c_str();
		printf("case 2: the same body fed as two pieces, cut at byte %zu\n    rendered:%s\n", cut, out);
		check("the name survives the copy boundary whole", strstr(out, SERVICE) != NULL, out);
		xtrace::String::free_ptr(log);
	}

	// 3. A RUN LONGER THAN A COPY CHUNK. The first XTRACE_EXCERPT_EMIT_MAX characters of the run must
	//    be one token, not several: this is the same contract at the budget's edge.
	{
		const size_t total = 8192;
		char* long_body = (char*) malloc(total);
		memset(long_body, 'A', total);
		long_body[0] = 'Z';   // so the token's start is recognizable
		xtrace::String* log = xtrace::String::new_ptr();
		struct xtrace_excerpt_state state;
		xtrace_excerpt_state_init(&state);
		size_t done = 0;
		while (done < total) {
			size_t piece = total - done > 4096 ? 4096 : total - done;
			xtrace_excerpt_render(&state, log, long_body + done, piece);
			done += piece;
		}
		xtrace_excerpt_finish(&state, log);
		const char* out = log->c_str();
		char expected[XTRACE_EXCERPT_EMIT_MAX + 1];
		memset(expected, 'A', XTRACE_EXCERPT_EMIT_MAX);
		expected[0] = 'Z';
		expected[XTRACE_EXCERPT_EMIT_MAX] = '\0';
		printf("case 3: an %zu-byte printable run fed in 4096-byte pieces\n    rendered length=%zu, first 20:%s\n",
			total, strlen(out), out);
		check("the run's first EMIT_MAX characters are one token", strstr(out, expected) != NULL, out);
		free(long_body);
		xtrace::String::free_ptr(log);
	}

	// 4. NOTHING PRINTABLE, NOTHING RENDERED -- and a run below the minimum is not quoted either.
	{
		xtrace::String* log = xtrace::String::new_ptr();
		char binary[64];
		memset(binary, 0x01, sizeof(binary));
		memcpy(binary + 20, "abc", 3);   // 3 printable characters: below XTRACE_EXCERPT_MIN_RUN
		const char* out = render(binary, sizeof(binary), log);
		printf("case 4: %zu bytes of non-printables plus a 3-character run\n    rendered:'%s'\n", sizeof(binary), out);
		check("a 3-character run is below the minimum and is not quoted", strstr(out, "abc") == NULL, out);
		check("a body with no run renders nothing at all", out[0] == '\0', out);
		xtrace::String::free_ptr(log);
	}

	printf("EXCERPT-CONTRACT %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
