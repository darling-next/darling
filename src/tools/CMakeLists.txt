project(tools)

if (COMPONENT_cli)
	set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "cli")

	add_darling_executable(sw_vers sw_vers.c)
	add_darling_executable(sudo sudo.c)
	add_darling_executable(codesign codesign.c)
	add_darling_executable(dsmemberutil dsmemberutil.c)
	add_darling_executable(softwareupdate softwareupdate.c)
	add_darling_executable(spctl spctl.c)
	# perf#26 RING-MACH-MSG: the deterministic guest workload for the Ring mach_msg path. It is a guest
	# Mach-O built by the ordinary cross build (host clang + Darling SDK/ld64), so measuring the transport
	# needs no compiler inside the prefix. Installed as a diagnostic, like the other tools here.
	add_darling_executable(ring_mach_msg_test ring_mach_msg_test.c)

	install(TARGETS sw_vers sudo codesign dsmemberutil DESTINATION libexec/darling/usr/bin)
	install(TARGETS softwareupdate spctl DESTINATION libexec/darling/usr/sbin)
	install(TARGETS ring_mach_msg_test DESTINATION libexec/darling/usr/bin)

	target_link_libraries(sw_vers CoreFoundation)
endif()

if (COMPONENT_gui)
	set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "gui")

	add_darling_executable(open open.m)
	target_link_libraries(open CoreServices Foundation AppKit)
	install(TARGETS open DESTINATION libexec/darling/usr/bin)
endif()
