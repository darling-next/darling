project(oah)

set(DYLIB_INSTALL_NAME "/usr/lib/liboah.dylib")
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")

# One symbol, one honest answer, and a DECLARED SURFACE DELIBERATELY LEFT UNBACKED: see src/oah.c for
# the measured callers (the load command of Xcode's IDEiOSSupportCore needs the image to exist at all
# and binds nothing from it, and IDEInterfaceBuilderCocoaTouchIntegration imports exactly
# _oah_is_current_process_translated). Nothing else of the 41 names a liboah.tbd declares is exported
# here, because the tree has no provider for the Rosetta/AOT translation machinery behind them and a
# guessed signature would be an invented implementation. The library links nothing because it needs
# nothing -- it must not become a home for other people's stubs.
add_darling_library(oah SHARED
	src/oah.c
)

make_fat(oah)

install(TARGETS oah DESTINATION libexec/darling/usr/lib)
