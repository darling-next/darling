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
	# three Xcode images reference it (SharedFrameworks/DVTFoundation,
	# SharedFrameworks/AssetRuntime/DVTFoundation, SharedFrameworks/CoreSymbolicationDT).
	Rosetta
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
	# image for the runtime to fix that reference up. The tree implements it (DMManager.h and the
	# DiskManagement sources). Version: the load command demands current 1.0.0 against compatibility
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
	# Six of the seven existed only as LOUD STUBS (functions.c prints "STUB: ..." and returns NULL);
	# they are pre-existing tree state, left untouched, and filed as their own task -- this rung does
	# not fix their answers. The seventh did not exist at all, which is why a declaration alone would
	# NOT have advanced the rung: dyld resolves all seven eagerly, so "image not found" would simply
	# have become "Symbol not found: _ASI_GetCurrentCPUDescriptionWithFormat". It is implemented for
	# real in src/private-frameworks/AppleSystemInfo/src/CurrentCPUDescription.c, from the only CPU
	# data this guest actually answers (hw.machine, hw.ncpu; machdep.cpu.brand_string and
	# hw.cpufrequency are unknown oids here), with the client's call site as the contract.
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
