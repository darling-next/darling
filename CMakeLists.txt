cmake_minimum_required(VERSION 3.13)

SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_SYSTEM_PROCESSOR "x86-64")

# This allows setting the compiler with -DCMAKE_C_COMPILER when configuring.
if (NOT DEFINED CMAKE_C_COMPILER)
	find_program(CMAKE_C_COMPILER NAMES
		"clang"
		"clang-4.0"
		"clang-6.0"
		"clang-7"
		"clang-9"
		"clang-10"
		"clang-11"
		"clang-12"
		"clang-13"
		"clang-14"
		"clang-15"
		"clang-16"
		"clang-17"
		"clang-18"
		"clang-19"
		"clang-20"
		"clang-21"
		"clang-22"
		"clang-23"
)
endif(NOT DEFINED CMAKE_C_COMPILER)

if (NOT DEFINED CMAKE_CXX_COMPILER)
	find_program(CMAKE_CXX_COMPILER NAMES
		"clang++"
		"clang++-4.0"
		"clang++-6.0"
		"clang++-7"
		"clang++-9"
		"clang++-10"
		"clang++-11"
		"clang++-12"
		"clang++-13"
		"clang++-14"
		"clang++-15"
		"clang++-16"
		"clang++-17"
		"clang++-18"
		"clang++-19"
		"clang++-20"
		"clang++-21"
		"clang++-22"
		"clang++-23"
)
endif(NOT DEFINED CMAKE_CXX_COMPILER)

option(DARLING_NO_CCACHE "Disable ccache usage" OFF)
option(DARLING_EUNION
	"Enable E-UNION union-in-vchroot prefix assembly (experimental)"
	OFF)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT DARLING_NO_CCACHE)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

SET(CMAKE_SKIP_RPATH TRUE)

# technically ignored by CMake when building on non-Apple platforms, but it's already a standard variable for the
# SDK deployment target, so we'll just use it and then add it to the compiler flags manually
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "The version of macOS we're simulating")

project(darling)

enable_language(ASM)

set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "core")

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(clang_version_check)
include(InstallSymlink)
include(MacroEnsureOutOfSourceBuild)
include(dsym)
include(xcproj)
include(architecture)
include(create_symlink)

set(CLANG_RECOMMENDED_MINIMUM_VERSION 11)
clang_version_check(${CMAKE_C_COMPILER} c ${CLANG_RECOMMENDED_MINIMUM_VERSION})
clang_version_check(${CMAKE_CXX_COMPILER} cpp ${CLANG_RECOMMENDED_MINIMUM_VERSION})

MACRO_ENSURE_OUT_OF_SOURCE_BUILD()

set(DARLING_TOP_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set(DARLING_NO_EXECUTABLES OFF)
set(CMAKE_C_IMPLICIT_LINK_LIBRARIES "")
set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES "")

if (${CMAKE_HOST_SYSTEM_PROCESSOR} MATCHES "i686|i386")
	message(FATAL_ERROR "This software can only be built on x86-64 systems")
endif (${CMAKE_HOST_SYSTEM_PROCESSOR} MATCHES "i686|i386")

SET(IGNORED_WARNINGS "-Wno-nullability-completeness -Wno-deprecated-declarations -Wno-availability")

if (${CMAKE_C_COMPILER_ID} STREQUAL "Clang" AND NOT ${CMAKE_C_COMPILER_VERSION} VERSION_LESS "3.9")
	SET(IGNORED_WARNINGS "${IGNORED_WARNINGS} -Wno-expansion-to-defined")
endif (${CMAKE_C_COMPILER_ID} STREQUAL "Clang" AND NOT ${CMAKE_C_COMPILER_VERSION} VERSION_LESS "3.9")

if(${CMAKE_C_COMPILER_ID} STREQUAL "Clang" AND ${CMAKE_C_COMPILER_VERSION} VERSION_GREATER_EQUAL "11")
	# newer Clang chokes on the idiomatic way to use CF_ENUM; e.g. like this:
	#     typedef CF_ENUM(int, MyEnum) {
	#       MY_ENUM_THING,
	#       MY_ENUM_OTHER_THING,
	#       # etc...
	#     }
	set(IGNORED_WARNINGS "${IGNORED_WARNINGS} -Wno-elaborated-enum-base -Wno-undef-prefix")
	set(ASM_IGNORED_WARNINGS "${ASM_IGNORED_WARNINGS} -Wno-undef-prefix")
endif()

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${IGNORED_WARNINGS}")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${IGNORED_WARNINGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${ASM_IGNORED_WARNINGS}")
SET(CMAKE_C_FLAGS_DEBUG "-O0 -ggdb")
SET(CMAKE_CXX_FLAGS_DEBUG "-O0 -ggdb")

# prevent object filename conflicts for two source files that differ only in extension (e.g. `object.c` and `object.m`)
set(CMAKE_C_OUTPUT_EXTENSION ".c.o")
set(CMAKE_CXX_OUTPUT_EXTENSION ".cpp.o")
set(CMAKE_OBJC_OUTPUT_EXTENSION ".m.o")

if(CMAKE_POSITION_INDEPENDENT_CODE)
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
	SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pie")
endif(CMAKE_POSITION_INDEPENDENT_CODE)

enable_language(ASM-ATT)

option(TARGET_i386 "Enable i386 slices" ON)
option(TARGET_x86_64 "Enable x86_64 slices" ON)
option(DEBIAN_PACKAGING "Packaging for Debian" OFF)
option(ENABLE_TESTS "Install in-prefix unit tests" OFF)
option(REGENERATE_SDK "Regenerate Header Files For Open Source SDK" OFF)

if (DEBIAN_PACKAGING)
	set(COMPONENTS_DEFAULT "all")
else()
	set(COMPONENTS_DEFAULT "stock")
endif()

set(COMPONENTS "${COMPONENTS_DEFAULT}" CACHE STRING "Choose which components of Darling to build")
include(darling_parse_components)
darling_parse_components("${COMPONENTS}")

set(COMPILE_PY2_BYTECODE AUTO CACHE STRING "Pre-compile bytecode for Python 2 packages")
set_property(CACHE COMPILE_PY2_BYTECODE PROPERTY STRINGS AUTO ON OFF)

string(TOLOWER "${COMPILE_PY2_BYTECODE}" COMPILE_PY2_BYTECODE)

if (COMPILE_PY2_BYTECODE STREQUAL "auto")
	find_package(Python2 QUIET COMPONENTS Interpreter)

	if (Python2_Interpreter_FOUND)
		set(COMPILE_PY2_BYTECODE ON)
		message("Found Python 2; enabling pre-compilation of Python bytecode")
	else()
		set(COMPILE_PY2_BYTECODE OFF)
		message("Python 2 not available; bytecode compilation is disabled")
	endif (Python2_Interpreter_FOUND)
endif (COMPILE_PY2_BYTECODE STREQUAL "auto")

if (COMPILE_PY2_BYTECODE)
	find_package(Python2 REQUIRED COMPONENTS Interpreter)
endif (COMPILE_PY2_BYTECODE)

set(ENABLE_METAL AUTO CACHE STRING "Build Darling with Metal support")
set_property(CACHE ENABLE_METAL PROPERTY STRINGS AUTO ON OFF)

string(TOLOWER "${ENABLE_METAL}" BUILD_METAL)

if(BUILD_METAL STREQUAL "auto")
	# check if Vulkan and LLVM are available
	find_program(LLVM_CONFIG_PROGRAM llvm-config)
	find_package(Vulkan)

	if(LLVM_CONFIG_PROGRAM AND Vulkan_FOUND)
		set(BUILD_METAL ON)
		message("Found required libraries; building with Metal support")
	else()
		set(BUILD_METAL OFF)
		message("Did not find required libraries (Vulkan and LLVM); building without Metal support")
	endif()
endif()

# THE METAL IMAGE THE ROOTLESS TOOLCHAIN DECLARES (the rung notes are at its entry in
# DARLING_ROOTLESS_TOOLCHAIN_TARGETS and at its resource entry below). MetalNoGPU is the honest
# answer for a configuration that cannot put the Vulkan-backed Metal on that install name; when the
# real one IS in this build it must be the image on
# /System/Library/Frameworks/Metal.framework/Versions/A/Metal, because two images cannot claim one
# install name and the rest of the tree links the real one (QuartzCore's Metal-enabled drawable code
# binds its Objective-C class). Both halves of the condition are required: BUILD_METAL is "this host
# can build it" and COMPONENT_dev_gui_common is the gate src/CMakeLists.txt actually adds
# src/external/metal under, so a CLI-only tree on a Vulkan host still gets the placeholder. This one
# variable is the single source of truth: src/private-frameworks builds the placeholder only when it
# names the placeholder, so the two images can never both be built.
if (BUILD_METAL AND COMPONENT_dev_gui_common)
	set(DARLING_ROOTLESS_METAL_TARGET Metal)
else()
	set(DARLING_ROOTLESS_METAL_TARGET MetalNoGPU)
endif()

FindDsymutil()
find_package(Setcap REQUIRED)

# Missing CMakeLists.txt must trigger an error
cmake_policy(SET CMP0014 NEW)

generate_architecture()

# --- dar-gax: UNCONDITIONAL patchset-drift gate ------------------------------
# TWICE a fix registered in patches.yml was missing from the committed
# integration/homebrew tip the binary was built from, silently regressing it
# (dar-gwn.6 xnu ulock, dar-gwn.1.9 darlingserver assert-wait). A gate that
# relies on someone remembering to run `west patch status` is no gate. So wire
# it into the build itself: it cannot be bypassed by a plain `ninja <target>`.
#
# Opt out only for packaging/CI that builds outside the patched workspace:
#   -DDARLING_SKIP_DRIFT_GATE=ON
option(DARLING_SKIP_DRIFT_GATE "Skip the patchset-drift gate (packaging/CI only)" OFF)
set(DARLING_PATCH_PROFILE "" CACHE STRING
	"Patch profile expected by the drift gate (empty: derive integration/<profile> or homebrew)")
if(NOT DARLING_SKIP_DRIFT_GATE)
	# Locate the workspace that owns patches.yml (manifest repo two levels up:
	# <ws>/darling-dev/darling -> <ws>/darling-dev/darling-workspace).
	find_program(WEST_PROGRAM west)
	set(_drift_ws "${CMAKE_CURRENT_SOURCE_DIR}/../darling-workspace")
	set(_drift_profile "${DARLING_PATCH_PROFILE}")
	if(NOT _drift_profile)
		execute_process(
			COMMAND git branch --show-current
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
			RESULT_VARIABLE _drift_branch_rc
			OUTPUT_VARIABLE _drift_branch
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_QUIET)
		if(_drift_branch_rc EQUAL 0 AND _drift_branch MATCHES "^integration/([A-Za-z0-9_.-]+)$")
			set(_drift_profile "${CMAKE_MATCH_1}")
		else()
			set(_drift_profile "homebrew")
		endif()
	endif()
	set(_drift_profile_manifest "${_drift_ws}/patches/${_drift_profile}/patches.yml")
	if(WEST_PROGRAM AND EXISTS "${_drift_profile_manifest}")
		# (1) configure-time check: fail `cmake`/reconfigure on drift.
		execute_process(
			COMMAND ${WEST_PROGRAM} patch status --profile ${_drift_profile} --strict
			WORKING_DIRECTORY "${_drift_ws}"
			RESULT_VARIABLE _drift_rc
			OUTPUT_VARIABLE _drift_out
			ERROR_VARIABLE  _drift_out)
		if(NOT _drift_rc EQUAL 0)
			message(FATAL_ERROR
				"patchset DRIFT in profile ${_drift_profile}: a patch is MISSING from the build tree.\n"
				"${_drift_out}\n"
				"Regenerate first: cd ${_drift_ws} && west patch verify --profile ${_drift_profile} && west patch apply --profile ${_drift_profile} --roll-back\n"
				"(stash manual worktree edits first). Override (packaging/CI): -DDARLING_SKIP_DRIFT_GATE=ON")
		endif()
		# (2) every-build check: an always-out-of-date target in ALL so a plain
		#     `ninja` re-runs the gate even when CMake does not reconfigure.
		add_custom_target(drift_gate ALL
			COMMAND ${WEST_PROGRAM} patch status --profile ${_drift_profile} --strict
			WORKING_DIRECTORY "${_drift_ws}"
			COMMENT "dar-gax: verifying ${_drift_profile} patchset is not drifted"
			VERBATIM)
	else()
		message(WARNING
			"DARLING_SKIP_DRIFT_GATE is OFF but the drift gate could not run "
			"(west=${WEST_PROGRAM}, profile=${_drift_profile}, ws=${_drift_ws}); proceeding WITHOUT drift protection.")
	endif()
