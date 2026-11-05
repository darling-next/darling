project(MobileDevice)

remove_sdk_framework(MobileDevice
	PRIVATE
)

set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")

set(FRAMEWORK_VERSION "A")

generate_sdk_framework(MobileDevice
    VERSION ${FRAMEWORK_VERSION}
    HEADER "include/MobileDevice"
	PRIVATE
)

add_framework(MobileDevice
    # Keep the link target for SDK users, not as a runtime implementation.
    NO_INSTALL
    FAT
    CURRENT_VERSION
    PRIVATE
    VERSION ${FRAMEWORK_VERSION}

    SOURCES
        src/MobileDevice.m
        src/AMSupportURLConnectionDelegate.m
        src/AMSupportURLSession.m
        src/RPStream.m
        src/PersonalizedImage.m

    DEPENDENCIES
        system
        objc
        Foundation
)

# The system framework is an OS-owned alias to the separately installed Apple
# payload. Without that package the alias intentionally remains dangling.
InstallSymlink("../../../Library/Apple/System/Library/PrivateFrameworks/MobileDevice.framework"
    "${CMAKE_INSTALL_PREFIX}/libexec/darling/System/Library/PrivateFrameworks/MobileDevice.framework")
