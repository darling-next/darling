project(MetalNoGPU)

set(DYLIB_INSTALL_NAME "/System/Library/Frameworks/Metal.framework/Versions/A/Metal")
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "370.63.7")

# A TRUE-ANSWER implementation, not a placeholder: this guest has no GPU, and Metal's four
# enumerated functions report exactly that. See src/Metal.c for the measurement that decided it
# (the client calls all four, tolerates an empty answer, and its call sites pass the result into
# Objective-C messages where nil is a no-op).
#
# The name is MetalNoGPU rather than Metal because the tree already defines a `Metal` target --
# src/external/metal, the real Vulkan-backed implementation, added under COMPONENT_dev_gui_common
# when BUILD_METAL is on. Two images cannot claim the same install name, so this one is built only
# when that real one is not, as selected by DARLING_METAL_TARGET.
add_framework(Metal
	VERSION "A"
	CURRENT_VERSION
	TARGET_NAME MetalNoGPU
	FAT
	SOURCES src/Metal.c
)