endif()
# --- end drift gate ----------------------------------------------------------

add_subdirectory(src)

# --- dar-gax: wire the drift gate to the deployable targets ------------------
# Make the binaries we actually build+deploy DEPEND on the always-out-of-date
# drift_gate, so a TARGETED `ninja darlingserver` / `ninja system_kernel` runs
# the gate first and fails on drift -- not only a bare `ninja`. Placed after
# add_subdirectory(src) so the targets exist; TARGET-guarded for component
# selections that exclude them.
if(TARGET drift_gate)
	foreach(_dg_target darlingserver system_kernel)
		if(TARGET ${_dg_target})
			add_dependencies(${_dg_target} drift_gate)
		endif()
	endforeach()
endif()

# Rootless bootstrap is a source-owned component, not a list maintained by
# external test runners. Keep each guest entrypoint paired with the CMake target
# that produces it; the runtime runner consumes the generated manifest and
# resolves its Mach-O library closure from the resulting build tree.
set(DARLING_ROOTLESS_BOOTSTRAP_TARGETS
	darling
	mldr
	launchd
	launchctl
	shellspawn
	vchroot
	system_loader
	darlingserver
	system_kernel
	objc
	resolv-darwin
	CoreFoundation
	bash
)
foreach(_bootstrap_target IN LISTS DARLING_ROOTLESS_BOOTSTRAP_TARGETS)
	if(NOT TARGET ${_bootstrap_target})
		message(FATAL_ERROR "rootless bootstrap target is unavailable: ${_bootstrap_target}")
	endif()
