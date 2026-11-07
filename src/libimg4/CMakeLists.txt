project(img4)

set(DYLIB_INSTALL_NAME "/usr/lib/libimg4.dylib")
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")

add_darling_library(img4 SHARED
    src/chip.c src/runtime.c src/firmware.c src/manifest.c src/manifest_policy.c)
target_include_directories(img4 BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(img4 PRIVATE
    ${CMAKE_SOURCE_DIR}/src/external/security/darling/src/libDER)
target_link_libraries(img4 PRIVATE system CoreFoundation IOKit Security DER)
# DER is an existing private static dependency, not additional libimg4 ABI.
set_property(TARGET img4 APPEND_STRING PROPERTY LINK_FLAGS
    " -Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/src/exports.exp")
set_property(TARGET img4 APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/exports.exp")
make_fat(img4)

install(TARGETS img4 DESTINATION libexec/darling/usr/lib)
install(DIRECTORY include/img4 DESTINATION libexec/darling/usr/include)
