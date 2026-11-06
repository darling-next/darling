#include <DiskArbitration/DADissenter.h>
#include <CoreFoundation/CFRuntime.h>

struct __DADissenter {
    CFRuntimeBase base;
    DAReturn status;
    CFStringRef string;
};

static CFTypeID dissenterType;

static void DADissenterFinalize(CFTypeRef object)
{
    DADissenterRef dissenter = (DADissenterRef)object;
    if (dissenter->string) CFRelease(dissenter->string);
}

static const CFRuntimeClass dissenterClass = {
    0, "DADissenter", NULL, NULL, DADissenterFinalize, NULL, NULL, NULL, NULL
};

__attribute__((constructor)) static void DADissenterInitialize(void)
{
    dissenterType = _CFRuntimeRegisterClass(&dissenterClass);
}

CFTypeID DADissenterGetTypeID(void) { return dissenterType; }

DADissenterRef DADissenterCreate(CFAllocatorRef allocator, DAReturn status, CFStringRef string)
{
    struct __DADissenter *dissenter = (struct __DADissenter *)_CFRuntimeCreateInstance(
        allocator, dissenterType, sizeof(*dissenter) - sizeof(CFRuntimeBase), NULL);
    if (!dissenter) return NULL;
    dissenter->status = status;
    dissenter->string = string ? CFStringCreateCopy(allocator, string) : NULL;
    if (string && !dissenter->string) { CFRelease(dissenter); return NULL; }
    return dissenter;
}

DAReturn DADissenterGetStatus(DADissenterRef dissenter) { return dissenter->status; }
CFStringRef DADissenterGetStatusString(DADissenterRef dissenter) { return dissenter->string; }
