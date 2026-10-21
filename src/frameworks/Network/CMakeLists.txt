project(Network)

remove_sdk_framework(Network)

set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1.0.0")

set(FRAMEWORK_VERSION "A")

generate_sdk_framework(Network
    VERSION ${FRAMEWORK_VERSION}
    HEADER "include/Network"
)

add_framework(Network
    FAT
    CURRENT_VERSION
    VERSION ${FRAMEWORK_VERSION}

    SOURCES
        src/Network.m
        src/NWAWDLibnetcoreMPTCPStatsReport.m
        src/NWSystemPathMonitor.m
        src/NWPBCloseConnection.m
        src/NWAWDLBEndpointsFetchReport.m
        src/NWTCPConnection.m
        src/NWAWDLibnetcoreTCPECNInterfaceStatsReport.m
        src/NWAWDNWAPIUsage.m
        src/NWAddressEndpoint.m
        src/NWPBEndpoint.m
        src/NWAWDLibnetcoreTCPKernelStats.m
        src/NWAWDNWConnectionReport.m
        src/NWHostEndpoint.m
        src/NWMonitor.m
        src/NWAWDMPTCPConnectionReport.m
        src/NWAWDLibnetcoreConnectionStatisticsReport.m
        src/NWAWDLibnetcoreStatsReport.m
        src/NWConnection.m
        src/NWAWDNWActivity.m
        src/NWAWDLibnetcoreNetworkdStatsReport.m
        src/NWPBBrowseDescriptor.m
        src/NWRemotePacketProxy.m
        src/NWPBServiceBrowse.m
        src/NWPBAgent.m
        src/NWPBAgentClass.m
        src/ManagedNetworkSettings.m
        src/NWAWDLibnetcoreTCPStatsReport.m
        src/NWAWDLibnetcoreCellularFallbackReport.m
        src/NWAWDNWDeviceReport.m
        src/NWAWDLBConnectionReport.m
        src/NWAdvertiseDescriptor.m
        src/NWEndpoint.m
        src/NWAWDLibnetcoreTCPTFOStatsReport.m
        src/NWPBParameters.m
        src/NWAWDNWActivityEpilogue.m
        src/NWPBUpdateBrowse.m
        src/NWNetworkAgentRegistration.m
        src/NWPHContext.m
        src/NWPHHandler.m
        src/NWPrivilegedHelper.m
        src/NWUDPSession.m
        src/NWRemoteConnectionWriteRequest.m
        src/NWRemoteConnectionDirector.m
        src/NWNetworkDescription.m
        src/NWUDPListener.m
        src/NWParameters.m
        src/NWGenericNetworkAgent.m
        src/NWPBAddressEndpoint.m
        src/NWBonjourServiceEndpoint.m
        src/NWPath.m
        src/NWPathFlow.m
        src/NWRemoteConnectionWrapper.m
        src/NWRemoteBrowserWrapper.m
        src/NWRemoteConnectionActor.m
        src/NWAWDLibnetcoreMbufStatsReport.m
        src/NWPBHostEndpoint.m
        src/NWAWDMPTCPSubflowSwitchingReport.m
        src/NWAWDNWL2Report.m
        src/NWPBCommandMessage.m
        src/NWStreamConnection.m
        src/NWPBOpenConnection.m
        src/NWProtocolTransform.m
        src/NWInterface.m
        src/NWPBStartBrowse.m
        src/NWAWDLibnetcoreConnectionDataUsageSnapshot.m
        src/NWBrowser.m
        src/NWAWDMPTCPConnectionInterfaceReport.m
        src/NWBonjourBrowseDescriptor.m
        src/NWPBPath.m
        src/NWAWDLibnetcoreRNFActivityNotification.m
        src/NWMessage.m
        src/NWOutboundMessage.m
        src/NWInboundMessage.m
        src/NWMessageConnection.m
        src/NWL2Report.m
        src/NWDeviceReport.m
        src/NWActivityStatistics.m
        src/NWActivityEpilogueStatistics.m
        src/NWActivityEmptyTrigger.m
        src/NWPBStopBrowse.m
        src/NWConnectionStatistics.m
        src/NWPBInterface.m
        src/NWPBSendData.m
        src/NWAWDLibnetcoreTCPConnectionReport.m
        src/NWAWDNWActivityEmptyTrigger.m
        src/NWPBServiceEndpoint.m
        src/NWDatagramConnection.m
        src/NWBrowseDescriptor.m
        src/NWAWDLBClientConnectionReport.m
        src/NWPBUpdatePath.m
        src/NWResolver.m
        src/NWTCPListener.m
        src/NWPathEvaluator.m
        src/NWAWDLibnetcoreTCPECNStatsReport.m

    DEPENDENCIES
        system
        objc
        Foundation
)

reexport(Network libnetwork ${CMAKE_BINARY_DIR}/src/external/libnetwork/libnetwork.dylib)

# Apple's Network.framework binary IS the Swift Network: a client built against a macOS SDK binds its
# Swift Network symbols to /System/Library/Frameworks/Network.framework/Versions/A/Network, not to
# /usr/lib/swift/libswiftNetwork.dylib, and dyld resolves a two-level reference through the image's
# re-export chain. Measured 2026-07: the SDK's own Network.tbd carries these names as
# $ld$previous$/usr/lib/swift/libswiftNetwork.dylib$$1$10.15$14.0$_$s7Network... entries, i.e. Apple
# moved them out of libswiftNetwork.dylib into the framework. Xcode 26.3's Notarization.framework
# binds 138 of them, all eager and strong (decoded from its LC_DYLD_CHAINED_FIXUPS), against this
# framework's ordinal 12. Darling's Network is Objective-C only, so the Swift half lives in the
# payload's overlay image; re-exporting it here is what makes the framework path satisfy such a
# client, and it is the same mechanism Foundation.framework uses for libswiftFoundation.dylib
# (src/external/foundation/CMakeLists.txt) and this target already uses for libnetwork above. It is a
# re-export, not a stub: the symbols are defined by that image, and the framework defines none of them
# locally.
# NOTE ON THE TARGET, measured 2026-10-13: this tree builds Network.framework through
# add_framework(... FAT ...), so the target is `Network` (not Network_x86_64: build/verify14 has only
# CMakeFiles/Network.dir) and make_fat appends `-arch i386 -arch x86_64` to ONE link
# (cmake/darling_lib.cmake, make_fat). ld64 tolerates the mismatch and says so --
#   ld: warning: ignoring file .../libswiftNetwork.dylib, building for macOS-i386 but attempting to
#   link with file built for macOS-x86_64
# -- and the link succeeds with the framework still universal (x86_64 + i386): the x86_64 slice
# carries the re-export (llvm-objdump --macho --arch=x86_64 --dylibs-used shows
# /usr/lib/swift/libswiftNetwork.dylib (… reexport)) and the i386 slice does not. That is exactly what
# Foundation's explicit per-arch split (Foundation_x86_64 / Foundation_i386,
# src/external/foundation/CMakeLists.txt) produces, so no split is needed here.
if (TARGET_x86_64)
	set(_network_swift_overlay "${CMAKE_SOURCE_DIR}/src/external/swift/libswiftNetwork.dylib")
	if(NOT EXISTS "${_network_swift_overlay}")
		message(FATAL_ERROR
			"Network must re-export the Swift Network overlay, but "
			"src/external/swift/libswiftNetwork.dylib is absent: materialise the git-lfs payload first")
	endif()
	set_property(
		TARGET Network APPEND_STRING PROPERTY
		LINK_FLAGS " -Wl,-reexport_library,${_network_swift_overlay}"
	)
endif (TARGET_x86_64)
