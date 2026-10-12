project(AssetCacheServices)

remove_sdk_framework(AssetCacheServices
    PRIVATE
)

set(DYLIB_COMPAT_VERSION "1.0.0")
# MEASURED 2026-10-12, and this value was raised once and then put back deliberately.
#
# It was raised to 136.0.0, the current version Xcode 26.3's DVTFoundation load command demands,
# under the belief that dyld enforces that value. It does not: the proof is on this same ladder --
# DVTFoundation demands current 1522.1.0 of CoreData, the deployed CoreData declares current 752.0.0,
# and the client loaded CoreData and moved on. The enforced value is the COMPATIBILITY version, and
# ours already matched the client's (1.0.0), which is why the rung advanced on its declaration alone.
# So the value goes back to the tree's own 106.1.0.
set(DYLIB_CURRENT_VERSION "106.1.0")

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
