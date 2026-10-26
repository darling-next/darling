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

#ifndef _DEVICE_IDENTITY_H_
#define _DEVICE_IDENTITY_H_

#include <stdbool.h>
#include <CoreFoundation/CFString.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The guest has no Secure Enclave or device identity service. */
bool DeviceIdentityIsSupported(void);

/* Reaching this operation is an unsupported capability, not a certificate. */
void DeviceIdentityIssueClientCertificateWithCompletion(void *options, void *queue, void *completion);

/*
 * Option keys consumed by DeviceIdentityIssueClientCertificateWithCompletion.
 * Apple exports them from this framework; clients such as CoreSimulator bind
 * them at load time even when DeviceIdentityIsSupported() is false.
 */
extern CFStringRef kMAOptionsBAAAccessControls;
extern CFStringRef kMAOptionsBAADeleteExistingKeysAndCerts;
extern CFStringRef kMAOptionsBAAIgnoreExistingKeychainItems;
extern CFStringRef kMAOptionsBAAKeychainAccessGroup;
extern CFStringRef kMAOptionsBAAKeychainLabel;
extern CFStringRef kMAOptionsBAANetworkTimeoutInterval;
extern CFStringRef kMAOptionsBAAOIDAccessControls;
extern CFStringRef kMAOptionsBAAOIDDeviceOSInformation;
extern CFStringRef kMAOptionsBAAOIDHardwareProperties;
extern CFStringRef kMAOptionsBAAOIDKeyUsageProperties;
extern CFStringRef kMAOptionsBAAOIDSToInclude;
extern CFStringRef kMAOptionsBAASCRTAttestation;
extern CFStringRef kMAOptionsBAAValidity;
extern CFStringRef kMAOptionsResuseExistingKey;

#ifdef __cplusplus
}
#endif

#endif
