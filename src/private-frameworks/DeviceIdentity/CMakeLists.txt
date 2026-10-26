project(DeviceIdentity)

remove_sdk_framework(DeviceIdentity
    PRIVATE
)

set(DYLIB_COMPAT_VERSION "0.0.0")
set(DYLIB_CURRENT_VERSION "0.0.0")

include_directories(include)

set(FRAMEWORK_VERSION "A")

generate_sdk_framework(DeviceIdentity
    VERSION ${FRAMEWORK_VERSION}
    HEADER "include/DeviceIdentity"
    PRIVATE
)

add_framework(DeviceIdentity
    FAT
    CURRENT_VERSION
    PRIVATE
    VERSION ${FRAMEWORK_VERSION}

    SOURCES
        src/DeviceIdentity.c

    DEPENDENCIES
        system
        CoreFoundation
)
