#ifndef DARLING_SM_JOB_PRIVATE_H
#define DARLING_SM_JOB_PRIVATE_H

#include <sys/cdefs.h>
#include <ServiceManagement/ServiceManagement.h>

__BEGIN_DECLS

/*
 * User-domain clients only: launchd selects and authorizes the caller-UID domain.
 * Inputs are borrowed and are never changed. On success, a supplied error slot
 * is cleared; on failure it receives an owned CFError (subject to CF allocation
 * failure). These functions do not consume any previously stored error.
 */
Boolean _SMJobSubmitUser(CFDictionaryRef job, CFErrorRef *error);
Boolean _SMJobRemoveUser(CFStringRef label, Boolean wait, CFErrorRef *error);

/* System-domain clients communicate with the authenticated privileged broker. */
Boolean _SMJobSubmitSystem(CFDictionaryRef job, AuthorizationRef auth, CFErrorRef *error);
Boolean _SMJobRemoveSystem(CFStringRef label, AuthorizationRef auth, Boolean wait,
    CFErrorRef *error);

__END_DECLS

#endif
