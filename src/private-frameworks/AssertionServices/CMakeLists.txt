project(AssertionServices)

remove_sdk_framework(AssertionServices
    PRIVATE
)

set(DYLIB_COMPAT_VERSION "1.0.0")
# MEASURED 2026-10-12: this is the ABI level OUR implementation offers to clients, and it answers the
# load command in Xcode 26.3's DVTFoundation, which demands "current version 1008.40.4" (compatibility
# version 1.0.0) and makes dyld refuse any lower one before it looks at symbols. The implementation
# behind it is not Apple's. It said 223.140.2 here, which is why declaring the framework in the
# rootless toolchain component was not enough on its own.
set(DYLIB_CURRENT_VERSION "1008.40.4")

set(FRAMEWORK_VERSION "A")

generate_sdk_framework(AssertionServices
    VERSION ${FRAMEWORK_VERSION}
    HEADER "include/AssertionServices"
    PRIVATE
)

add_framework(AssertionServices
    FAT
    CURRENT_VERSION
    PRIVATE
    VERSION ${FRAMEWORK_VERSION}

    SOURCES
        src/AssertionServices.m
        src/BKSWorkspace.m
        src/BKSAssertion.m
        src/BKSProcessExitContext.m
        src/BKSProcess.m
        src/BKSTerminationContext.m
        src/BKSProcessAssertion.m
        src/BKSTerminationAssertionObserverManager.m
        src/BKSTerminationAssertion.m
        src/BKSApplicationStateMonitor.m
        src/BKSLaunchdJobSpecification.m

    DEPENDENCIES
        system
        objc
        Foundation
)
