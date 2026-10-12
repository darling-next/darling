project(AssetCacheServices)

remove_sdk_framework(AssetCacheServices
    PRIVATE
)

set(DYLIB_COMPAT_VERSION "1.0.0")
# MEASURED 2026-10-12: this is the ABI level OUR build offers to clients, and it answers the load
# command in Xcode 26.3's DVTFoundation, which demands "current version 136.0.0". Our compatibility
# version already matches the client's (1.0.0) and only the current version is raised -- a deliberate
# answer to a specific load command, not a guess. The implementation behind it is not Apple's.
set(DYLIB_CURRENT_VERSION "136.0.0")

set(FRAMEWORK_VERSION "A")

generate_sdk_framework(AssetCacheServices
    VERSION ${FRAMEWORK_VERSION}
    HEADER "include/AssetCacheServices"
    PRIVATE
)

add_framework(AssetCacheServices
    FAT
    CURRENT_VERSION
    PRIVATE
    VERSION ${FRAMEWORK_VERSION}

    SOURCES
        src/AssetCacheServices.m
        src/ACSURLSession.m
        src/ACSURLSessionUploadTask.m
        src/ACSURLSessionDataTask.m
        src/ACSURLSessionTask.m
        src/ACSURLSessionDownloadTask.m

    DEPENDENCIES
        system
        objc
        Foundation
)
