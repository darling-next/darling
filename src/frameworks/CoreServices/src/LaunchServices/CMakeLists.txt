set(SUBFRAMEWORK_VERSION "A")

generate_sdk_subframework(LaunchServices
	BASE_PATH ${coreservices_sdk_path}
	VERSION ${SUBFRAMEWORK_VERSION}
	HEADER "../../include/LaunchServices"
)

add_framework(LaunchServices
	FAT
	CURRENT_VERSION
	VERSION ${SUBFRAMEWORK_VERSION}
	PARENT "CoreServices"

	SOURCES
		LaunchServices.c
		LaunchServices.cpp
		LSApplicationProxy.m
		LSApplicationRecord.m
		LSApplicationWorkspace.m
		LSBundleRegistration.m
		LSRegister.m
		LSRunning.m
		constants.c
		LSInfo.m
		LSOpen.m
		UTType.m

	DEPENDENCIES
		CoreFoundation
		Foundation
		cxx
		icucore
		system
		CarbonCore
		FMDB
		z
)

add_subdirectory(launchservicesd)
