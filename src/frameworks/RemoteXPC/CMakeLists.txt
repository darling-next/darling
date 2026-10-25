project(RemoteXPC)

# The SDK ships no RemoteXPC; the image is supplied here. Do not claim a full provider until the
# remote-service entry points exist.
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")
set(FRAMEWORK_VERSION "A")

add_framework(RemoteXPC
    PRIVATE
    CURRENT_VERSION
    VERSION ${FRAMEWORK_VERSION}

    SOURCES
        src/RemoteXPCConnection.m
        src/xr_wire.c
        src/xr_h2.c

    DEPENDENCIES
        system
        objc
        xpc
)

# libxpc's public object model is declared in its internal headers, the same ones libxpc itself uses
# to define a type. The include is build-time only; the runtime dependency is the normal xpc target.
target_include_directories(RemoteXPC PRIVATE
    ${CMAKE_SOURCE_DIR}/src/external/libxpc/internal-include
    ${CMAKE_SOURCE_DIR}/src/external/libxpc/include
)
