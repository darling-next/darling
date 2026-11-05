project(RemoteXPC)

# The SDK ships no RemoteXPC; the image is supplied here. The measured remote-service constructors
# are present; the listener's accepted-connection handoff remains explicitly unimplemented.
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

    DEPENDENCIES
        system
        objc
        xpc
        nghttp2
)

# libxpc's public object model is declared in its internal headers, the same ones libxpc itself uses
# to define a type. The include is build-time only; the runtime dependency is the normal xpc target.
target_include_directories(RemoteXPC PRIVATE
    ${CMAKE_SOURCE_DIR}/src/external/libxpc/internal-include
    ${CMAKE_SOURCE_DIR}/src/external/libxpc/include
    # The vendored HTTP/2 implementation already in this configuration: its source headers and the
    # generated version header in the build tree. The transport uses its session state; only the XPC
    # wrapper codec stays ours.
    ${CMAKE_SOURCE_DIR}/src/external/nghttp2/lib/includes
    ${CMAKE_BINARY_DIR}/src/external/nghttp2/lib/includes
)


# The native provider imports the RSD service accessors. Link the source-built RSD target so the
# two-level imports resolve to the same installed image the consumer loads; no local fake is added.
target_link_libraries(RemoteXPC PRIVATE RemoteServiceDiscovery)
