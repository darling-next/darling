project(Rosetta)

set(DYLIB_INSTALL_NAME "/usr/lib/libRosetta.dylib")
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")

# One symbol, one honest answer: see src/Rosetta.c for the measured caller (Xcode's DVTFoundation
# hard-links this install name and imports exactly _rosetta_is_current_process_translated). The library
# links nothing because it needs nothing -- it must not become a home for other people's stubs.
add_darling_library(Rosetta SHARED
	src/Rosetta.c
)

make_fat(Rosetta)

install(TARGETS Rosetta DESTINATION libexec/darling/usr/lib)
