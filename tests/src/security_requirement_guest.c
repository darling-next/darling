#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <stdio.h>

static CFDataRef parse(CFStringRef text) {
    SecRequirementRef requirement = NULL;
    OSStatus status = SecRequirementCreateWithString(text, kSecCSDefaultFlags, &requirement);
    if (status || !requirement) {
        fprintf(stderr, "FAIL requirement parse: status=%d\n", (int)status);
        if (requirement) CFRelease(requirement);
        return NULL;
    }
    CFDataRef data = NULL;
    status = SecRequirementCopyData(requirement, kSecCSDefaultFlags, &data);
    CFRelease(requirement);
    if (status) {
        fprintf(stderr, "FAIL requirement serialization: status=%d\n", (int)status);
        if (data) CFRelease(data);
        return NULL;
    }
    return data;
}

int main(void) {
    CFStringRef expression = CFSTR("anchor apple and identifier \"com.apple.codesign_allocate\"");
    CFDataRef before = parse(expression);
    if (!before) return 1;

    SecRequirementRef invalid = NULL;
    OSStatus status = SecRequirementCreateWithString(CFSTR("anchor"), kSecCSDefaultFlags, &invalid);
    if (status != errSecCSReqInvalid || invalid) {
        fprintf(stderr, "FAIL incomplete requirement: status=%d object=%d\n", (int)status, invalid != NULL);
        if (invalid) CFRelease(invalid);
        CFRelease(before);
        return 2;
    }

    CFDataRef after = parse(expression);
    int recovered = after && CFEqual(before, after);
    if (after) CFRelease(after);
    CFRelease(before);
    if (!recovered) {
        fputs("FAIL syntax error changed subsequent requirement bytecode\n", stderr);
        return 3;
    }
    puts("SECURITY_REQUIREMENT_GUEST_OK");
    return 0;
}
