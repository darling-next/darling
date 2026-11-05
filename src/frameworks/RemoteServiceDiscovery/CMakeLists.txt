project(RemoteServiceDiscovery)

# The measured broker/device/service client used by CoreDeviceUtilities and by RemoteXPC. It is a
# genuine client of com.apple.remoted and com.apple.remoted.control: when the broker is absent every
# call returns the real XPC error (NULL/false/-1) and no synthetic device, reply, or catalog.
#
# The consumer's LC_LOAD_DYLIB records compatibility version 1.0.0. Keep that contract; this source
# does not claim the SDK's current version.
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")

set(FRAMEWORK_VERSION "A")

add_framework(RemoteServiceDiscovery
    PRIVATE
    CURRENT_VERSION
    VERSION ${FRAMEWORK_VERSION}

    SOURCES
        src/RemoteServiceDiscovery.m

    DEPENDENCIES
        system
        objc
        xpc
)
