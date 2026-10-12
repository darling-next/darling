project(private-frameworks)

# this is mainly for frameworks that are required for Security,
# since Security is built for both CLI and GUI
if (COMPONENT_cli OR COMPONENT_dev_gui_common)
	set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "cli_gui_common")

	# these are also stubs, but they're needed for Security
	add_subdirectory(AppleAccount)
	add_subdirectory(AppleFSCompression)
	add_subdirectory(AppleSystemInfo)
	add_subdirectory(AuthKit)
	add_subdirectory(ConfigurationProfiles)
	add_subdirectory(CrashReporterSupport)
	add_subdirectory(login)
	add_subdirectory(MobileAsset)
	add_subdirectory(ProtocolBuffer)
	add_subdirectory(TrustedPeers)

	# required for dtrace and IOKitTools
	add_subdirectory(CoreSymbolication)
endif()

# this is mainly for anything that Xcode requires to run on the CLI
if (COMPONENT_dev_gui_common)
	set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "cli_dev_gui_common")

	add_subdirectory(DebugSymbols)
endif()

# same here, except this is for stubs that Xcode needs
if (COMPONENT_dev_gui_stubs_common)
	set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "cli_dev_gui_stubs_common")

	add_subdirectory(AppleSauce)
	add_subdirectory(AssetCacheServices)
	add_subdirectory(AssistantServices)
	add_subdirectory(AuthKitUI)
	add_subdirectory(CoreAnalytics)
	add_subdirectory(CoreSimulator)
	add_subdirectory(CoreUI)
	add_subdirectory(DataDetectors)
	add_subdirectory(DataDetectorsCore)
	add_subdirectory(DataDetectorsNaturalLanguage)
	add_subdirectory(DeviceLink)
	add_subdirectory(DiskImages)
	add_subdirectory(DiskManagement)
	add_subdirectory(Espresso)
	add_subdirectory(IconServices)
	add_subdirectory(IOPlatformPluginFamily)
	add_subdirectory(kperf)
	add_subdirectory(LoggingSupport)
	# MetalNoGPU answers "no Metal device exists in this guest", which is true rather than a
	# placeholder; see its source. It is here rather than under COMPONENT_dev_gui_common because
	# that flag gates the REAL Vulkan-backed Metal in src/external/metal, and this library exists
	# precisely because that one is not built.
	#
	# MEASURED 2026-10-12 (the QuartzCore/MTLTextureInternal rung): the old "Remove this line if
	# that component is enabled" is now the guard below, because the two can be built at once.
	# COMPONENT_dev_gui_common is not the switch that matters here -- the top-level selection is
	# (DARLING_ROOTLESS_METAL_TARGET = Metal when BUILD_METAL and dev_gui_common, MetalNoGPU
	# otherwise), and a Vulkan host with a CLI-only component set has BUILD_METAL on without that
	# directory. Two images cannot claim one install name, and the one that must go is the
	# placeholder: the guest-visible Metal path has to be the image the rest of the tree links.
	# The guard tests the selection itself, so this file cannot disagree with the declaration.
	if (NOT DARLING_ROOTLESS_METAL_TARGET STREQUAL "Metal")
		add_subdirectory(MetalNoGPU)
	endif()
	add_subdirectory(MobileDevice)
	add_subdirectory(NetworkStatistics)
	add_subdirectory(PackageKit)
	add_subdirectory(PlugInKit)
	add_subdirectory(PowerLog)
	add_subdirectory(PubSub)
	add_subdirectory(SkyLight)
	add_subdirectory(SpotlightIndex)
	add_subdirectory(StreamingZip)
	add_subdirectory(TCC)
	add_subdirectory(ViewBridge)
endif()

# this is for all the other stubbed frameworks
if (COMPONENT_gui_stubs)
	set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "gui_stubs")

	add_subdirectory(AssertionServices)
	add_subdirectory(Bom)
	add_subdirectory(CoreUtils)
	add_subdirectory(InternationalSupport)
	add_subdirectory(PerformanceAnalysis)
	add_subdirectory(ServerInformation)
	add_subdirectory(ShazamCore)
	add_subdirectory(ShazamEvents)
	add_subdirectory(ShazamInsights)
	add_subdirectory(ShazamKitUI)
	add_subdirectory(Spotlight)
	add_subdirectory(SpotlightDaemon)
	add_subdirectory(SpotlightReceiver)
	add_subdirectory(SpotlightServerKit)
	add_subdirectory(SpotlightServices)
	add_subdirectory(UIFoundation)
endif()
