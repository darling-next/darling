#include <ServiceManagement/ServiceManagement.h>
#include "SMJobPrivate.h"
#include <errno.h>

#define STUB() (printf("STUB CALLED: %s:%i\n\t\t%s\n", __FILE__, __LINE__, __PRETTY_FUNCTION__))

const CFStringRef kSMDomainSystemLaunchd = CFSTR("kSMDomainSystemLaunchd");

const CFStringRef kSMDomainUserLaunchd = CFSTR("kSMDomainUserLaunchd");

const CFStringRef kSMErrorDomainIPC = CFSTR("kSMErrorDomainIPC");

const CFStringRef kSMErrorDomainFramework = CFSTR("kSMErrorDomainFramework");

const CFStringRef kSMErrorDomainLaunchd = CFSTR("kSMErrorDomainLaunchd");

CFDictionaryRef SMJobCopyDictionary(CFStringRef domain, CFStringRef jobLabel) {
  STUB();
  return NULL;
}

CFArrayRef SMCopyAllJobDictionaries(CFStringRef domain) {
  STUB();
  return NULL;
}

Boolean SMJobSubmit(CFStringRef domain, CFDictionaryRef job, AuthorizationRef auth, CFErrorRef *outError) {
  if (outError)
    *outError = NULL;
  if (domain && CFGetTypeID(domain) == CFStringGetTypeID()) {
    if (CFEqual(domain, kSMDomainSystemLaunchd))
      return _SMJobSubmitSystem(job, auth, outError);
    if (CFEqual(domain, kSMDomainUserLaunchd))
      return _SMJobSubmitUser(job, outError);
  }
  if (outError)
    *outError = CFErrorCreate(kCFAllocatorDefault, kCFErrorDomainPOSIX, EINVAL, NULL);
  return false;
}

Boolean SMJobRemove(CFStringRef domain, CFStringRef jobLabel, AuthorizationRef auth, Boolean wait, CFErrorRef *outError) {
  if (outError)
    *outError = NULL;
  if (domain && CFGetTypeID(domain) == CFStringGetTypeID()) {
    if (CFEqual(domain, kSMDomainSystemLaunchd))
      return _SMJobRemoveSystem(jobLabel, auth, wait, outError);
    if (CFEqual(domain, kSMDomainUserLaunchd))
      return _SMJobRemoveUser(jobLabel, wait, outError);
  }
  if (outError)
    *outError = CFErrorCreate(kCFAllocatorDefault, kCFErrorDomainPOSIX, EINVAL, NULL);
  return false;
}

#if !(TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR)

Boolean SMJobBless(CFStringRef domain, CFStringRef executableLabel, AuthorizationRef auth, CFErrorRef *outError) {
  STUB();
  return 1;
}

Boolean SMLoginItemSetEnabled(CFStringRef identifier, Boolean enabled) {
  STUB();
  return 1;
}

#endif
