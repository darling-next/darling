project(AppleArchive)

set(DYLIB_INSTALL_NAME "/usr/lib/libAppleArchive.dylib")
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")

# DELIBERATELY NO SYMBOL SURFACE. This library exists so that an image which links it loads, and it
# resolves nothing on purpose: a future client that binds a real AA* symbol then fails at LOAD with a
# symbol-not-found error, which is the loud side, rather than calling something that answers wrongly.
# Do not add a guessed AAArchiveStream* signature here -- guessing a signature is inventing an
# implementation, and the real Apple Archive format API is unimplemented work (see src/AppleArchive.c).
#
# Both versions are identical to the demand (compatibility 1.0.0, current 1.0.0), so there is no
# version claim of either kind to make -- the same baseline as the other plain rungs.
add_darling_library(AppleArchive SHARED
	src/AppleArchive.c
)

# The load-time notice is the point of this image, so it needs libSystem for the raw write(2, ...) it
# uses -- the same `system` link target the tree's other notifying libraries use (libMobileGestalt,
# libsysmon). A descriptor write rather than a stdio one, because an initializer may run before any
# stream is usable and must not depend on stderr's state.
target_link_libraries(AppleArchive system)

make_fat(AppleArchive)

install(TARGETS AppleArchive DESTINATION libexec/darling/usr/lib)
