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
# src/external/metal, the real Vulkan-backed implementation, gated behind COMPONENT_dev_gui_common
# and not built in this configuration. If that component is ever enabled, this one must go: two
# images cannot claim the same install name.
add_darling_library(MetalNoGPU SHARED
	src/Metal.c
)

make_fat(MetalNoGPU)

install(TARGETS MetalNoGPU DESTINATION libexec/darling/System/Library/Frameworks/Metal.framework/Versions/A)
