project(img4)

set(DYLIB_INSTALL_NAME "/usr/lib/libimg4.dylib")
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")

add_darling_library(img4 SHARED src/chip.c)
target_include_directories(img4 BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(img4 system CoreFoundation IOKit)
make_fat(img4)

install(TARGETS img4 DESTINATION libexec/darling/usr/lib)
