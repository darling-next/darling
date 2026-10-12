/*
 This file is part of Darling.

 Copyright (C) 2019 Lubos Dolezel

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


#ifndef _MobileGestalt_H_
#define _MobileGestalt_H_

#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>

// I don't know what the actual keys values are...
// (declared here, defined once in src/MobileGestalt.m: this header is included by more than one
// translation unit now, and a definition in a header is a duplicate symbol at link time)
extern CFStringRef kMGQReleaseType;

/* The MODERN device SPI (the macOS 26 / iOS 18 shape).  The ABI is the SDK's own
 * /usr/lib/libMobileGestalt.tbd, which names these eight; the SDK ships no header for them, so the
 * shapes below are the ones the measured clients compile against, and the `_obj` suffix means the
 * answer is an Objective-C string (CFStringRef here to stay C-compatible; toll-free bridged), not
 * a CF-owning `copy`.  Ownership is +0/autoreleased -- the one client (Xcode 26.3's XCTHarness)
 * balances it with _objc_retainAutoreleasedReturnValue + release, measured from its own code.  See
 * src/MobileGestaltDevice.m for the call sites and for which answers are real facts and which are
 * honest absences. */
void* MobileGestalt_get_current_device(void);
int32_t MobileGestalt_get_cpuType(void* device);
int32_t MobileGestalt_get_cpuSubType(void* device);
CFStringRef MobileGestalt_copy_humanReadableProductVersionString_obj(void* device);
CFStringRef MobileGestalt_copy_productType_obj(void* device);
CFStringRef MobileGestalt_copy_supplementalBuildVersion_obj(void* device);
CFStringRef MobileGestalt_copy_uniqueDeviceID_obj(void* device);
CFStringRef MobileGestalt_copy_userAssignedDeviceName_obj(void* device);

void* MGCancelNotifications(void);
CFStringRef MGCopyAnswer(CFStringRef key, void* unknown);
void* MGCopyAnswerWithError(void);
void* MGCopyMultipleAnswers(void);
void* MGGetBoolAnswer(void);
void* MGGetFloat32Answer(void);
void* MGGetProductType(void);
void* MGGetSInt32Answer(void);
void* MGGetSInt64Answer(void);
void* MGGetStringAnswer(void);
void* MGIsDeviceOfType(void);
void* MGIsDeviceOneOfType(void);
void* MGIsQuestionValid(void);
void* MGRegisterForBulkUpdates(void);
void* MGRegisterForUpdates(void);
void* MGSetAnswer(void);
void* MGSetLogHandler(void);
void* _MGCacheCopyValueForKey(void);
void* _MGCacheValid(void);
void* _MGClearInProcessCache(void);
void* _MGCopyDeviceDescription(void);
void* _MGCopyIteratedDeviceDescription(void);
void* _MGGetFastPathLog(void);
void* _MGIterateDevices(void);
void* _MGLog(void);
void* _MGPrintIteratedDeviceDescription(void);
void* _MGRebuildCache(void);
void* _MGServerCopyAnswerWithError(void);
void* _MGServerQuestionIsPlatform(void);
void* _MGSetServer(void);
void* _MGWaitForDevices(void);
void* _MGWriteCache(void);

#endif
