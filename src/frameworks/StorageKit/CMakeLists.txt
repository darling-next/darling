project(StorageKit)

# The measured CoreDevice consumer imports SKDiskImage, SKDiskImageAttachParams and
# SKManager and sends the attach/query surface declared in src/SKInternal.h. This
# provider runs the real in-guest hdiutil (/usr/bin/hdiutil, shipped by the CLI
# component) and keeps a mount record identified by the exact kernel mount
# instance, so the consumer sees the mounted state rather than a substitute.
#
# The consumer's LC_LOAD_DYLIB records compatibility version 1.0.0. Keep that
# compatibility contract; this source does not claim the SDK's current version.
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")

set(FRAMEWORK_VERSION "A")

add_framework(StorageKit
    PRIVATE
    CURRENT_VERSION
    VERSION ${FRAMEWORK_VERSION}

    SOURCES
        src/SKDisk.m
        src/SKDiskImage.m
        src/SKManager.m
        src/SKParams.m
        src/SKSupport.m

    DEPENDENCIES
        system
        objc
        Foundation
)
