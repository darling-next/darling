project(launchd-top)

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_subdirectory(liblaunch)
add_subdirectory(src)
add_subdirectory(support)

install(FILES man/launchctl.1 DESTINATION libexec/darling/usr/share/man/man1)
install(FILES man/launchd.8 DESTINATION libexec/darling/usr/share/man/man8)

# Match xcscripts/launchctl-postflight.sh: launchctl rejects group/world-writable
# service directories, including directories implicitly created under the host umask.
foreach(directory IN ITEMS Library/LaunchDaemons Library/LaunchAgents
		System/Library/LaunchDaemons System/Library/LaunchAgents)
	install(DIRECTORY DESTINATION libexec/darling/${directory}
		DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
			GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()
