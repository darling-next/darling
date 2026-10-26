/*
 This file is part of Darling.

 Copyright (C) 2026 Darling contributors

 Darling is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Darling is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Darling.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <DeviceIdentity/DeviceIdentity.h>
#include <CoreFoundation/CFString.h>
#include <stdlib.h>

/*
 * Device identity is a Secure Enclave capability.  A Linux guest has none, so
 * the capability probe answers false and the certificate operation refuses.
 * CoreSimulator still binds the option keys unconditionally at load; they are
 * real CFStrings with Apple's exported key names.
 */
bool DeviceIdentityIsSupported(void)
{
    return false;
}

void DeviceIdentityIssueClientCertificateWithCompletion(void *options, void *queue, void *completion)
{
    (void) options;
    (void) queue;
    (void) completion;
    abort();
}

CFStringRef kMAOptionsBAAAccessControls;
CFStringRef kMAOptionsBAADeleteExistingKeysAndCerts;
CFStringRef kMAOptionsBAAIgnoreExistingKeychainItems;
CFStringRef kMAOptionsBAAKeychainAccessGroup;
CFStringRef kMAOptionsBAAKeychainLabel;
CFStringRef kMAOptionsBAANetworkTimeoutInterval;
CFStringRef kMAOptionsBAAOIDAccessControls;
CFStringRef kMAOptionsBAAOIDDeviceOSInformation;
CFStringRef kMAOptionsBAAOIDHardwareProperties;
CFStringRef kMAOptionsBAAOIDKeyUsageProperties;
CFStringRef kMAOptionsBAAOIDSToInclude;
CFStringRef kMAOptionsBAASCRTAttestation;
CFStringRef kMAOptionsBAAValidity;
CFStringRef kMAOptionsResuseExistingKey;

static CFStringRef
DarlingDeviceIdentityOption(const char *name)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, name, kCFStringEncodingUTF8);
}

__attribute__((constructor))
static void
DarlingDeviceIdentityInitializeOptionKeys(void)
{
    kMAOptionsBAAAccessControls = DarlingDeviceIdentityOption("kMAOptionsBAAAccessControls");
    kMAOptionsBAADeleteExistingKeysAndCerts = DarlingDeviceIdentityOption("kMAOptionsBAADeleteExistingKeysAndCerts");
    kMAOptionsBAAIgnoreExistingKeychainItems = DarlingDeviceIdentityOption("kMAOptionsBAAIgnoreExistingKeychainItems");
    kMAOptionsBAAKeychainAccessGroup = DarlingDeviceIdentityOption("kMAOptionsBAAKeychainAccessGroup");
    kMAOptionsBAAKeychainLabel = DarlingDeviceIdentityOption("kMAOptionsBAAKeychainLabel");
    kMAOptionsBAANetworkTimeoutInterval = DarlingDeviceIdentityOption("kMAOptionsBAANetworkTimeoutInterval");
    kMAOptionsBAAOIDAccessControls = DarlingDeviceIdentityOption("kMAOptionsBAAOIDAccessControls");
    kMAOptionsBAAOIDDeviceOSInformation = DarlingDeviceIdentityOption("kMAOptionsBAAOIDDeviceOSInformation");
    kMAOptionsBAAOIDHardwareProperties = DarlingDeviceIdentityOption("kMAOptionsBAAOIDHardwareProperties");
    kMAOptionsBAAOIDKeyUsageProperties = DarlingDeviceIdentityOption("kMAOptionsBAAOIDKeyUsageProperties");
    kMAOptionsBAAOIDSToInclude = DarlingDeviceIdentityOption("kMAOptionsBAAOIDSToInclude");
    kMAOptionsBAASCRTAttestation = DarlingDeviceIdentityOption("kMAOptionsBAASCRTAttestation");
    kMAOptionsBAAValidity = DarlingDeviceIdentityOption("kMAOptionsBAAValidity");
    kMAOptionsResuseExistingKey = DarlingDeviceIdentityOption("kMAOptionsResuseExistingKey");
}
