project(mldr)

cmake_minimum_required(VERSION 3.13)

include_directories(include)

set_source_files_properties(${CMAKE_BINARY_DIR}/src/external/darlingserver/src/rpc.c PROPERTIES
	GENERATED TRUE
)

add_library(mldr_dserver_rpc STATIC ${CMAKE_BINARY_DIR}/src/external/darlingserver/src/rpc.c)

add_dependencies(mldr_dserver_rpc generate_dserver_rpc_wrappers)

target_compile_options(mldr_dserver_rpc PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/resources/dserver-rpc-defs.h)
add_dependencies(mldr_dserver_rpc rtsig_h)
target_include_directories(mldr_dserver_rpc PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..)

set(mldr_sources
	mldr.c
	../runtime_mode.c
	glibc_fork_reset.c
	commpage.c
	elfcalls/elfcalls.c
	elfcalls/dthreads.c
	elfcalls/native_threads.c
	elfcalls/threads.c
)

add_executable(mldr ${mldr_sources})
add_dependencies(mldr rtsig_h)
target_include_directories(mldr PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..)
target_compile_definitions(mldr PRIVATE
	DARLING_RUNTIME_EUNION_CAPABLE=$<BOOL:${DARLING_EUNION}>
)

target_compile_options(mldr PRIVATE -pthread)
# native_threads.c defines pthread_create for the host threads NATIVE libraries create inside a guest process. The
# loader is the process's main executable, so its definition heads the dynamic loader's global symbol scope and
# interposes for every object loaded afterwards (measured on the host: a dlopened library, directly or through a
# chain of dlopens, binds to it on the lazy first call and after). The export is named so that exactly this one
# symbol leaves .dynsym and mldr's other symbols stay private.
target_link_options(mldr PRIVATE -pthread -Wl,--export-dynamic-symbol=pthread_create)

target_link_libraries(mldr PRIVATE -lrt -ldl mldr_dserver_rpc -lm)

install(TARGETS mldr DESTINATION libexec/darling/usr/libexec/darling)

configure_file(darling.conf.in darling.conf @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/darling.conf" DESTINATION lib/binfmt.d)

if (BUILD_TARGET_32BIT)
	add_library(mldr32_dserver_rpc STATIC ${CMAKE_BINARY_DIR}/src/external/darlingserver/src/rpc.c)
	add_dependencies(mldr32_dserver_rpc generate_dserver_rpc_wrappers)
	target_compile_options(mldr32_dserver_rpc PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/resources/dserver-rpc-defs.h)
	target_compile_options(mldr32_dserver_rpc PRIVATE -m32)
	target_link_options(mldr32_dserver_rpc PRIVATE -m32)
	add_dependencies(mldr32_dserver_rpc rtsig_h)
	target_include_directories(mldr32_dserver_rpc PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..)

	add_executable(mldr32 ${mldr_sources})
	add_dependencies(mldr32 rtsig_h)
	target_include_directories(mldr32 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..)
	target_compile_definitions(mldr32 PRIVATE
		DARLING_RUNTIME_EUNION_CAPABLE=$<BOOL:${DARLING_EUNION}>
	)
	target_compile_options(mldr32 PRIVATE -m32)
	target_link_options(mldr32 PRIVATE -m32)
	target_compile_options(mldr32 PRIVATE -pthread)
	# Same interposition as mldr: see the note on the 64-bit target above.
	target_link_options(mldr32 PRIVATE -pthread -Wl,--export-dynamic-symbol=pthread_create)
	target_link_libraries(mldr32 PRIVATE -lrt -ldl mldr32_dserver_rpc)

	install(TARGETS mldr32 DESTINATION libexec/darling/usr/libexec/darling)
endif()
