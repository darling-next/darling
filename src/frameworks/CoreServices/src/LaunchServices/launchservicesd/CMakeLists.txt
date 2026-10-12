project(launchservicesd)

# The registration operation itself (the bundle row and its claims) lives in the parent directory so
# that the framework's __LSRegisterURLWithOptions can run the same code: one writer convention for
# /private/var/db/launchservices.db, not a daemon copy and a framework copy.
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/..")

set(sources
	launchservicesd.m
	LSBundle.m
	../LSBundleRegistration.m
)

add_darling_executable(launchservicesd ${sources})

target_link_libraries(launchservicesd system Foundation CoreFoundation CoreServices FMDB sqlite3 z)
install(TARGETS launchservicesd DESTINATION libexec/darling/System/Library/CoreServices)
install(FILES schema.sql RENAME launchservicesd-schema.sql DESTINATION libexec/darling/System/Library/Frameworks/CoreServices.framework/Versions/A/Resources)
