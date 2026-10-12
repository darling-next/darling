project(DiskImages)

remove_sdk_framework(DiskImages
	PRIVATE
)

add_compile_options(
	-nostdinc
)

set(DYLIB_COMPAT_VERSION "1.0.8")
# MEASURED 2026-10-12, and this component's version handling was changed once and then put back.
#
# A DYLIB_CURRENT_VERSION of 680.0.0 was added here, the current version Xcode 26.3's DVTFoundation
# load command demands, under the belief that dyld enforces that value. It does not: the proof is on
# this same ladder -- DVTFoundation demands current 1522.1.0 of CoreData, the deployed CoreData
# declares current 752.0.0, and the client loaded CoreData and moved on. The enforced value is the
# COMPATIBILITY version, and ours already equals the client's 1.0.8, which is why the rung advanced
# on its declaration alone. So the component goes back to setting NO current version at all, which is
# what it had: cmake/darling_lib.cmake passes no -current_version and ld64 writes its own default.
#
# COMPATIBILITY 1.0.8 is the value that matters and it is unchanged: it equals the client's demand,
# so there is no ABI claim to make, and raising it would claim compatibility with a level this build
# has not implemented.

set(FRAMEWORK_VERSION "A")

generate_sdk_framework(DiskImages
    VERSION ${FRAMEWORK_VERSION}
    HEADER "include/DiskImages"
	PRIVATE
)

add_framework(DiskImages
	PRIVATE
	VERSION ${FRAMEWORK_VERSION}
	CURRENT_VERSION
	SOURCES
		src/functions.c
		#src/DIHelperProxy.m
		src/NSDictionary+BetterAccessors.m
	DEPENDENCIES
		system
		Foundation
)
