project(cryptex)

set(DYLIB_INSTALL_NAME "/usr/lib/libcryptex.dylib")
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")

add_darling_library(cryptex SHARED src/objects.m src/bundle.m src/manifest.m)
target_include_directories(cryptex BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libimg4/include
)
target_compile_options(cryptex PRIVATE -fno-objc-arc)
target_link_libraries(cryptex PRIVATE system objc CoreFoundation Foundation)
make_fat(cryptex)
install(TARGETS cryptex DESTINATION libexec/darling/usr/lib)
install(FILES include/cryptex.h DESTINATION libexec/darling/usr/include)
