project(OSLog)

remove_sdk_framework(OSLog)

# The version SWBBuildSystem's load command demands, measured from the guest image: OSLog.framework
# current version 1815.60.2, compatibility version 1.0.0.  dyld enforces only compatibility, but
# there is no reason to declare a current version below the demand.
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1815.60.2")

set(FRAMEWORK_VERSION "A")

generate_sdk_framework(OSLog
	VERSION ${FRAMEWORK_VERSION}
	HEADER "include/OSLog"
)

# The framework's own headers are imported as <OSLog/...> (the same shape an SDK client uses), so the
# include root is this directory's include/ -- as CryptoTokenKit and the tree's other frameworks with
# their own public headers do.
include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The store side is written against the ASL API and its sources use ARC, like the tree's other
# newer Objective-C (PackageKit sets the same flag).
add_definitions(-fobjc-arc)

add_framework(OSLog
	FAT
	CURRENT_VERSION
	VERSION ${FRAMEWORK_VERSION}

	SOURCES
		src/_OSLogEntryFields.m
		src/OSLogEntry.m
		src/OSLogEntryActivity.m
		src/OSLogEntryBoundary.m
		src/OSLogEntryLog.m
		src/OSLogEntrySignpost.m
		src/OSLogEntryFromRecord.m
		src/OSLogEnumerator.m
		src/OSLogMessageComponent.m
		src/OSLogPosition.m
		src/OSLogStore.m

	DEPENDENCIES
		system
		objc
		Foundation
)
