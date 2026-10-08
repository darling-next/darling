#ifndef DARLING_RUNTIME_MODE_H
#define DARLING_RUNTIME_MODE_H

#include <stdbool.h>
#include <stddef.h>

#define DARLING_RUNTIME_MODE_ENV "DARLING_RUNTIME_MODE"
/*
 * The privileged launcher is RETIRED. It needed setuid root, which made a normal
 * development run depend on an install step and on mount options, and it is not the
 * mode the product is validated in. Rootless is the default; a privileged mode is
 * refused unless this variable is set explicitly, so an accidental privileged run
 * fails loudly instead of silently demanding setuid root.
 */
#define DARLING_RUNTIME_FORCE_ROOTFULL_ENV "DARLING_FORCE_ROOTFULL_MODE"
#define DARLING_RUNTIME_MODE_MARKER_NAME ".darling-runtime-mode-v1"
#define DARLING_RUNTIME_MODE_MARKER_VERSION 1

#ifndef DARLING_RUNTIME_EUNION_CAPABLE
#define DARLING_RUNTIME_EUNION_CAPABLE 0
#endif

enum darling_runtime_mode {
	DARLING_RUNTIME_MODE_INVALID = 0,
	DARLING_RUNTIME_MODE_PRIVILEGED_OVERLAY,
	DARLING_RUNTIME_MODE_PRIVILEGED_COPY,
	DARLING_RUNTIME_MODE_PRIVILEGED_EUNION,
	DARLING_RUNTIME_MODE_ROOTLESS_EUNION,
};

struct darling_runtime_cli {
	bool rootless;
	bool show_help;
	bool show_version;
	int command_index;
};

const char* darling_runtime_mode_name(enum darling_runtime_mode mode);
enum darling_runtime_mode darling_runtime_mode_parse(const char* value);
bool darling_runtime_mode_is_rootless(enum darling_runtime_mode mode);
bool darling_runtime_mode_uses_overlay(enum darling_runtime_mode mode);
bool darling_runtime_mode_uses_eunion(enum darling_runtime_mode mode);
enum darling_runtime_mode darling_runtime_mode_default_for_host(void);

int darling_runtime_mode_parse_cli(
	int argc,
	char* const argv[],
	struct darling_runtime_cli* cli,
	char* error,
	size_t error_size
);

int darling_runtime_mode_resolve(
	bool cli_rootless,
	const char* canonical,
	const char* legacy_rootless,
	const char* legacy_nooverlay,
	const char* legacy_eunion,
	enum darling_runtime_mode default_mode,
	bool eunion_capable,
	bool force_rootfull,
	enum darling_runtime_mode* mode,
	char* error,
	size_t error_size
);

int darling_runtime_mode_select_process(
	const struct darling_runtime_cli* cli,
	bool eunion_capable,
	enum darling_runtime_mode* mode,
	char* error,
	size_t error_size
);

int darling_runtime_mode_publish(
	enum darling_runtime_mode mode,
	char* error,
	size_t error_size
);

int darling_runtime_mode_require_canonical_process(
	bool eunion_capable,
	enum darling_runtime_mode* mode,
	char* error,
	size_t error_size
);

#endif
