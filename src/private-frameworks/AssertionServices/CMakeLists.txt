project(AssertionServices)

remove_sdk_framework(AssertionServices
    PRIVATE
)

set(DYLIB_COMPAT_VERSION "1.0.0")
# MEASURED 2026-10-12, and this value was raised once and then put back deliberately.
#
# It was raised to 1008.40.4, the current version Xcode 26.3's DVTFoundation load command demands,
# under the belief that dyld enforces that value. It does not. The proof is on this same ladder:
# DVTFoundation demands current 1522.1.0 of CoreData, the deployed CoreData declares current 752.0.0,
# and the client loaded CoreData and moved on -- dyld measured refusing nothing. The enforced value
# is the COMPATIBILITY version, and this framework's compatibility already matches the client's
# (1.0.0), which is why the rung advanced on its declaration alone.
#
# So the value goes back to the tree's own 223.140.2: a current-version claim that is not enforced,
# and that describes an ABI level this build does not implement, is worse than no claim at all,
# because the next reader cannot tell it was written under a mistaken belief.
set(DYLIB_CURRENT_VERSION "223.140.2")

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
