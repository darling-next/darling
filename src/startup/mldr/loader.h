#ifndef _MLDR_LOADER_H_
#define _MLDR_LOADER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../runtime_mode.h"

struct load_results {
	unsigned long mh;
	unsigned long entry_point;
	unsigned long stack_size;
	unsigned long dyld_all_image_location;
	unsigned long dyld_all_image_size;
	uint8_t uuid[16];

	unsigned long vm_addr_max;
	bool _32on64;
	enum darling_runtime_mode init_runtime_mode;
	unsigned long base;
	uint32_t bprefs[4];
	char* root_path;
	size_t root_path_length;
	unsigned long stack_top;
	char* socket_path;
	int kernfd;
	int lifetime_pipe;

	size_t argc;
	size_t envc;
	char** argv;
	char** envp;
};
// perf#30 LOADER CHECKIN ORDERING (doc section 165): the guest DYLD is loaded by the nested `load()` inside
// `load64` (`LC_LOAD_DYLINKER`), and opening the guest's `/usr/lib/dyld` goes through the guest's vchroot --
// which needs this process registered. That call therefore cannot precede the checkin, and the checkin cannot
// precede the plane's establishment. This hook performs establishment -> checkin -> vchroot exactly once, at the
// first point where all three are possible: after the outer image is mapped and before the dylinker is loaded.
// It is idempotent, so `main` may call it again before the dependent bootstrap traffic.
void mldr_bootstrap_before_dylinker_load(struct load_results* lr);

/* THE LOADER'S [tag ...] INSTRUMENT FAMILY, GATED OFF IN A PRODUCTION BUILD (DARLING_GUEST_STREAM_PROBES) --
 * the same compile-time switch the syscall-layer probes (101bf85) and the workqueue markers (a3e4b36) use.
 * MEASURED 2026-10-16 (evidence/ios-ci-rung/round43, round42): the loader writes these to the guest process's
 * OWN fd 2, so a child started with `2>file` got loader diagnostics before its own output, and the registered
 * contract `darling/startup_stream_purity_guest` fails on them ("SSP leak stderr=..."). Being thorough about
 * one instrument family and leaving another ungated is what made the earlier gate incomplete: a marker on a
 * stream that is DATA for another program is a product defect no matter which family emits it.
 * Build with -DDARLING_GUEST_STREAM_PROBES=1 to re-enable; the default emits nothing and contains none of
 * these strings. `fprintf` must already be in scope at each use (all callers include <stdio.h>). */
#ifndef MLDR_TAG
#if DARLING_GUEST_STREAM_PROBES
#define MLDR_TAG(...) fprintf(stderr, __VA_ARGS__)
#else
#define MLDR_TAG(...) ((void)0)
#endif
#endif

#endif // _MLDR_LOADER_H_
