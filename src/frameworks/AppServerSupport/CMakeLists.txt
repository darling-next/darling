project(AppServerSupport)

# The measured CoreDeviceUtilities consumer imports OSLaunchdDomain and
# OSLaunchdJob and uses the job plist/query/monitor path. The implementation is
# in src/AppServerSupport.m and delegates to the existing launchd client stack.
# The consumer's LC_LOAD_DYLIB records current 3089.40.16 and compatibility
# 1.0.0. The SDK tbd's 3089.60.7 is metadata, not measured native runtime
# provenance, so this provider carries its own version. dyld's load requirement
# is the compatibility version.
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")

set(FRAMEWORK_VERSION "A")

add_framework(AppServerSupport
    PRIVATE
    CURRENT_VERSION
    VERSION ${FRAMEWORK_VERSION}

    SOURCES
        src/AppServerSupport.m

    DEPENDENCIES
        system
        objc
        Foundation
        ServiceManagement
        launch
)

target_include_directories(AppServerSupport PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