endforeach()
add_custom_target(rootless_bootstrap DEPENDS ${DARLING_ROOTLESS_BOOTSTRAP_TARGETS})
set(_rootless_bootstrap_resource_dir "${CMAKE_BINARY_DIR}/rootless-bootstrap-resources")
file(MAKE_DIRECTORY
	"${_rootless_bootstrap_resource_dir}/System/Library/LaunchDaemons"
	"${_rootless_bootstrap_resource_dir}/private/etc"
)
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/shellspawn/org.darlinghq.shellspawn.plist"
	"${_rootless_bootstrap_resource_dir}/System/Library/LaunchDaemons/org.darlinghq.shellspawn.plist"
	COPYONLY
)
# MEASURED (2026-10-15, prefix-verify14): a ROOTLESS prefix's
# /System/Library/LaunchDaemons shows the UPPER (prefix) directory's entries ONLY -- the runtime
# root's other launchd plists (installed by their components into
# libexec/darling/System/Library/LaunchDaemons) are NOT visible to the guest, so the guest's launchd
# never loads those jobs. Measured in the guest: `ls -la /System/Library/LaunchDaemons/` printed
# ".", "..", "org.darlinghq.shellspawn.plist" and nothing else, while
# /usr/sbin/filecoordinationd (a lower-placement file with no upper counterpart) WAS visible.
#
# The consequence is not cosmetic: com.apple.FileCoordination has no receiver, so a client's
# xpc_connection_create_mach_service + resume cannot activate, and Foundation's
# -[NSFileCoordinator(Internal) submitIntent:onQueue:withBlock:] waits DISPATCH_TIME_FOREVER on a
# semaphore only the reply handler signals (src/external/foundation/src/NSFileCoordinator.m:378/414).
# xcodebuild's main thread parks there (bead dar-liaq, the ladder's file-coordination wall). The job
# must therefore be placed in the prefix like shellspawn's is.
# A launchd plist in the guest's /System/Library/LaunchDaemons must NOT be
# group- or world-writable: launchctl's path_goodness_check (support/launchctl.c) SKIPS such a file
# ("Dubious permissions on file (skipping)") and launchd then never imports the job at all, so its
# MachServices have no receiver. MEASURED 2026-10-15 (prefix-verify14, bead dar-liaq): with the plist
# placed mode 0775, launchd's own debug log listed no `Importing com.apple.FileCoordination.` and
# `bootstrap_look_up com.apple.FileCoordination` failed; with the SAME bytes at 0755 the log shows
# `System: Importing com.apple.FileCoordination.` and all three `Mach service added:` lines. The
# source file carries the execute bit and the workspace umask is 002, so COPYONLY placement is a
# mode lottery -- install the resource with an explicit 0644 instead.
file(COPY
	"${CMAKE_CURRENT_SOURCE_DIR}/src/external/foundation/services/filecoordinationd/com.apple.FileCoordination.plist"
	DESTINATION "${_rootless_bootstrap_resource_dir}/System/Library/LaunchDaemons"
	FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/etc/hosts"
	"${_rootless_bootstrap_resource_dir}/private/etc/hosts"
	COPYONLY
)
# Schema 2 makes older consumers reject unsupported lower-default placement.
file(GENERATE
	OUTPUT "${CMAKE_BINARY_DIR}/darling-rootless-bootstrap.json"
	CONTENT
"{
  \"schema\": 2,
  \"entrypoints\": [
    {\"target\": \"darling\", \"guest_path\": \"/bin/darling\", \"host_path\": \"$<TARGET_FILE:darling>\"},
    {\"target\": \"mldr\", \"guest_path\": \"/usr/libexec/darling/mldr\", \"host_path\": \"$<TARGET_FILE:mldr>\"},
    {\"target\": \"launchd\", \"guest_path\": \"/sbin/launchd\", \"host_path\": \"$<TARGET_FILE:launchd>\"},
    {\"target\": \"launchctl\", \"guest_path\": \"/bin/launchctl\", \"host_path\": \"$<TARGET_FILE:launchctl>\"},
    {\"target\": \"shellspawn\", \"guest_path\": \"/usr/libexec/shellspawn\", \"host_path\": \"$<TARGET_FILE:shellspawn>\"},
    {\"target\": \"vchroot\", \"guest_path\": \"/usr/libexec/darling/vchroot\", \"host_path\": \"$<TARGET_FILE:vchroot>\"},
    {\"target\": \"system_loader\", \"guest_path\": \"/usr/lib/dyld\", \"host_path\": \"$<TARGET_FILE:system_loader>\"},
    {\"target\": \"darlingserver\", \"guest_path\": \"/bin/darlingserver\", \"host_path\": \"$<TARGET_FILE:darlingserver>\"},
    {\"target\": \"bash\", \"guest_path\": \"/bin/bash\", \"host_path\": \"$<TARGET_FILE:bash>\"}
  ],
  \"resources\": [
    {\"target\": \"shellspawn_launchd_plist\", \"guest_path\": \"/System/Library/LaunchDaemons/org.darlinghq.shellspawn.plist\", \"host_path\": \"${_rootless_bootstrap_resource_dir}/System/Library/LaunchDaemons/org.darlinghq.shellspawn.plist\"},
    {\"target\": \"filecoordination_launchd_plist\", \"guest_path\": \"/System/Library/LaunchDaemons/com.apple.FileCoordination.plist\", \"host_path\": \"${_rootless_bootstrap_resource_dir}/System/Library/LaunchDaemons/com.apple.FileCoordination.plist\"},
    {\"target\": \"hosts_default\", \"guest_path\": \"/private/etc/hosts\", \"host_path\": \"${_rootless_bootstrap_resource_dir}/private/etc/hosts\", \"placement\": \"lower\"}
  ]
}
"
)

# Guest toolchain provisioning is deliberately a separate source-owned
# component. It may be expensive, but it must not enlarge the ordinary
# rootless regression provider. Keep binary entrypoints and non-Mach-O data in
# this manifest so West never has to duplicate install rules by hand.
option(DARLING_ROOTLESS_TOOLCHAIN
	"Build the source-owned rootless guest-toolchain component" OFF)
option(DARLING_ROOTLESS_HOMEBREW
	"Include the Homebrew userland in the rootless guest-toolchain component" OFF)
if(DARLING_ROOTLESS_HOMEBREW AND NOT DARLING_ROOTLESS_TOOLCHAIN)
	message(FATAL_ERROR "DARLING_ROOTLESS_HOMEBREW requires DARLING_ROOTLESS_TOOLCHAIN")
endif()
set(DARLING_ROOTLESS_TOOLCHAIN_TARGETS
	installer
	perl
	perl5.18
	perl5.28
	CFNetwork
	CoreFoundation_x86_64
	securitytool_macos
	coretls_cfhelpers
	# Runtime pieces the build tooling itself loads. Measured by running the real
	# Xcode's xcodebuild in a guest: it dies on the first one that is absent, in
	# this order -- /usr/lib/swift/libswiftCore.dylib, then PackageKit.framework,
	# then /usr/lib/libarchive.2.dylib. Each was buildable and installable all
	# along; they were only missing from this component, which is why a prefix
	# filled through the ordinary path did not carry them.
	archive
	PackageKit
	# MEASURED 2026-10-11, same discipline as the three above: a minimal xcodebuild
	# project in a guest died with
	#   "Error loading required libraries ... dlopen(@rpath/libxcodebuildLoader.dylib, 1):
	#    Library not loaded: /System/Library/Frameworks/UniformTypeIdentifiers.framework/
	#    Versions/A/UniformTypeIdentifiers  Referenced from: .../Xcode3Core.framework/...
	#    Reason: image not found"
	# (and then with SIGILL at __abort+0x15f, because the abort that reports it never
	# becomes a fatal signal -- see the abort-gap note). The framework was buildable and
	# installable all along -- its install rule had already put it under
	# libexec/darling -- and was only missing from this component, which is exactly why
	# a prefix filled through the ordinary path did not carry it. Xcode3Core hard-links
	# it at current version 709.0.0, the version this tree builds.
	UniformTypeIdentifiers
	# MEASURED 2026-10-12, same discipline as the four above: with the framework in place, the
	# minimal xcodebuild project moved to the next missing load-time dependency --
	#   "Library not loaded: /usr/lib/libRosetta.dylib  Referenced from:
	#    .../Xcode.app/Contents/SharedFrameworks/DVTFoundation.framework/Versions/A/DVTFoundation
	#    Reason: image not found"
	# It was not a declaration gap like the framework: libRosetta existed NOWHERE in the tree. The
	# tree now builds it (src/libRosetta, one symbol, see its header for the measured caller), and
	# AUDITED 2026-10-12, CORRECTING THIS COMMENT: the rung advanced while the demanded name did NOT exist.
	# DVTFoundation binds _rosetta_is_current_process_translated (one leading underscore) non-lazily, and the
	# artifact exported __rosetta_is_current_process_translated (two), because the C identifier was written with
	# a leading underscore of its own. Fixed under dar-8twt; recorded here because "the ladder advanced" was
	# accepted for a rung whose strong non-lazy demand was unsatisfied -- see the loader-leniency finding
	# (dar-q50c) for why that was possible at all.
	# three Xcode images reference it (SharedFrameworks/DVTFoundation,
	# SharedFrameworks/AssetRuntime/DVTFoundation, SharedFrameworks/CoreSymbolicationDT).
	Rosetta
	# MEASURED 2026-10-14, the rung after the platform scan: BOTH xcodebuild workloads clear the plugin
	# scan only to stop on a DEFERRED load --
	#   "Failed to load code for plug-in com.apple.dt.IDE.IDEiOSSupportCore (.../Contents/PlugIns/
	#    IDEiOSSupportCore.framework), error = ... (dlopen_preflight(.../IDEiOSSupportCore): Library not
	#    loaded: /usr/lib/liboah.dylib  Referenced from: .../IDEiOSSupportCore  Reason: image not found)"
	# NOT a declaration gap like the framework rungs: no component of this tree built that install name.
	# (The only liboah mention anywhere under src/ is an Apple Xcode project file,
	# src/external/IOKitUser/IOKitUser.xcodeproj/project.pbxproj -- not a CMake target and not a build
	# input.) The tree now builds it (src/liboah, ONE exported symbol). MEASURED PER REFERRER before the
	# image was written, because the referrers are not all the same shape: IDEiOSSupportCore has a STRONG
	# LC_LOAD_DYLIB and its 608-entry chained import table holds ZERO entries for that ordinal;
	# IDEFoundation has a WEAK load command and zero imports; IDEInterfaceBuilderCocoaTouchIntegration has
	# a STRONG load command and imports exactly _oah_is_current_process_translated; SymbolicationDT (and
	# the two DVTInstrumentsFoundation Resources images liboainject.dylib and RemoteInjectionAgent) only
	# dlopen the absolute path at run time and tolerate a missing handler. The remaining names of the
	# iPhoneOS SDK's liboah.tbd (Rosetta/AOT translation machinery) have no provider in this tree and are
	# deliberately NOT exported -- see src/liboah/src/oah.c. The load command demands compatibility
	# 1.0.0 and current 1.0.0 and the component declares exactly that, so no version claim of either kind.
	oah
	# MEASURED 2026-10-12, one rung further: with libRosetta in place DVTFoundation's next missing
	# load-time dependency is
	#   "Library not loaded: /System/Library/PrivateFrameworks/MobileAsset.framework/Versions/A/MobileAsset
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTFoundation.framework/Versions/A/DVTFoundation"
	# The tree builds it (src/private-frameworks/MobileAsset) and it was simply never declared here, so
	# it landed nowhere in the prefix -- the same declaration gap as the framework above. A plain
	# declaration with no version claim of either kind: the load command demands current 1.0.0 against
	# compatibility 1.0.0 and the tree declares exactly that, so both values are identical to the demand.
	MobileAsset
	# MEASURED 2026-10-12, the two rungs after MobileAsset, by probing a minimal xcodebuild project
	# one missing load-time dependency at a time and then building the target each error named:
	#   "Library not loaded: /System/Library/Frameworks/CoreData.framework/Versions/A/CoreData"
	#   "Library not loaded: /System/Library/Frameworks/ImageIO.framework/Versions/A/ImageIO"
	# both "Referenced from: .../SharedFrameworks/DVTFoundation.framework/Versions/A/DVTFoundation".
	# Each time the tree already linked the file (src/external/cocotron/CoreData,
	# src/frameworks/ImageIO, both C_SHARED_LIBRARY_LINKER outputs) and the only fault was that this
	# component never named it, so a prefix built through the ordinary path carried no copy -- the
	# same declaration gap as PackageKit and UniformTypeIdentifiers above. The rung after these two
	# is CryptoKit.framework, which NO target in this tree produces at all: it is the overlay build's
	# product (the ported recipe's own prebuilt CryptoKit.framework), i.e. a different class of work,
	# not another declaration here.
	# Both are plain declarations with no version claim of either kind: CoreData's load command demands
	# current 1522.1.0 against compatibility 1.0.0 and the tree declares current 752.0.0 against the
	# same compatibility, while ImageIO's demand and declaration are both 1.0.0. See the CoreData note
	# under the AssertionServices rung: that current-version shortfall is measured NOT to be enforced.
	CoreData
	ImageIO
	# Same declaration gap again, and the same fix: Xcode's DVTFoundation hard-depends on
	# /System/Library/PrivateFrameworks/CoreAnalytics.framework/Versions/A/CoreAnalytics, and the tree
	# builds it (src/private-frameworks/CoreAnalytics) but this component never named it. Measured after
	# CryptoKit was supplied: "Library not loaded: .../CoreAnalytics.framework/Versions/A/CoreAnalytics
	# Referenced from .../DVTFoundation.framework/Versions/A/DVTFoundation".
	# Plain declaration, no version claim of either kind: the demand is current 1.0.0 against
	# compatibility 1.0.0 and the tree declares exactly that.
	CoreAnalytics
	# MEASURED 2026-10-12, the rung after CoreAnalytics, same discipline: DVTFoundation's next missing
	# load-time dependency is
	#   "Library not loaded: /System/Library/PrivateFrameworks/AssertionServices.framework/Versions/A/AssertionServices
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTFoundation.framework/Versions/A/DVTFoundation
	#    Reason: image not found"
	# DECLARATION gap: the tree builds it (src/private-frameworks/AssertionServices) and this component
	# never named it. DVTFoundation binds one symbol from it, _BKSWatchdogAssertionCreateForPID,
	# through __DATA_CONST __got (eager, so dyld resolves it at load).
	#
	# MEASURED 2026-10-12, LATER AND CORRECTING THIS COMMENT: it used to say the framework's
	# DYLIB_CURRENT_VERSION was raised to 1008.40.4 because the load command demands it. That raise was
	# REVERTED -- dyld does not enforce the current version (proof: DVTFoundation demands current
	# 1522.1.0 of CoreData, the deployed CoreData declares 752.0.0, and the client loaded it anyway).
	# The declaration is what advanced this rung; the component's compatibility version already
	# matched, which is the value dyld enforces. See its CMakeLists for the full note.
	#
	# NOTE for the reader: the tree's implementation of that symbol is a STUB
	# (src/private-frameworks/AssertionServices/src/AssertionServices.m:98 prints "STUB: ..." and
	# returns NULL); that is pre-existing tree state, not something this declaration introduces.
	AssertionServices
	# MEASURED 2026-10-12, the rung after AssertionServices, same discipline and the same shape:
	#   "Library not loaded: /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTFoundation.framework/Versions/A/DVTFoundation
	#    Reason: image not found"
	# DECLARATION gap: the tree builds it (src/external/cocotron/CoreGraphics) and this component never
	# named it. DVTFoundation binds four symbols from it, _CGRectGetMaxX/_CGRectGetMaxY/_CGRectGetMinX/
	# _CGRectGetMinY, all through __DATA_CONST __got (eager). Unlike the two rungs above, the SYMBOLS
	# were never the problem: the tree implements all four (CGGeometry.h/CGGeometry.m).
	#
	# MEASURED 2026-10-12, LATER AND CORRECTING THIS COMMENT: it used to say the framework's
	# DYLIB_CURRENT_VERSION was raised to 1965.1.2 because the load command demands it. That raise was
	# REVERTED -- dyld does not enforce the current version (proof: DVTFoundation demands current
	# 1522.1.0 of CoreData, the deployed CoreData declares 752.0.0, and the client loaded it anyway).
	# The declaration is what advanced this rung, and the compatibility version -- the value dyld DOES
	# enforce -- already matched at 64.0.0.
	CoreGraphics
	# MEASURED 2026-10-12, the rung after CoreGraphics, same discipline:
	#   "Library not loaded: /System/Library/Frameworks/DiskArbitration.framework/Versions/A/DiskArbitration
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTFoundation.framework/Versions/A/DVTFoundation
	#    Reason: image not found"
	# DECLARATION gap and nothing else: the tree builds it (src/frameworks/DiskArbitration) and this
	# component never named it. DVTFoundation binds three symbols, _DASessionCreate,
	# _DASessionScheduleWithRunLoop and _DASessionUnscheduleFromRunLoop, all through __DATA_CONST __got
	# (eager), and the tree implements all three (DASession.c). Unlike the CoreGraphics and
	# AssertionServices rungs, the compatibility level already matches: the load command demands
	# current 1.0.0 against compatibility 1.0.0 and the framework declares exactly that, so no version
	# change was needed here.
	DiskArbitration
	# MEASURED 2026-10-12, the rung after DiskArbitration, same discipline:
	#   "Library not loaded: /System/Library/PrivateFrameworks/DiskManagement.framework/Versions/A/DiskManagement
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTFoundation.framework/Versions/A/DVTFoundation
	#    Reason: image not found"
	# DECLARATION gap and nothing else, the DiskArbitration shape: the tree builds it
	# (src/private-frameworks/DiskManagement) and this component never named it. DVTFoundation binds one
	# name from it, the Objective-C class _OBJC_CLASS_$_DMManager, through __DATA __objc_classrefs
	# rather than __got -- still a load-time requirement, because the class object has to exist in the
	# image for the runtime to fix that reference up.
	# AUDITED 2026-10-12, CORRECTING THIS COMMENT: it used to say "The tree implements it (DMManager.h and the
	# DiskManagement sources)". It does not. DMManager.m:22-32 is methodSignatureForSelector:/forwardInvocation:
	# only and logs "Stub called", so the class LOADS and forwards rather than answering -- a declaration over a
	# stub, which is the acceptable-but-must-be-named side of this family's policy and is now mechanically
	# classified as STUB-or-incomplete by scripts/closure_analyze.py. Version: the load command demands current 1.0.0 against compatibility
	# 1.0.0 and the framework declares exactly that, so no version change was needed.
	DiskManagement
	# MEASURED 2026-10-12, the rung after DiskManagement, same discipline -- and this one is MIXED:
	#   "Library not loaded: /System/Library/PrivateFrameworks/AppleSystemInfo.framework/Versions/A/AppleSystemInfo
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTFoundation.framework/Versions/A/DVTFoundation
	#    Reason: image not found"
	# The tree builds it (src/private-frameworks/AppleSystemInfo) and this component never named it, so
	# part of the gap is the usual DECLARATION one. DVTFoundation binds SEVEN names from it, all
	# through __DATA_CONST __got (eager): _ASI_BusSpeedInHz, _ASI_CPUSpeedInHz,
	# _ASI_CopyComputerModelName, _ASI_CopyCurrentCPUDescription,
	# _ASI_GetCurrentCPUDescriptionWithFormat, _ASI_NumberOfCPUs, _ASI_PhysicalMemorySize.
	#
	# Six of the seven existed only as stubs (functions.c prints "STUB: ..." and returns NULL);
	# they are pre-existing tree state, left untouched, and filed as their own task -- this rung does
	# not fix their answers. The seventh did not exist at all, which is why a declaration alone would
	# NOT have advanced the rung: dyld resolves all seven eagerly, so "image not found" would simply
	# have become "Symbol not found: _ASI_GetCurrentCPUDescriptionWithFormat". It is implemented for
	# real in src/private-frameworks/AppleSystemInfo/src/CurrentCPUDescription.c, from the only CPU
	# data this guest actually answers (hw.machine, hw.ncpu; machdep.cpu.brand_string and
	# hw.cpufrequency are unknown oids here), with the client's call site as the contract.
	#
	# AUDITED 2026-10-12, CORRECTING THIS COMMENT: only FOUR of the six are loud. Audited mechanically
	# (scripts/closure_analyze.py), two of the demanded names are SILENT stand-ins -- ASI_NumberOfCPUs
	# returns 1 and ASI_CopyComputerModelName returns @"UNIMPLEMENTED" without logging anything -- which is
	# the side of the family's policy that is NOT acceptable, because a caller cannot tell an answer from a
	# placeholder. Filed separately (dar-bnw5); the component as deployed answers six of the seven demanded
	# calls with a stand-in and is classified STUB-or-incomplete, not "mixed and fixed".
	# Version: the load command demands current 1.0.0 against compatibility 1.0.0 and the framework
	# declares exactly that, so no version claim was needed.
	AppleSystemInfo
	# MEASURED 2026-10-12, the rung after AppleSystemInfo, same discipline:
	#   "Library not loaded: /System/Library/PrivateFrameworks/AssetCacheServices.framework/Versions/A/AssetCacheServices
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTFoundation.framework/Versions/A/DVTFoundation
	#    Reason: image not found"
	# DECLARATION gap plus a compatibility-level claim, the CoreGraphics shape. The tree builds it
	# (src/private-frameworks/AssetCacheServices) and this component never named it. DVTFoundation
	# binds one name, the Objective-C class _OBJC_CLASS_$_ACSURLSession, through __DATA
	# __objc_classrefs (the DiskManagement mechanism, not __got) -- still a load-time requirement. The
	# class exists (src/.../ACSURLSession.m); note for the reader that its methods are LOUD
	# forwarding stubs (it logs "Stub called: ..." and does not implement the selector), which is
	# pre-existing tree state this rung does not fix.
	#
	# MEASURED 2026-10-12, LATER AND CORRECTING THIS COMMENT: it used to say the framework's
	# DYLIB_CURRENT_VERSION was raised to 136.0.0 because the load command demands it. That raise was
	# REVERTED -- dyld does not enforce the current version (proof: DVTFoundation demands current
	# 1522.1.0 of CoreData, the deployed CoreData declares 752.0.0, and the client loaded it anyway).
	# The declaration is what advanced this rung, and the compatibility version -- the value dyld DOES
	# enforce -- already matched at 1.0.0.
	AssetCacheServices
	# MEASURED 2026-10-12, the rung after AssetCacheServices, same discipline:
	#   "Library not loaded: /System/Library/PrivateFrameworks/DiskImages.framework/Versions/A/DiskImages
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTFoundation.framework/Versions/A/DVTFoundation
	#    Reason: image not found"
	# DECLARATION gap plus a current-version claim. The tree builds it
	# (src/private-frameworks/DiskImages) and this component never named it. DVTFoundation binds two
	# symbols, _DICopyStrError and _DIHLDiskImageAttach, through __DATA_CONST __got (back to the
	# ordinary mechanism after two classrefs rungs). Both exist, as LOUD STUBS in that component's
	# functions.c -- pre-existing tree state, named here and not fixed by this rung.
	#
	# MEASURED 2026-10-12, LATER AND CORRECTING THIS COMMENT: it used to say a current version was
	# added because the load command demands 680.0.0. That was REVERTED -- dyld does not enforce the
	# current version (proof: DVTFoundation demands current 1522.1.0 of CoreData, the deployed CoreData
	# declares 752.0.0, and the client loaded it anyway). The declaration is what advanced this rung.
	# The compatibility version is the enforced value, and ours already equalled the client's 1.0.8,
	# which is why it was deliberately not touched.
	DiskImages
	# MEASURED 2026-10-12, the rung after DiskImages -- and the FIRST ONE NOT REFERENCED BY DVTFoundation:
	#   "Library not loaded: /System/Library/PrivateFrameworks/CrashReporterSupport.framework/Versions/A/CrashReporterSupport
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/SymbolicationDT.framework/Versions/A/SymbolicationDT
	#    Reason: image not found"
	# DVTFoundation's own dependency chain is satisfied (measured: it references this framework zero
	# times, SymbolicationDT references it once), so the ladder has moved to the next image the client
	# loads. DECLARATION gap: the tree builds it (src/private-frameworks/CrashReporterSupport) and this
	# component never named it. SymbolicationDT binds one symbol, _CRCopySanitizedPath, through
	# __DATA_CONST __got (the ordinary mechanism); it exists as a LOUD STUB in that component's
	# functions.c -- pre-existing tree state, named and not fixed.
	#
	# VERSION, deliberately NOT claimed, and this is the interesting part: the load command demands
	# current 15138.0.0 against compatibility 1.0.0 while the component declares 827.0.0. Measured
	# evidence says dyld does NOT enforce the current version: DVTFoundation demands current 1522.1.0
	# of CoreData, the deployed CoreData declares current 752.0.0, and the client loaded it and moved
	# on. So a current-version claim here would be neither enforced nor a statement we can defend --
	# the symbol behind it is a stub, not an implementation. The declaration is the whole fix.
	CrashReporterSupport
	# MEASURED 2026-10-12, the rung after CrashReporterSupport, and the second one from the NEW
	# referrer rather than DVTFoundation:
	#   "Library not loaded: /usr/lib/libcompression.dylib
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/SymbolicationDT.framework/Versions/A/SymbolicationDT
	#    Reason: image not found"
	# DECLARATION gap, and note this is a dylib at /usr/lib rather than a framework -- the guest path is
	# the dylib's own install name (src/libcompression sets DYLIB_INSTALL_NAME to exactly that). The
	# tree builds it (target `compression`, producing libcompression.dylib) and this component never
	# named it. SymbolicationDT binds three symbols, _compression_stream_destroy,
	# _compression_stream_init and _compression_stream_process, through __DATA_CONST __got (eager),
	# and the tree IMPLEMENTS them (src/libcompression/src/compression.c, no stubs).
	# Version: the load command demands current 1.0.0 against compatibility 1.0.0 and the tree declares
	# exactly that, so no version claim of either kind was needed.
	compression
	# MEASURED 2026-10-12, the rung after compression, and the first one that is NOT a declaration gap
	# and NOT an existing implementation:
	#   "Library not loaded: /usr/lib/libAppleArchive.dylib
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DebugSymbolsDT.framework/Versions/A/DebugSymbolsDT
	#    Reason: image not found"
	# The tree had no libAppleArchive at all, so a new component was needed. It is a LOUD PLACEHOLDER,
	# deliberately with no symbol surface: DebugSymbolsDT (the ONE referrer -- see the source comment for
	# the substring error that once said DVTFoundation) has a non-weak load command and binds ZERO
	# symbols, so an image with the right install name satisfies it, while any future client that binds a
	# real AA* symbol fails at LOAD rather than calling something that answers wrongly. The real Apple
	# Archive format API (AAArchiveStream and the extraction/compression operations) is unimplemented and
	# filed as separate work. Both versions are identical to the demand (1.0.0/1.0.0), so no version
	# claim of either kind was made.
	AppleArchive
	# MEASURED 2026-10-12, the rung after libAppleArchive, and it is a plain declaration gap again --
	# the FIFTH distinct referrer is what is new:
	#   "Library not loaded: /System/Library/PrivateFrameworks/PlugInKit.framework/Versions/A/PlugInKit
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTInstrumentsFoundation.framework/Versions/A/DVTInstrumentsFoundation
	#    Reason: image not found"
	# The tree builds it (src/private-frameworks/PlugInKit) and this component never named it.
	# DVTInstrumentsFoundation binds three things, _PKIdentifierAttribute through __DATA_CONST __got and
	# the classrefs _OBJC_CLASS_$_PKManager and _OBJC_CLASS_$_PKHost -- the classref mechanism again,
	# after AssertionServices and AssetCacheServices. All three exist in the tree, which is what the
	# declaration needed.
	# AUDITED 2026-10-12, CORRECTING THIS COMMENT: it used to call this "a real framework with PKBundle/PKHost/
	# PKManager/PKDaemonClient implementations, not stubs" and to say "PKManager.m and PKHost.m implement the
	# classes". The two DEMANDED classrefs do not: both are methodSignatureForSelector:/forwardInvocation: only
	# (the "Stub called" forwarding pattern), so the client can load and message them and gets forwarding rather
	# than behaviour. Others in the component may be real; the demanded ones are not. Mechanically classified as
	# STUB-or-incomplete by scripts/closure_analyze.py.
	# Plain declaration, no version claim of either kind: the demand is current 1.0.0 against
	# compatibility 1.0.0 and the tree declares exactly that.
	PlugInKit
	# MEASURED 2026-10-12, the rung after PlugInKit, from the same new referrer:
	#   "Library not loaded: /usr/lib/libdtrace.dylib
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTInstrumentsFoundation.framework/Versions/A/DVTInstrumentsFoundation
	#    Reason: image not found"
	# DECLARATION gap, and this one is comfortable: the tree ALREADY builds a real libdtrace
	# (src/external/dtrace, the Apple dtrace source, target `libdtrace`, DYLIB_INSTALL_NAME already
	# /usr/lib/libdtrace.dylib with its own install rule) and this component never named it, so it
	# landed nowhere in the prefix. DVTInstrumentsFoundation has a load command for it and binds ZERO
	# symbols from it -- the same symbol-free shape as libAppleArchive, except that here the
	# implementation genuinely exists, so nothing needs to be written.
	# Plain declaration, and the version needs stating precisely rather than as "declares exactly that":
	# MEASURED from the deployed guest image, the COMPATIBILITY version is 1.0.0 and matches the demand
	# exactly -- that is the value dyld compares -- while the CURRENT version is 130.0.0, the tree's own
	# value, which EXCEEDS the 1.0.0 the client asks for. Nothing was raised to meet a demand here.
	libdtrace
	# MEASURED 2026-10-12, the rung after libdtrace, from the same referrer:
	#   "Library not loaded: /System/Library/PrivateFrameworks/NetworkStatistics.framework/Versions/A/NetworkStatistics
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTInstrumentsFoundation.framework/Versions/A/DVTInstrumentsFoundation
	#    Reason: image not found"
	# DECLARATION gap, and the widest symbol surface of the ladder so far: the tree builds it
	# (src/private-frameworks/NetworkStatistics) and this component never named it.
	# DVTInstrumentsFoundation binds THIRTY symbols from it, all through __DATA_CONST __got (eager) --
	# _NStatManagerCreate/_NStatManagerDestroy/_NStatManagerAddAllTCP/_NStatManagerAddAllUDP/
	# _NStatManagerQueryAllSources/_NStatSourceCopyProperty and the rest. All thirty are present in the
	# tree, which is why the rung is a declaration and not an implementation task.
	# NOTE for the reader: functions.c implements them as LOUD STUBS (37 "STUB" sites, the
	# DiskImages/AssertionServices style); that is pre-existing tree state this rung does not fix.
	# Plain declaration, no version claim: measured from the deployed image, current 1.0.0 and
	# compatibility 1.0.0, identical to the demand.
	NetworkStatistics
	# MEASURED 2026-10-12, the rung after NetworkStatistics, from the same referrer:
	#   "Library not loaded: /System/Library/PrivateFrameworks/kperf.framework/Versions/A/kperf
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTInstrumentsFoundation.framework/Versions/A/DVTInstrumentsFoundation
	#    Reason: image not found"
	# DECLARATION gap: the tree builds it (src/private-frameworks/kperf) and this component never
	# named it. DVTInstrumentsFoundation binds FIFTY symbols from kperf itself, all eagerly through
	# __DATA_CONST __got, and all fifty are present in the tree.
	#
	# READ THIS BEFORE COUNTING ITS DEMAND BY SUBSTRING: DVTInstrumentsFoundation binds 79 symbols
	# whose dependency name contains "kperf", but 29 of those belong to a DIFFERENT dependency,
	# @rpath/kperfdataDT.framework/Versions/A/kperfdataDT (Xcode's own file, not ours) -- _kpep_db_create,
	# _kpdecode_cursor_create and the rest. Count by the dependency field, not by substring, or kperf
	# looks like an implementation task when it is not. That mistake was made once here and corrected.
	#
	# NOTE for the reader: the tree implements those fifty as LOUD STUBS (108 stub sites in kperf.c),
	# the DiskImages/AssertionServices style -- the rung loads and prints rather than works, and that is
	# pre-existing tree state this declaration does not change.
	# Plain declaration, no version claim: measured from the deployed image, current 1.0.0 and
	# compatibility 1.0.0, identical to the demand.
	kperf
	# MEASURED 2026-10-12, the rung after kperf, from the same referrer:
	#   "Library not loaded: /System/Library/PrivateFrameworks/StreamingZip.framework/Versions/A/StreamingZip
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTInstrumentsFoundation.framework/Versions/A/DVTInstrumentsFoundation
	#    Reason: image not found"
	# DECLARATION gap: the tree builds it (src/private-frameworks/StreamingZip) and this component never
	# named it. DVTInstrumentsFoundation binds three symbols, counted by the dependency field rather
	# than by substring: _kSZArchiverOptionNoCentralDirectory and _kSZArchiverOptionNoSeekOutput (globals,
	# globals.c) and _SZArchiverCreateStreamableZip (a LOUD STUB, functions.c:29 -- 7 stub sites in the
	# component, the usual style). All three present, so this is a declaration and not an
	# implementation task; the archive work behind it stays under the stub filing policy.
	# Plain declaration, no version claim: measured from the deployed image, current 1.0.0 and
	# compatibility 1.0.0, identical to the demand.
	StreamingZip
	# MEASURED 2026-10-12, the rung after StreamingZip, and the first of the decision points rather
	# than a declaration gap:
	#   "Library not loaded: /System/Library/Frameworks/Metal.framework/Versions/A/Metal
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTInstrumentsFoundation.framework/Versions/A/DVTInstrumentsFoundation
	#    Reason: image not found"
	# The tree HAS a real Metal (src/external/metal, Vulkan-backed). That rung declared MetalNoGPU
	# -- a TRUE-ANSWER implementation: this guest has no Metal device, so "no device exists" is a
	# fact rather than a placeholder, THE THIRD SHAPE (libRosetta and this; libAppleArchive's empty
	# surface is the second, the loud stubs the first). It does NOT print a "STUB:" line. DVT
	# InstrumentsFoundation binds seven symbols, all eager: four calls it makes
	# (_MTLCreateSystemDefaultDevice 2 sites, _MTLCopyAllDevices 4, _MTLCopyAllDevicesWithObserver 1,
	# _MTLRemoveDeviceObserver 2) and three notification NAMES it only reads. Neither call site of
	# _MTLCreateSystemDefaultDevice tests for NULL -- one messages the result, the other sends it
	# -[%rdi _removeGPU:] -- and messaging nil is a no-op, so the client tolerates the empty answer.
	# See MetalNoGPU's source for that measurement, the no-op reasoning for
	# _MTLRemoveDeviceObserver, and why its three notification values are arbitrary but consistent.
	#
	# CORRECTED MEASURED 2026-10-12, the rung after QuartzCore: that rung's premise was FALSE for
	# this configuration, and the declaration it produced made a real component unreachable.
	#   (1) It said the real Metal "is gated behind COMPONENT_dev_gui_common and nothing is built".
	#       The gate that matters is BUILD_METAL (ENABLE_METAL=AUTO + llvm-config + Vulkan), and in
	#       this tree it is ON: build/verify14/src/external/metal/Metal is built, 1.2 MB fat,
	#       exporting 238 names including its 34 Objective-C classes and all seven symbols
	#       DVTInstrumentsFoundation binds. QuartzCore links it (Metal_private) and links
	#       indium_private, and the deploy's closure resolver had ALREADY pulled libindium.dylib
	#       and libiridium.dylib into the prefix for QuartzCore -- so the GPU stack the old note
	#       wanted to keep out of the bootstrap was in the guest already. MetalNoGPU changed only
	#       which image answered the install name.
	#   (2) THE SHADOWING, which is the defect this file now records: both images claim
	#       /System/Library/Frameworks/Metal.framework/Versions/A/Metal, and declaring MetalNoGPU
	#       for that path made the built real Metal unreachable. The loader then stopped on
	#       QuartzCore, whose __DATA_CONST __objc_classrefs binds _OBJC_CLASS_$_MTLTextureInternal
	#       EAGERLY (its one other Metal bind, _MTLCreateSystemDefaultDevice, is lazy): a class only
	#       the real Metal defines, at the ABI the tree's own Metal implements
	#       (MTLTextureInternal : NSObject <MTLTexture>, -initWithTexture:device:resourceOptions:,
	#       the initializer QuartzCore's CAMetalDrawableInternal actually sends -- i386 does not
	#       reference Metal classes at all). MetalNoGPU can answer a function truthfully; it cannot
	#       become a texture, so the honest image here is the one the tree already has.
	# SELECTED, NOT NAMED: DARLING_ROOTLESS_METAL_TARGET is Metal when BUILD_METAL and MetalNoGPU
	# otherwise (set right after the BUILD_METAL decision). The MetalNoGPU path is unchanged for the
	# configurations that cannot build the real one; this configuration is the exception, not the
	# new rule.
	${DARLING_ROOTLESS_METAL_TARGET}
	# MEASURED 2026-10-12, the rung after Metal, from the same referrer:
	#   "Library not loaded: /System/Library/PrivateFrameworks/LoggingSupport.framework/Versions/A/LoggingSupport
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTInstrumentsFoundation.framework/Versions/A/DVTInstrumentsFoundation
	#    Reason: image not found"
	# DECLARATION gap, with ONE class added so the declaration can hold: the tree builds this component
	# (src/private-frameworks/LoggingSupport, 58 .m files) and this component never
	# named it. DVTInstrumentsFoundation binds SEVEN classrefs from it, all eager in __DATA
	# __objc_classrefs -- OSLogEventLiveStore, OSLogEventLiveStream,
	# OSLogEventStore, OSLogEventStream, OSLogPreferencesCategory, OSLogPreferencesSubsystem, plus the
	# seventh below -- and the
	# SEVENTH, OSLogDeserializedEventStream, was the only class the tree did not have. Because the
	# classref is eager, declaring without it would have turned a clear image-not-found into a
	# symbol-not-found (the kperf lesson), so it was added: a DOCUMENTED METHOD-FREE class, the fourth
	# shape this walk has produced, whose absence of behaviour is deliberate and named in its source
	# (the client binds the classref and sends it zero selectors anywhere in the image).
	# VERSION: measured from the deployed image, compatibility 1.0.0 matching the demand, current
	# 906.220.1 against a demand of 1815.60.2 -- a current shortfall deliberately NOT raised, since
	# dyld does not enforce the current version (the CoreData proof). Same decision as the four reverts.
	LoggingSupport
	# MEASURED 2026-10-12, the rung after LoggingSupport, from the same referrer -- and the first of the
	# two gaps that were known ahead of time and deliberately classified only when the ladder arrived:
	#   "Library not loaded: /System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTInstrumentsFoundation.framework/Versions/A/DVTInstrumentsFoundation
	#    Reason: image not found"
	# DECLARATION gap plus FOUR small additions, and the additions are two of the shapes this walk has
	# settled on, not new ones. The component is real and substantial (src/external/cocotron/QuartzCore,
	# 39 .m files of CA* classes) and was never named here. DVTInstrumentsFoundation binds four symbols
	# eagerly, none of which existed anywhere in the component:
	#   three FACT-ANSWER functions (Metal's shape) -- _CARenderServerGetFrameCounter,
	#   _CARenderServerGetDebugOption, _CARenderServerSetDebugOption. Both frame-counter call sites pass
	#   zero and STORE the result into DTGraphicsService._lastFPSCount with nothing testing or branching
	#   on it, so "zero frames rendered" is a fact about a guest with no render server. The debug option
	#   REGISTER IS HONOURED (set-then-get returns what was set; its initial value is zero because there
	#   is no server holding state), never a "STUB:" line.
	#   one DOCUMENTED METHOD-FREE class (LoggingSupport's shape) -- _OBJC_CLASS_$_CADisplay, to which
	#   the client sends zero selectors anywhere in the image.
	# AUDITED 2026-10-12, ADDING WHAT THIS COMMENT DID NOT KNOW: this rung is classified REAL for the four
	# symbols DVTFoundation demands, and it is the only component of the walk in that class -- but a DIFFERENT
	# referrer, DVTInstrumentsFoundation, also links CoreGraphics strongly and binds _CGSAcceleratorForDisplayNumber
	# non-lazily, and that symbol is in neither the artifact nor the cocotron source. The rung advanced with it
	# absent (see the loader-leniency finding dar-q50c); the symbol is being added under dar-8twt. Nothing here
	# is retracted -- the point is that "this rung is real" describes the four symbols it was declared for, not
	# every symbol any client may bind from the same image. FIXED: the symbol was added as a truthfully headless
	# answer (cocotron, committed and pinned), and the analyzer now reports MISS 0 for this component.
	# VERSION: compatibility 1.2.0 already MATCHES the client's demand -- the enforced value, so no
	# version work and no claim -- while the current version (1.11.0 against a demand of 1193.38.0) is
	# deliberately left alone under the CoreData proof, the same decision as the four version reverts.
	QuartzCore
	# MEASURED 2026-10-12, the rung after QuartzCore, from the same referrer -- the SECOND of the two
	# gaps known ahead of time, classified only now that the ladder arrived:
	#   "Library not loaded: /System/Library/Frameworks/CoreLocation.framework/Versions/A/CoreLocation
	#    Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTInstrumentsFoundation.framework/Versions/A/DVTInstrumentsFoundation
	#    Reason: image not found"
	# DECLARATION gap plus TWO method-free classes, which is the LoggingSupport shape twice and the
	# smallest addition this walk has needed. The component is real (src/frameworks/CoreLocation, sixteen
	# .m files of CL* classes) and was never named here. DVTInstrumentsFoundation binds exactly two
	# classrefs from it, both eager in __DATA __objc_classrefs: _OBJC_CLASS_$_CLLocation and
	# _OBJC_CLASS_$_CLSimulationManager. Both had HEADERS in the tree and no implementations -- and both
	# headers declare an @interface with NO methods, so method-free was already the tree's written
	# decision and these files only supply the class symbols. The client sends neither class anything:
	# zero references to either name anywhere in the image's code.
	# VERSION: compatibility 1.0.0 matches the demand (the enforced value, so no claim); the current
	# version (tree 2245.16.14 against a demand of 3064.0.3) is left alone under the CoreData proof.
	CoreLocation
	#
	# MEASURED 2026-10-12, supplied as a WAVE (scripts/closure_analyze.py --sweep) rather than one rung
	# at a time: these six are DECLARATION rows -- the tree already builds each one, the strong
	# referrer's demanded symbols are all present in the built artifact (demanded-not-exported = 0 for
	# every one), and in each case the COMPATIBILITY version matches the load command's demand while the
	# current shortfall is left unraised under the CoreData proof (current_version is not enforced).
	#   ApplicationServices -- strong referrer DevToolsCore binds 0; the sweep's 18+1 names are behind
	#     weak+upward XCTAutomationSupport/XCUIAutomation, and the weak AX* names all exist in the built
	#     HIServices (489 exports). Demand 1.0.0/66.0.0 against a tree 1.0.0/48.0.0.
	#   AuthKit -- DVTAppStoreConnect 1, DVTITunesSoftwareServiceFoundation 2, Notarization 2 (union 2,
	#     both exported). Demand and declaration both 1.0.0/1.0.0.
	#   login -- IDEFoundation binds 1, present. Demand 1.0.0/264.1.2 against a tree 1.0.0/137.0.0.
	#     NAME COLLISION to keep in mind: this target is the FRAMEWORK (src/private-frameworks/login);
	#     the system_cmds executable is target login-exe with OUTPUT_NAME login, so `ninja login` builds
	#     the executable while $<TARGET_FILE:login> is the framework.
	#   exslt -- Localization binds 1, present (17 exports). Demand 9.0.0/9.15.0; the tree declares
	#     compat 9.0.0, and its DYLIB_CURRENT_VERSION is misspelled, so current is unset (separate nit).
	#   xslt -- Localization binds 0 symbols. Demand and declaration 3.0.0/3.26.0 (245 exports).
	#   sqlite3 -- IDEFoundation 14, AppThinning 4, DNTDocumentationSupport 24, XCResultKit 36 (union 41,
	#     all present, 255 exports). Demand 9.0.0/377.0.0 against a tree 9.0.0/252.0.0. The host file is
	#     libsqlite3.0.dylib (its own LC_ID) while every client names /usr/lib/libsqlite3.dylib.
	ApplicationServices
	AuthKit
	login
	exslt
	xslt
	sqlite3
	#
	# MEASURED 2026-10-12, the Accelerate rung -- the one whose gap is a SYMBOL, not just an image:
	#   Library not loaded: /System/Library/Frameworks/Accelerate.framework/Versions/A/Accelerate
	#   Referenced from: .../Xcode.app/Contents/SharedFrameworks/DVTSmartSearch.framework/Versions/A/DVTSmartSearch
	# DVTSmartSearch binds twelve symbols from the `Accelerate` dependency field, all STRONG and
	# NON-LAZY (chained imports 0x0..0xB, __DATA_CONST,__got), so declaring the framework without them
	# would turn "image not found" into "symbol not found": nine are in the built libvDSP (455 exports:
	# _vDSP_distancesq, _vDSP_dotpr, _vDSP_maxv, _vDSP_minv, _vDSP_svesq, _vDSP_vadd, _vDSP_vintb,
	# _vDSP_vsdiv, _vDSP_vsmul) and three were ABSENT from libBNNS's twelve exports and from its source,
	# so libBNNS now really implements them (BNNSCreateRandomGenerator, BNNSDestroyRandomGenerator,
	# BNNSRandomFillUniformFloat -- AES-128 in counter mode, the single method BNNSRandomGeneratorMethod
	# defines; the twelve pre-existing stubs are untouched).
	# The Accelerate umbrella exports nothing itself: its value is its LC_REEXPORT_DYLIB of vecLib, and
	# vecLib in turn re-exports the nine sub-dylibs below, so the whole chain has to be guest-visible for
	# those nine names to resolve. Guest paths are the built artifacts' own LC_ID install names, read
	# from build/verify14 rather than transcribed. Nothing here needs building: the tree already builds
	# all eleven images.
	Accelerate
	vecLib
	vMisc
	vDSP
	BLAS
	LAPACK
	LinearAlgebra
	SparseBLAS
	Quadrature
	BNNS
	Sparse
	# MEASURED 2026-10-14, the rung after the platform scan (both workloads advanced to this next
	# wall at once). libobjc dlopens this image BY ABSOLUTE NAME the first time a block-based
	# trampoline is used:
	#   objc[<pid>]: couldn't dlopen libobjc-trampolines.dylib:
	#     dlopen(/usr/lib/libobjc-trampolines.dylib, 262): image not found
	# at src/external/objc4/runtime/objc-block-trampolines.mm:173, whose _objc_fatal arm aborts
	# (abort_with_payload; both xcodebuild workloads returned XCB_RC=134). It is the same
	# invisible-to-the-loader class as the ICU data file: nothing ever links it, so no LOAD command
	# names it and the Mach-O closure resolver cannot reach it. It was a DECLARATION gap and nothing
	# else -- the tree builds and installs the image (its own LC_ID is exactly the demanded
	# /usr/lib/libobjc-trampolines.dylib, and it is the built objc4 runtime's sibling).
	objc-trampolines
	# MEASURED 2026-10-14, the rung after liboah/NSNumberFormatterCompatibility. With liboah in place
	# the same dlopen_preflight walk of Xcode's iOS platform plug-in advanced to the NEXT dependency
	# in its own LC_LOAD_DYLIB list:
	#   Failed to load code for plug-in com.apple.dt.IDE.IDEiOSSupportCore ... (dlopen_preflight(
	#   .../IDEiOSSupportCore): Library not loaded:
	#     /System/Library/PrivateFrameworks/MobileDevice.framework/Versions/A/MobileDevice
	#     Referenced from: .../IDEiOSSupportCore   Reason: image not found)
	# The referrer is Contents/PlugIns/IDEiOSSupportCore.framework/Versions/A/IDEiOSSupportCore
	# (sha256 3d941fe8f7fec44db944850aebe67d93a3a389559b7794162ea450c33b78fd5e), which carries a
	# STRONG, non-weak LC_LOAD_DYLIB for it at
	# compatibility 1.0.0 / current 1818.60.2 and binds TWELVE strong eager names from it
	# (_AMDGetBundleVersion, _AMDServiceConnectionGet{Socket,TypeID}, _AMDServiceConnection{Invalidate,
	# Receive,Send}, _AMS{Cleanup,Initialize}, _AMS{ConnectTo,DisconnectFrom}CrashReportCopyTarget,
	# _AMSCopyCrashReportsFromTarget, _SZArchiverCreateStreamableZip) -- so the image cannot be a file
	# that merely exists. It is the objc-trampolines declaration gap again: the tree BUILDS the image
	# (src/private-frameworks/MobileDevice), its own LC_ID is exactly the demanded path, and the
	# toolchain component named neither it nor its guest path, so no prefix filled through the
	# ordinary path carried it (26 private frameworks were present in each root; MobileDevice was not
	# among them). Nothing else about it is special -- it is not a second source-authoring rung.
	MobileDevice
	# MEASURED 2026-10-14, the rung after MobileDevice. With MobileDevice in place the plug-in scan
	# advanced to the NEXT image of the same bundle, Xcode's own
	# Contents/Frameworks/IDESimulatorFoundation.framework/Versions/A/IDESimulatorFoundation
	# (sha256 83db461982c7f9846adf4a160baaa6b6d73925852a3d3f2429d6756b84873c0f, 1,072,192 bytes), and stopped on the FIRST absolute-path dependency its
	# LC_LOAD_DYLIB list names:
	#   DVTPlugInLoading: Failed to load code for plug-in com.apple.dt.IDESimulatorFoundation ...
	#     (dlopen_preflight(.../IDESimulatorFoundation): Library not loaded:
	#       /Library/Developer/PrivateFrameworks/CoreSimulator.framework/Versions/A/CoreSimulator
	#       Reason: image not found)
	# The load command is STRONG (LC_LOAD_DYLIB, compatibility 1.0.0, current 1051.17.8) and the
	# referrer binds EIGHT names from it -- six ObjC class refs (_OBJC_CLASS_$_SimDiskImage,
	# _OBJC_CLASS_$_SimDiskImageIdentifier, _OBJC_CLASS_$_SimDiskImageManager,
	# _OBJC_CLASS_$_SimRuntime, _OBJC_CLASS_$_SimServiceContext, _OBJC_CLASS_$_SimUsageTracker) and
	# _SimDiskImageStateDisplayString/_SimDiskImageStateIsTerminal -- so the image cannot be a file
	# that merely exists. Seven of the eight are exported by the built image; the eighth is the next
	# wall and is named in the rung's evidence. It is the MobileDevice declaration gap exactly: the
	# tree BUILDS this image (src/private-frameworks/CoreSimulator, registered under
	# COMPONENT_dev_gui_stubs_common), its own LC_ID is exactly the demanded path
	# (compatibility 1.0.0, current 885.2.0 -- dyld checks the COMPATIBILITY version, so the demanded
	# current version 1051.17.8 is informational), and the toolchain component named neither it nor
	# its guest path, so neither prefix root carried it.
	CoreSimulator
	# MEASURED 2026-10-15, the rung after the lane-memfd fix (the fd-collision defect whose fix let
	# the child past the lane attach). With that fix in place the child reached dyld's own init and
	# died on a STRONG demand, quoted from the child's own core (/tmp/t4cores/core.484280):
	#   Library not loaded: /System/Library/Frameworks/OSLog.framework/Versions/A/OSLog
	#   Referenced from: .../SwiftBuild.framework/Versions/A/PlugIns/SWBBuildService.bundle/
	#                    Contents/Frameworks/SWBBuildSystem.framework/Versions/A/SWBBuildSystem
	#   Reason: image not found        (stack: _dyld_start -> dyld::_main -> dyld::halt)
	# THE SET, NOT ONE RUNG: `scripts/closure_analyze.py --sweep` over the SWBBuildService bundle's
	# images (110 images reached) gives the whole wave this closure is missing -- 16 xcode-provided,
	# 4 weak-only (libswiftIOKit, libswiftCompression, libRemarks, ExecutionPolicy: every referrer
	# LC_LOAD_WEAK_DYLIB, so not blockers) and exactly TWO strong rows, both from SWBBuildSystem:
	#   /System/Library/Frameworks/OSLog.framework/Versions/A/OSLog  2 demanded symbols
	#       _OBJC_CLASS_$_OSLogMessageComponent, _OBJC_CLASS_$_OSLogStore
	#   /usr/lib/swift/libswiftOSLog.dylib                          1 demanded symbol
	#       _$sSo10OSLogStoreC0A0E10getEntries4with2at8matchings11AnySequenceVySo0A5EntryCG...
	# and the second of those is answered by the Swift overlay in the payload (see the overlay
	# entry), so this image is the wall. Tree-wide (`--referrers tree` over every Xcode subtree) the
	# demand on this install name is three classrefs: the two above plus _OBJC_CLASS_$_OSLogEntryLog
	# (IDESourceEditor, SymbolCacheSupport). Load command: STRONG LC_LOAD_DYLIB, compatibility
	# 1.0.0 / current 1815.60.2 -- the versions below.
	# REAL, NOT A STUB, AND WHY IT IS BACKED BY ASL: Apple's OSLog.framework reads the tracev3
	# unified log through LoggingSupport's index/chunk readers, and THIS TREE HAS NEITHER HALF of
	# that path -- measured 2026-10-15, every file of src/private-frameworks/LoggingSupport/src/
	# _OSLog*.m, OSLogEventStore.m, OSLogEventStream.m and OSLogEventProxy.m is a 32-line
	# methodSignatureForSelector:/forwardInvocation: forwarding stub, in this tree and in the
	# VibeDarling reference checkout alike, and libsystem_trace's signpost entry points assert on
	# call. What this tree does have is the writer: libsystem_trace's _os_log_impl decodes every
	# os_log message and writes it into ASL (the "org.puredarwin.os_log" client, with the
	# "os_log(3)" marker, Subsystem, Category, the argument buffer's hex, the decoded message and
	# asl_base_msg's Time/Sender/PID/Level). So the framework's store side reads THAT store, through
	# libsystem_asl (Apple's real syslog implementation, 14.5k lines in src/external/syslog), and
	# the entry values it hands back -- date, composed message, subsystem, category, level, process,
	# pid, sender -- are the record's own. The store's exclusions are documented in the source and
	# in the rung's evidence rather than filled in: the ASL record carries no format string (so
	# OSLogEntryWithPayload.formatString is empty and components is empty), no thread id, no
	# activity id, and the ASL database has no store tiers (storeCategory Undefined).
	OSLog
)
if(DARLING_ROOTLESS_TOOLCHAIN)
	set(_missing_toolchain_targets)
	foreach(_toolchain_target IN LISTS DARLING_ROOTLESS_TOOLCHAIN_TARGETS)
		if(NOT TARGET ${_toolchain_target})
			list(APPEND _missing_toolchain_targets ${_toolchain_target})
		endif()
	endforeach()
	if(_missing_toolchain_targets)
		message(FATAL_ERROR
			"rootless toolchain component requested but source targets are unavailable: "
			"${_missing_toolchain_targets}")
	endif()
	add_custom_target(rootless_toolchain DEPENDS ${DARLING_ROOTLESS_TOOLCHAIN_TARGETS})
	set(_rootless_toolchain_resource_dir "${CMAKE_BINARY_DIR}/rootless-toolchain-resources")
	file(MAKE_DIRECTORY
		"${_rootless_toolchain_resource_dir}/usr/share/man/man8"
		"${_rootless_toolchain_resource_dir}/usr/bin"
	)
	configure_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/src/external/installer/installer.8"
		"${_rootless_toolchain_resource_dir}/usr/share/man/man8/installer.8"
		COPYONLY
	)
	configure_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/src/external/perl/fix/dummy.pl"
		"${_rootless_toolchain_resource_dir}/usr/bin/c2ph"
		COPYONLY
	)
	set(_rootless_toolchain_extra_entrypoints "")
	set(_rootless_toolchain_extra_resources "")
	set(_rootless_toolchain_manifest "${CMAKE_BINARY_DIR}/darling-rootless-toolchain.json")
	# The Swift runtime is source-owned data (the project fetches it with git-lfs
	# from swift.org), so it belongs in this manifest as resources rather than
	# being left to an install rule the deploy never reads. The deploy refuses a
	# host path outside the build tree (measured: "resource 'swift_runtime'
	# escapes build root"), so stage them into this component's resource
	# directory first -- the same reason the Homebrew module stages Perl with
	# configure_file COPYONLY before naming it in a manifest.
	#
	# Declare the whole runtime the project carries, discovered rather than listed. What used to be a
	# MEASURED MINIMUM was a property of the 5.2.2 payload, not of this design: its 44 dylibs were
	# per-framework OVERLAYS (libswiftAVFoundation, libswiftAppKit, ...), each a dependency root on a
	# system framework this tree does not build, so naming them all failed. Swift 6.2.4 ships 15
	# dylibs instead -- the overlays moved into the frameworks themselves -- and those 15 depend only
	# on each other and libSystem. The guest names them one at a time as it loads a toolchain (measured:
	# swift-driver asked for libswift_Concurrency after libswiftCore), and every declared root must
	# resolve, so a runtime that is present but undeclared is a load-time failure waiting for the next
	# guest command. Declaring what the payload holds keeps that in step with version.txt.
	file(GLOB _rootless_swift_runtime CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/external/swift/libswift*.dylib")
	if(NOT _rootless_swift_runtime)
		message(FATAL_ERROR
			"the rootless toolchain component needs the Swift runtime, but no "
			"src/external/swift/libswift*.dylib is present: materialise the git-lfs "
			"payload first (git lfs pull, or the project's build.sh)")
	endif()
	list(SORT _rootless_swift_runtime)
	file(MAKE_DIRECTORY "${_rootless_toolchain_resource_dir}/usr/lib/swift")
	foreach(_swift_dylib IN LISTS _rootless_swift_runtime)
		get_filename_component(_swift_name "${_swift_dylib}" NAME)
		configure_file("${_swift_dylib}"
			"${_rootless_toolchain_resource_dir}/usr/lib/swift/${_swift_name}" COPYONLY)
		string(REPLACE "." "_" _swift_target "${_swift_name}")
		string(APPEND _rootless_toolchain_extra_resources
			",\n    {\"target\": \"swift_${_swift_target}\", \"guest_path\": \"/usr/lib/swift/${_swift_name}\", \"host_path\": \"${_rootless_toolchain_resource_dir}/usr/lib/swift/${_swift_name}\"}")
	endforeach()
	# The same payload also carries framework binaries, which are not /usr/lib/swift dylibs and cannot ride the
	# loop above: their guest path is the framework's own install name. Declared the same way -- discovered, not
	# listed -- because the set grows as the overlay work reaches each framework (measured: the closure resolver
	# refused the deploy with "no built provider for /System/Library/Frameworks/Combine.framework/Versions/A/Combine
	# required by /usr/lib/swift/libswiftFoundation.dylib" the moment libswiftFoundation joined the payload, and
	# Combine was built and staged but undeclared).
	file(GLOB _rootless_swift_frameworks CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/external/swift/*.framework")
	foreach(_swift_framework IN LISTS _rootless_swift_frameworks)
		get_filename_component(_swift_framework_name "${_swift_framework}" NAME_WE)
		set(_swift_framework_binary "${_swift_framework}/Versions/A/${_swift_framework_name}")
		if(NOT EXISTS "${_swift_framework_binary}")
			continue()
		endif()
		file(MAKE_DIRECTORY "${_rootless_toolchain_resource_dir}/System/Library/Frameworks/${_swift_framework_name}.framework/Versions/A")
		configure_file("${_swift_framework_binary}"
			"${_rootless_toolchain_resource_dir}/System/Library/Frameworks/${_swift_framework_name}.framework/Versions/A/${_swift_framework_name}" COPYONLY)
		string(REPLACE "." "_" _swift_framework_target "${_swift_framework_name}")
		string(APPEND _rootless_toolchain_extra_resources
			",\n    {\"target\": \"swift_framework_${_swift_framework_target}\", \"guest_path\": \"/System/Library/Frameworks/${_swift_framework_name}.framework/Versions/A/${_swift_framework_name}\", \"host_path\": \"${_rootless_toolchain_resource_dir}/System/Library/Frameworks/${_swift_framework_name}.framework/Versions/A/${_swift_framework_name}\"}")
	endforeach()
	# A third shape the two globs above cannot express: a payload binary whose install name is a PRIVATE
	# framework path. The frameworks glob maps to /System/Library/Frameworks and the runtime glob to
	# /usr/lib/swift, so a private-framework payload is discovered under its own directory and keeps its
	# own guest path (measured: Xcode 26.3's SWBUtil.framework hard-links
	# /System/Library/PrivateFrameworks/ArgumentParserInternal.framework/Versions/A/ArgumentParserInternal
	# and the whole IDEXCBuildSupportCore plug-in is refused with "Reason: image not found" while that
	# image is absent, so the file has to be guest-visible at exactly that path and not merely installed
	# under libexec/darling). Discovered rather than listed, like the globs around it: the payload grows
	# as the overlay work reaches each one.
	file(GLOB _rootless_private_frameworks CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/external/swift/private-frameworks/*.framework")
	foreach(_private_framework IN LISTS _rootless_private_frameworks)
		get_filename_component(_private_framework_name "${_private_framework}" NAME_WE)
		set(_private_framework_binary "${_private_framework}/Versions/A/${_private_framework_name}")
		if(NOT EXISTS "${_private_framework_binary}")
			continue()
		endif()
		file(MAKE_DIRECTORY "${_rootless_toolchain_resource_dir}/System/Library/PrivateFrameworks/${_private_framework_name}.framework/Versions/A")
		configure_file("${_private_framework_binary}"
			"${_rootless_toolchain_resource_dir}/System/Library/PrivateFrameworks/${_private_framework_name}.framework/Versions/A/${_private_framework_name}" COPYONLY)
		string(REPLACE "." "_" _private_framework_target "${_private_framework_name}")
		string(APPEND _rootless_toolchain_extra_resources
			",\n    {\"target\": \"private_framework_${_private_framework_target}\", \"guest_path\": \"/System/Library/PrivateFrameworks/${_private_framework_name}.framework/Versions/A/${_private_framework_name}\", \"host_path\": \"${_rootless_toolchain_resource_dir}/System/Library/PrivateFrameworks/${_private_framework_name}.framework/Versions/A/${_private_framework_name}\"}")
	endforeach()
	# Three payload artifacts whose guest path is neither of the two globbed shapes above: a PRIVATE
	# framework path (OSAnalytics, RemoteServiceDiscovery) and a plain /usr/lib dylib
	# (libCoreEntitlements). Each payload file mirrors its guest path under `substitutes/`, so one
	# recursive glob stages and declares all of them -- discovered, not listed, like the two globs
	# above. The target label is synthetic for the same reason swift_framework_<name> is: the payload
	# is staged here, not built by a target, and only host_path is read.
	file(GLOB_RECURSE _rootless_substitutes CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/external/swift/substitutes/*")
	foreach(_substitute IN LISTS _rootless_substitutes)
		if(IS_DIRECTORY "${_substitute}")
			continue()
		endif()
		file(RELATIVE_PATH _substitute_rel
			"${CMAKE_CURRENT_SOURCE_DIR}/src/external/swift/substitutes" "${_substitute}")
		get_filename_component(_substitute_dir "${_substitute_rel}" DIRECTORY)
		file(MAKE_DIRECTORY "${_rootless_toolchain_resource_dir}/${_substitute_dir}")
		configure_file("${_substitute}"
			"${_rootless_toolchain_resource_dir}/${_substitute_rel}" COPYONLY)
		string(REPLACE "/" "_" _substitute_target "substitute_${_substitute_rel}")
		string(APPEND _rootless_toolchain_extra_resources
			",\n    {\"target\": \"${_substitute_target}\", \"guest_path\": \"/${_substitute_rel}\", \"host_path\": \"${_rootless_toolchain_resource_dir}/${_substitute_rel}\"}")
	endforeach()
	# A framework this TREE BUILDS, so unlike the Swift payload above it does not exist at configure time and cannot
	# be staged with configure_file: the manifest names the build product directly with a generator expression, the
	# same way the entrypoints below name theirs. MEASURED 2026-10-11: a minimal xcodebuild project in a guest died
	# because the guest-visible tree had no /System/Library/Frameworks/UniformTypeIdentifiers.framework while the
	# install rule had already placed it under libexec/darling -- installed, but undeclared here, and this manifest is
	# the only thing that makes a component guest-visible (the same reason Combine.framework, which the skeleton does
	# not contain, is reachable in a filled prefix). Xcode3Core.framework hard-links this path at current version
	# 709.0.0, which is what src/frameworks/UniformTypeIdentifiers builds.
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"framework_UniformTypeIdentifiers\", \"guest_path\": \"/System/Library/Frameworks/UniformTypeIdentifiers.framework/Versions/A/UniformTypeIdentifiers\", \"host_path\": \"$<TARGET_FILE:UniformTypeIdentifiers>\"}")
	# Both are built by this component now, and both are install-name/path shapes a
	# build tool loads at run time: libarchive.2.dylib at /usr/lib, and the
	# framework binary at the private-framework path dyld reports.
	string(APPEND _rootless_toolchain_extra_entrypoints
		",\n    {\"target\": \"archive\", \"guest_path\": \"/usr/lib/libarchive.2.dylib\", \"host_path\": \"$<TARGET_FILE:archive>\"}")
	# Loaded by an Xcode image at load time, exactly like libarchive.2.dylib above: DVTFoundation
	# hard-links /usr/lib/libRosetta.dylib (measured; it imports one symbol from it), so it has to be
	# guest-visible and not merely installed under libexec/darling.
	string(APPEND _rootless_toolchain_extra_entrypoints
		",\n    {\"target\": \"Rosetta\", \"guest_path\": \"/usr/lib/libRosetta.dylib\", \"host_path\": \"$<TARGET_FILE:Rosetta>\"}")
	# Same shape and same measured reason as libRosetta immediately above: Xcode's iOS platform plug-in
	# hard-links /usr/lib/liboah.dylib at load time (STRONG load command; it binds zero symbols from it),
	# so the image has to be guest-visible and not merely installed under libexec/darling. See its target
	# entry above and src/liboah/src/oah.c for the per-referrer measurement.
	string(APPEND _rootless_toolchain_extra_entrypoints
		",\n    {\"target\": \"oah\", \"guest_path\": \"/usr/lib/liboah.dylib\", \"host_path\": \"$<TARGET_FILE:oah>\"}")
	string(APPEND _rootless_toolchain_extra_entrypoints
		",\n    {\"target\": \"PackageKit\", \"guest_path\": \"/System/Library/PrivateFrameworks/PackageKit.framework/Versions/A/PackageKit\", \"host_path\": \"$<TARGET_FILE:PackageKit>\"}")
	# Same shape as PackageKit immediately above and for the same measured reason: DVTFoundation
	# hard-links this private-framework path at load time.
	string(APPEND _rootless_toolchain_extra_entrypoints
		",\n    {\"target\": \"MobileAsset\", \"guest_path\": \"/System/Library/PrivateFrameworks/MobileAsset.framework/Versions/A/MobileAsset\", \"host_path\": \"$<TARGET_FILE:MobileAsset>\"}")
	# The next two load-time dependencies the same probing run named, at the exact paths DVTFoundation
	# names in its load commands.
	string(APPEND _rootless_toolchain_extra_entrypoints
		",\n    {\"target\": \"CoreData\", \"guest_path\": \"/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData\", \"host_path\": \"$<TARGET_FILE:CoreData>\"}")
	string(APPEND _rootless_toolchain_extra_entrypoints
		",\n    {\"target\": \"ImageIO\", \"guest_path\": \"/System/Library/Frameworks/ImageIO.framework/Versions/A/ImageIO\", \"host_path\": \"$<TARGET_FILE:ImageIO>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"CoreAnalytics\", \"guest_path\": \"/System/Library/PrivateFrameworks/CoreAnalytics.framework/Versions/A/CoreAnalytics\", \"host_path\": \"$<TARGET_FILE:CoreAnalytics>\"}")
	# Same shape, same measurement as CoreAnalytics above: the exact path DVTFoundation's load command
	# names.
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"AssertionServices\", \"guest_path\": \"/System/Library/PrivateFrameworks/AssertionServices.framework/Versions/A/AssertionServices\", \"host_path\": \"$<TARGET_FILE:AssertionServices>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"CoreGraphics\", \"guest_path\": \"/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics\", \"host_path\": \"$<TARGET_FILE:CoreGraphics>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"DiskArbitration\", \"guest_path\": \"/System/Library/Frameworks/DiskArbitration.framework/Versions/A/DiskArbitration\", \"host_path\": \"$<TARGET_FILE:DiskArbitration>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"DiskManagement\", \"guest_path\": \"/System/Library/PrivateFrameworks/DiskManagement.framework/Versions/A/DiskManagement\", \"host_path\": \"$<TARGET_FILE:DiskManagement>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"AppleSystemInfo\", \"guest_path\": \"/System/Library/PrivateFrameworks/AppleSystemInfo.framework/Versions/A/AppleSystemInfo\", \"host_path\": \"$<TARGET_FILE:AppleSystemInfo>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"AssetCacheServices\", \"guest_path\": \"/System/Library/PrivateFrameworks/AssetCacheServices.framework/Versions/A/AssetCacheServices\", \"host_path\": \"$<TARGET_FILE:AssetCacheServices>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"DiskImages\", \"guest_path\": \"/System/Library/PrivateFrameworks/DiskImages.framework/Versions/A/DiskImages\", \"host_path\": \"$<TARGET_FILE:DiskImages>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"CrashReporterSupport\", \"guest_path\": \"/System/Library/PrivateFrameworks/CrashReporterSupport.framework/Versions/A/CrashReporterSupport\", \"host_path\": \"$<TARGET_FILE:CrashReporterSupport>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"compression\", \"guest_path\": \"/usr/lib/libcompression.dylib\", \"host_path\": \"$<TARGET_FILE:compression>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"AppleArchive\", \"guest_path\": \"/usr/lib/libAppleArchive.dylib\", \"host_path\": \"$<TARGET_FILE:AppleArchive>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"PlugInKit\", \"guest_path\": \"/System/Library/PrivateFrameworks/PlugInKit.framework/Versions/A/PlugInKit\", \"host_path\": \"$<TARGET_FILE:PlugInKit>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"libdtrace\", \"guest_path\": \"/usr/lib/libdtrace.dylib\", \"host_path\": \"$<TARGET_FILE:libdtrace>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"NetworkStatistics\", \"guest_path\": \"/System/Library/PrivateFrameworks/NetworkStatistics.framework/Versions/A/NetworkStatistics\", \"host_path\": \"$<TARGET_FILE:NetworkStatistics>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"kperf\", \"guest_path\": \"/System/Library/PrivateFrameworks/kperf.framework/Versions/A/kperf\", \"host_path\": \"$<TARGET_FILE:kperf>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"StreamingZip\", \"guest_path\": \"/System/Library/PrivateFrameworks/StreamingZip.framework/Versions/A/StreamingZip\", \"host_path\": \"$<TARGET_FILE:StreamingZip>\"}")
	# The Metal image is SELECTED, not named: DARLING_ROOTLESS_METAL_TARGET is Metal when
	# BUILD_METAL and MetalNoGPU otherwise. See the rung notes at its target-list entry for why
	# (the placeholder must not shadow the real Metal the tree builds and links).
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"${DARLING_ROOTLESS_METAL_TARGET}\", \"guest_path\": \"/System/Library/Frameworks/Metal.framework/Versions/A/Metal\", \"host_path\": \"$<TARGET_FILE:${DARLING_ROOTLESS_METAL_TARGET}>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"LoggingSupport\", \"guest_path\": \"/System/Library/PrivateFrameworks/LoggingSupport.framework/Versions/A/LoggingSupport\", \"host_path\": \"$<TARGET_FILE:LoggingSupport>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"QuartzCore\", \"guest_path\": \"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore\", \"host_path\": \"$<TARGET_FILE:QuartzCore>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"CoreLocation\", \"guest_path\": \"/System/Library/Frameworks/CoreLocation.framework/Versions/A/CoreLocation\", \"host_path\": \"$<TARGET_FILE:CoreLocation>\"}")
	# A framework this tree BUILDS (src/frameworks/OSLog), so like framework_UniformTypeIdentifiers
	# above it is named by its build product rather than staged at configure time. MEASURED
	# 2026-10-15: SWBBuildSystem's STRONG LC_LOAD_DYLIB for this path (compatibility 1.0.0 / current
	# 1815.60.2) was the child's next stop after the lane-memfd fix, and the prefix carried no such
	# image. Installing it under libexec/darling is not enough on its own -- this manifest is the
	# only thing that makes a component guest-visible (the UniformTypeIdentifiers lesson) -- so the
	# declaration is part of the rung, not an afterthought. See the target-list entry for the whole
	# measured wave and for what this framework's store side is built on.
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"OSLog\", \"guest_path\": \"/System/Library/Frameworks/OSLog.framework/Versions/A/OSLog\", \"host_path\": \"$<TARGET_FILE:OSLog>\"}")
	# The WAVE of six DECLARATION rows measured on 2026-10-12 (see the target-list comment for the
	# per-component evidence). One resource line each is enough: the deploy's Mach-O closure resolver
	# pulls each image's reexports, as the declared CoreServices already does for FSEvents/LaunchServices.
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"ApplicationServices\", \"guest_path\": \"/System/Library/Frameworks/ApplicationServices.framework/Versions/A/ApplicationServices\", \"host_path\": \"$<TARGET_FILE:ApplicationServices>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"AuthKit\", \"guest_path\": \"/System/Library/PrivateFrameworks/AuthKit.framework/Versions/A/AuthKit\", \"host_path\": \"$<TARGET_FILE:AuthKit>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"login\", \"guest_path\": \"/System/Library/PrivateFrameworks/login.framework/Versions/A/login\", \"host_path\": \"$<TARGET_FILE:login>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"exslt\", \"guest_path\": \"/usr/lib/libexslt.0.dylib\", \"host_path\": \"$<TARGET_FILE:exslt>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"xslt\", \"guest_path\": \"/usr/lib/libxslt.1.dylib\", \"host_path\": \"$<TARGET_FILE:xslt>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"sqlite3\", \"guest_path\": \"/usr/lib/libsqlite3.dylib\", \"host_path\": \"$<TARGET_FILE:sqlite3>\"}")
	# MEASURED 2026-10-14, the rung after the platform scan -- see the measured note on this target in
	# DARLING_ROOTLESS_TOOLCHAIN_TARGETS. libobjc dlopens /usr/lib/libobjc-trampolines.dylib by
	# absolute name, so no LOAD command ever names it and the closure resolver cannot see it; without
	# this declaration the image was built and deployed NOWHERE, and both xcodebuild workloads aborted
	# at objc-block-trampolines.mm:173 with XCB_RC=134. The guest path is the image's own LC_ID, which
	# is exactly the name libobjc asks for.
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"objc-trampolines\", \"guest_path\": \"/usr/lib/libobjc-trampolines.dylib\", \"host_path\": \"$<TARGET_FILE:objc-trampolines>\"}")
	# MEASURED 2026-10-14 -- see the measured note on this target in DARLING_ROOTLESS_TOOLCHAIN_TARGETS.
	# Xcode's IDEiOSSupportCore carries a STRONG LC_LOAD_DYLIB for this exact path and binds twelve
	# strong eager names from it, so the plug-in cannot load until the image is in the prefix. The
	# guest path is the image's own LC_ID, read from the built artifact rather than transcribed, and it
	# is the CONCRETE version directory: the framework's top-level name is a symlink in the libexec
	# root (made by CMake's install rule) and absent in the guest-visible root, exactly like the other
	# twenty-odd private frameworks this component declares, so declaring the concrete file keeps the
	# two roots identical.
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"MobileDevice\", \"guest_path\": \"/System/Library/PrivateFrameworks/MobileDevice.framework/Versions/A/MobileDevice\", \"host_path\": \"$<TARGET_FILE:MobileDevice>\"}")
	# MEASURED 2026-10-14 -- see the measured note on this target in DARLING_ROOTLESS_TOOLCHAIN_TARGETS.
	# Xcode's IDESimulatorFoundation carries a STRONG LC_LOAD_DYLIB for this exact path and binds
	# eight names from it, so the plug-in cannot load until the image is in the prefix. The guest path
	# is the image's own LC_ID, read from the built artifact rather than transcribed
	# (llvm-objdump --macho --dylib-id build/verify14/src/private-frameworks/CoreSimulator/CoreSimulator
	# -> /Library/Developer/PrivateFrameworks/CoreSimulator.framework/Versions/A/CoreSimulator), and it
	# is the CONCRETE version directory, exactly as for MobileDevice above. It is also the ONLY artifact
	# in this component whose guest path is under Library/ rather than usr/ or System/: the deploy's own
	# runtime_deploy_targets() (west_commands/test_runtime.py) gives a Library/ path ONE destination,
	# the guest-visible prefix root, which is the root the guest's /Library resolves from.
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"CoreSimulator\", \"guest_path\": \"/Library/Developer/PrivateFrameworks/CoreSimulator.framework/Versions/A/CoreSimulator\", \"host_path\": \"$<TARGET_FILE:CoreSimulator>\"}")
	# The Accelerate rung (see the target-list comment for the twelve measured demands). One resource
	# line per image of the re-export chain, each guest_path being that artifact's own LC_ID install
	# name, and the vecLib sub-dylibs living under Accelerate.framework/Versions/A/Frameworks.
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"Accelerate\", \"guest_path\": \"/System/Library/Frameworks/Accelerate.framework/Versions/A/Accelerate\", \"host_path\": \"$<TARGET_FILE:Accelerate>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"vecLib\", \"guest_path\": \"/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/vecLib\", \"host_path\": \"$<TARGET_FILE:vecLib>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"vMisc\", \"guest_path\": \"/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libvMisc.dylib\", \"host_path\": \"$<TARGET_FILE:vMisc>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"vDSP\", \"guest_path\": \"/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libvDSP.dylib\", \"host_path\": \"$<TARGET_FILE:vDSP>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"BLAS\", \"guest_path\": \"/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libBLAS.dylib\", \"host_path\": \"$<TARGET_FILE:BLAS>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"LAPACK\", \"guest_path\": \"/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libLAPACK.dylib\", \"host_path\": \"$<TARGET_FILE:LAPACK>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"LinearAlgebra\", \"guest_path\": \"/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libLinearAlgebra.dylib\", \"host_path\": \"$<TARGET_FILE:LinearAlgebra>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"SparseBLAS\", \"guest_path\": \"/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libSparseBLAS.dylib\", \"host_path\": \"$<TARGET_FILE:SparseBLAS>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"Quadrature\", \"guest_path\": \"/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libQuadrature.dylib\", \"host_path\": \"$<TARGET_FILE:Quadrature>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"BNNS\", \"guest_path\": \"/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libBNNS.dylib\", \"host_path\": \"$<TARGET_FILE:BNNS>\"}")
	string(APPEND _rootless_toolchain_extra_resources
		",\n    {\"target\": \"Sparse\", \"guest_path\": \"/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libSparse.dylib\", \"host_path\": \"$<TARGET_FILE:Sparse>\"}")
	if(DARLING_ROOTLESS_HOMEBREW)
		include(cmake/RootlessHomebrew.cmake)
	endif()
	file(GENERATE
		OUTPUT "${_rootless_toolchain_manifest}"
		CONTENT
"{
  \"schema\": 1,
  \"entrypoints\": [
    {\"target\": \"installer\", \"guest_path\": \"/usr/bin/installer\", \"host_path\": \"$<TARGET_FILE:installer>\"},
    {\"target\": \"perl\", \"guest_path\": \"/usr/bin/perl\", \"host_path\": \"$<TARGET_FILE:perl>\"},
    {\"target\": \"perl5.18\", \"guest_path\": \"/usr/bin/perl5.18\", \"host_path\": \"$<TARGET_FILE:perl5.18>\"},
    {\"target\": \"perl5.28\", \"guest_path\": \"/usr/bin/perl5.28\", \"host_path\": \"$<TARGET_FILE:perl5.28>\"},
    {\"target\": \"security\", \"guest_path\": \"/usr/bin/security\", \"host_path\": \"$<TARGET_FILE:securitytool_macos>\"}${_rootless_toolchain_extra_entrypoints}
  ],
  \"resources\": [
    {\"target\": \"installer_man\", \"guest_path\": \"/usr/share/man/man8/installer.8\", \"host_path\": \"${_rootless_toolchain_resource_dir}/usr/share/man/man8/installer.8\"},
    {\"target\": \"perl_c2ph\", \"guest_path\": \"/usr/bin/c2ph\", \"host_path\": \"${_rootless_toolchain_resource_dir}/usr/bin/c2ph\"}${_rootless_toolchain_extra_resources}
  ]
}
"
	)
endif()

install(DIRECTORY DESTINATION libexec/darling/private)
install(DIRECTORY DESTINATION libexec/darling/private/etc)
install(DIRECTORY DESTINATION libexec/darling/private/var)
install(DIRECTORY DESTINATION libexec/darling/private/tmp)
InstallSymlink(private/etc ${CMAKE_INSTALL_PREFIX}/libexec/darling/etc)
InstallSymlink(private/var ${CMAKE_INSTALL_PREFIX}/libexec/darling/var)

install(FILES etc/resolv.conf etc/hosts
	DESTINATION libexec/darling/private/etc)
InstallSymlink(/Volumes/SystemRoot/etc/machine-id ${CMAKE_INSTALL_PREFIX}/libexec/darling/private/etc/machine-id)
InstallSymlink(/Volumes/SystemRoot/etc/nsswitch.conf ${CMAKE_INSTALL_PREFIX}/libexec/darling/private/etc/nsswitch.conf)

install(DIRECTORY DESTINATION libexec/darling/Volumes)
install(DIRECTORY DESTINATION libexec/darling/Volumes/SystemRoot)
InstallSymlink(/ ${CMAKE_INSTALL_PREFIX}/libexec/darling/Volumes/DarlingEmulatedDrive)

install(DIRECTORY DESTINATION libexec/darling/proc)

install(DIRECTORY DESTINATION libexec/darling/var/root)
install(DIRECTORY DESTINATION libexec/darling/var/run)
InstallSymlink(/dev/log ${CMAKE_INSTALL_PREFIX}/libexec/darling/var/run/syslog)

install(DIRECTORY DESTINATION libexec/darling/usr)
install(DIRECTORY DESTINATION libexec/darling/usr/local)
install(DIRECTORY DESTINATION libexec/darling/usr/local/share)

InstallSymlink(/Volumes/SystemRoot/dev ${CMAKE_INSTALL_PREFIX}/libexec/darling/dev)
InstallSymlink(private/tmp ${CMAKE_INSTALL_PREFIX}/libexec/darling/tmp)

InstallSymlink(/proc/self/mounts ${CMAKE_INSTALL_PREFIX}/libexec/darling/private/etc/mtab)
#InstallSymlink(/Volumes/SystemRoot/etc/passwd ${CMAKE_INSTALL_PREFIX}/libexec/darling/private/etc/passwd)
#InstallSymlink(/Volumes/SystemRoot/etc/group ${CMAKE_INSTALL_PREFIX}/libexec/darling/private/etc/group)
InstallSymlink(/Volumes/SystemRoot/etc/localtime ${CMAKE_INSTALL_PREFIX}/libexec/darling/private/etc/localtime)

InstallSymlink(/Volumes/SystemRoot/usr/share/zoneinfo ${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/share/zoneinfo)

install(DIRECTORY DESTINATION libexec/darling/Library/Preferences)

if(NOT DEBIAN_PACKAGING)
	install(CODE "execute_process(COMMAND bash ${DARLING_TOP_DIRECTORY}/tools/shutdown-user.sh)")
endif(NOT DEBIAN_PACKAGING)

add_custom_target(uninstall
	COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/uninstall
	COMMENT "Uninstall Darling and kernel module")
