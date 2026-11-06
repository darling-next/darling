project(DiskImages2)

# DiskImages2 provider for the unchanged DVTDownloads consumer. It attaches
# real image media through the in-guest hdiutil producer, proves device
# readiness on the selected data node, and reports the producer's version 3
# BSD/registry identity. Mounts go through DiskArbitration so only a genuine
# kernel mount is reported as mounted.

remove_sdk_framework(DiskImages2
	PRIVATE
)

set(DYLIB_COMPAT_VERSION "1.0.0")

set(FRAMEWORK_VERSION "A")

generate_sdk_framework(DiskImages2
    VERSION ${FRAMEWORK_VERSION}
    HEADER "include/DiskImages2"
	PRIVATE
)

add_framework(DiskImages2
	PRIVATE
	VERSION ${FRAMEWORK_VERSION}
	CURRENT_VERSION
	SOURCES
		src/DIError.m
		src/DIProducer.m
		src/DIHdiutil.m
		src/DIMount.m
		src/DIAttachParams.m
		src/DIDeviceHandle.m
		src/DIAttachedDeviceInfo.m
		src/DiskImages2.m
	DEPENDENCIES
		system
		objc
		Foundation
		CoreFoundation
		DiskArbitration
)

# The wire contract header is owned by darling-dmg; include it from its
# canonical location instead of copying the ABI definition.
target_include_directories(DiskImages2 PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/../../external/darling-dmg/src
)
