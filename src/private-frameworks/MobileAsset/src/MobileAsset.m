/*
 This file is part of Darling.

 Copyright (C) 2020 Lubos Dolezel

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


#include <MobileAsset/MobileAsset.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

/* MobileAsset's asset-dictionary keys, not self-named symbol placeholders. */
NSString * const ASAttributeDownloadSize = @"_DownloadSize";
NSString * const ASAttributeUnarchivedSize = @"_UnarchivedSize";

static int verbose = 0;

__attribute__((constructor))
static void initme(void) {
    verbose = getenv("STUB_VERBOSE") != NULL;
}

void* ASEnsureDataVault(void)
{
    if (verbose) puts("STUB: ASEnsureDataVault called");
    return NULL;
}

void* ASNonUserInitiatedDownloadsAllowed(void)
{
    if (verbose) puts("STUB: ASNonUserInitiatedDownloadsAllowed called");
    return NULL;
}

void* ASServerURLForAssetType(void)
{
    if (verbose) puts("STUB: ASServerURLForAssetType called");
    return NULL;
}

void* ASSetAssetServerURL(void)
{
    if (verbose) puts("STUB: ASSetAssetServerURL called");
    return NULL;
}

void* ASSetAssetServerURLForAssetType(void)
{
    if (verbose) puts("STUB: ASSetAssetServerURLForAssetType called");
    return NULL;
}

void* ASSetDefaultAssetServerURLForAssetType(void)
{
    if (verbose) puts("STUB: ASSetDefaultAssetServerURLForAssetType called");
    return NULL;
}

void* ASSetLogLevel(void)
{
    if (verbose) puts("STUB: ASSetLogLevel called");
    return NULL;
}

void* MACleanV1Repository(void)
{
    if (verbose) puts("STUB: MACleanV1Repository called");
    return NULL;
}

/* --------------------------------------------------------------------------------------------
 * THE PALLAS CONFIGURATION REGISTER, AND WHY IT IS NOT A STUB.
 *
 * MEASURED NEED, 2026-10-12. Xcode 26.3's SharedFrameworks/DVTFoundation.framework links this
 * framework strongly and binds five Pallas symbols through __DATA_CONST __got, so all five must be
 * exported for the client to load. Four of them were absent from this file entirely --
 * MAGetPallasEnabled, MASetPallasEnabled, MASetPallasAudienceForType, MASetPallasUrlForType -- and
 * the fifth, MAGetPallasAudience, was a printed "STUB: MAGetPallasAudience called" that returned
 * NULL. The NULL is not survivable for the client that demands it: DVTFoundation bridges the result
 * with Swift's unconditionallyBridgeFromObjectiveC, so a NULL audience traps.
 *
 * THE ABIs BELOW COME FROM THE CALL SITES, not from a guess (disassembly of DVTFoundation's
 * DVTDirectMobileAssetDownloader.PallasConfiguration):
 *   MAGetPallasEnabled(assetType, bool *out)        the caller zeroes a byte, passes its address in
 *                                                   %rsi and reads it back; the return register is
 *                                                   not consulted, so the answer is the out byte.
 *   MASetPallasEnabled(assetType, bool enabled)     NSString in %rdi, movzbl of the flag in %esi.
 *   MASetPallasAudienceForType(audience, assetType) value first, asset type second; the audience is
 *   MASetPallasUrlForType(url, assetType)           an NSString, the URL an NSURL, the asset type an
 *                                                   NSString, each bridged just before the call.
 *   MAGetPallasAudience(assetType) -> NSString *    one NSString argument; the autoreleased result
 *                                                   is retained by the caller.
 * Pallas is Apple's asset-delivery backend, so these are the toggles a client sets on its downloader
 * configuration: enabled, audience and URL.
 *
 * WHAT IS HONEST HERE. This is a REGISTER with no asset service behind it. It stores what a caller
 * set, keyed by asset type, and gives it back unchanged: set-then-get returns what was set, and an
 * asset type nobody configured reads false for the flag and an empty string for the audience. What
 * does not exist is the service the register would be a front for: nothing is transmitted, no
 * download is affected, and there is no other MobileAsset state to consult -- which is why there is
 * nothing else to do with a value that was never set. This is the same reading already accepted for
 * QuartzCore's CARenderServer debug-option register (commit 83085aca): honouring the caller's write
 * is strictly more honest than dropping it, and the unset default is an honest empty rather than a
 * made-up value.
 *
 * THE REGISTER IS A PROCESS-LOCAL DICTIONARY, guarded by @synchronized on one class object so that
 * the lazy creation of the register itself is race-free without a constructor.
 */

static NSMutableDictionary *pallasConfigurations = nil;   /* asset type -> mutable configuration */

static NSMutableDictionary *pallasConfigurationForType(NSString *assetType, BOOL create)
{
    /* Callers hold the register lock. */
    if (assetType == nil)
        return nil;

    if (pallasConfigurations == nil)
        pallasConfigurations = [NSMutableDictionary new];

    NSMutableDictionary *configuration = [pallasConfigurations objectForKey: assetType];
    if (configuration == nil && create) {
        configuration = [NSMutableDictionary new];
        NSString *key = [assetType copy];
        [pallasConfigurations setObject: configuration forKey: key];
        [key release];
        [configuration release];
    }
    return configuration;
}

void MAGetPallasEnabled(NSString *assetType, bool *enabled)
{
    if (enabled == NULL)
        return;

    @synchronized ([MAAsset class]) {
        NSMutableDictionary *configuration = pallasConfigurationForType(assetType, NO);
        *enabled = [[configuration objectForKey: @"enabled"] boolValue];
    }
}

void MASetPallasEnabled(NSString *assetType, bool enabled)
{
    @synchronized ([MAAsset class]) {
        NSMutableDictionary *configuration = pallasConfigurationForType(assetType, YES);
        if (configuration != nil)
            [configuration setObject: [NSNumber numberWithBool: enabled] forKey: @"enabled"];
    }
}

NSString *MAGetPallasAudience(NSString *assetType)
{
    @synchronized ([MAAsset class]) {
        NSMutableDictionary *configuration = pallasConfigurationForType(assetType, NO);
        NSString *audience = [configuration objectForKey: @"audience"];
        /* No audience configured for this asset type is an empty audience, not a NULL one: the
         * client that demands this symbol traps on NULL. */
        return audience != nil ? audience : @"";
    }
}

void MASetPallasAudienceForType(NSString *audience, NSString *assetType)
{
    @synchronized ([MAAsset class]) {
        NSMutableDictionary *configuration = pallasConfigurationForType(assetType, YES);
        if (configuration == nil)
            return;

        if (audience != nil) {
            NSString *stored = [audience copy];
            [configuration setObject: stored forKey: @"audience"];
            [stored release];
        } else {
            [configuration removeObjectForKey: @"audience"];
        }
    }
}

NSURL *MAGetPallasUrlForType(NSString *assetType)
{
    @synchronized ([MAAsset class]) {
        NSMutableDictionary *configuration = pallasConfigurationForType(assetType, NO);
        return [[[configuration objectForKey: @"url"] retain] autorelease];
    }
}

void MASetPallasUrlForType(NSURL *url, NSString *assetType)
{
    @synchronized ([MAAsset class]) {
        NSMutableDictionary *configuration = pallasConfigurationForType(assetType, YES);
        if (configuration == nil)
            return;

        if (url != nil) {
            NSURL *stored = [url copy];
            [configuration setObject: stored forKey: @"url"];
            [stored release];
        } else {
            [configuration removeObjectForKey: @"url"];
        }
    }
}

void* MAMigrateAssets(void)
{
    if (verbose) puts("STUB: MAMigrateAssets called");
    return NULL;
}

void* MAPurgeAll(void)
{
    if (verbose) puts("STUB: MAPurgeAll called");
    return NULL;
}

void* MASendRepairState(void)
{
    if (verbose) puts("STUB: MASendRepairState called");
    return NULL;
}

void* MASetPallasAudience(void)
{
    if (verbose) puts("STUB: MASetPallasAudience called");
    return NULL;
}

void* MASetPallasUrl(void)
{
    if (verbose) puts("STUB: MASetPallasUrl called");
    return NULL;
}

void* MASetServerUrlOverride(void)
{
    if (verbose) puts("STUB: MASetServerUrlOverride called");
    return NULL;
}

void* MobileAssetQueryCreate(void)
{
    if (verbose) puts("STUB: MobileAssetQueryCreate called");
    return NULL;
}

void* MobileAssetQueryCreateArrayOfKnownAssets(void)
{
    if (verbose) puts("STUB: MobileAssetQueryCreateArrayOfKnownAssets called");
    return NULL;
}

void* MobileAssetQueryGetMatchingAsset(void)
{
    if (verbose) puts("STUB: MobileAssetQueryGetMatchingAsset called");
    return NULL;
}

void* MobileAssetQueryGetTypeID(void)
{
    if (verbose) puts("STUB: MobileAssetQueryGetTypeID called");
    return NULL;
}

void* MobileAssetQueryRefreshAssetsAgainstLocalCache(void)
{
    if (verbose) puts("STUB: MobileAssetQueryRefreshAssetsAgainstLocalCache called");
    return NULL;
}

void* MobileAssetQueryRefreshKnownAssets(void)
{
    if (verbose) puts("STUB: MobileAssetQueryRefreshKnownAssets called");
    return NULL;
}

void* _ForceMetadataUpdate(void)
{
    if (verbose) puts("STUB: _ForceMetadataUpdate called");
    return NULL;
}

void* _MACleanV1Repository(void)
{
    if (verbose) puts("STUB: _MACleanV1Repository called");
    return NULL;
}

void* _MAClientSendRepairState(void)
{
    if (verbose) puts("STUB: _MAClientSendRepairState called");
    return NULL;
}

void* _MAMigrateAssets(void)
{
    if (verbose) puts("STUB: _MAMigrateAssets called");
    return NULL;
}

void* _MAclientSendCoverageDump(void)
{
    if (verbose) puts("STUB: _MAclientSendCoverageDump called");
    return NULL;
}

void* _MAclientSendQueryNSUrlState(void)
{
    if (verbose) puts("STUB: _MAclientSendQueryNSUrlState called");
    return NULL;
}

void* _MAclientSendRepairStateSync(void)
{
    if (verbose) puts("STUB: _MAclientSendRepairStateSync called");
    return NULL;
}

void* _MAclientSendServerUrlOverride(void)
{
    if (verbose) puts("STUB: _MAclientSendServerUrlOverride called");
    return NULL;
}

void* _MAensureExtension(void)
{
    if (verbose) puts("STUB: _MAensureExtension called");
    return NULL;
}

void* _MAsendCancelDownload(void)
{
    if (verbose) puts("STUB: _MAsendCancelDownload called");
    return NULL;
}

void* _MAsendConfigDownload(void)
{
    if (verbose) puts("STUB: _MAsendConfigDownload called");
    return NULL;
}

void* _MAsendDownloadAsset(void)
{
    if (verbose) puts("STUB: _MAsendDownloadAsset called");
    return NULL;
}

void* _MAsendDownloadMetaData(void)
{
    if (verbose) puts("STUB: _MAsendDownloadMetaData called");
    return NULL;
}

void* _MAsendDumpClientAccess(void)
{
    if (verbose) puts("STUB: _MAsendDumpClientAccess called");
    return NULL;
}

void* _MAsendGarbageCollect(void)
{
    if (verbose) puts("STUB: _MAsendGarbageCollect called");
    return NULL;
}

void* _MAsendGetPallasAudience(void)
{
    if (verbose) puts("STUB: _MAsendGetPallasAudience called");
    return NULL;
}

void* _MAsendPurgeAsset(void)
{
    if (verbose) puts("STUB: _MAsendPurgeAsset called");
    return NULL;
}

void* _MAsendQueryMetaData(void)
{
    if (verbose) puts("STUB: _MAsendQueryMetaData called");
    return NULL;
}

void* _MAsendSetPallasAudience(void)
{
    if (verbose) puts("STUB: _MAsendSetPallasAudience called");
    return NULL;
}

void* _MAsendSetPallasUrl(void)
{
    if (verbose) puts("STUB: _MAsendSetPallasUrl called");
    return NULL;
}

void* _MAsendUpdateClientAccess(void)
{
    if (verbose) puts("STUB: _MAsendUpdateClientAccess called");
    return NULL;
}

void* _MobileAssetGarbageCollectionBehaviorForAttributes(void)
{
    if (verbose) puts("STUB: _MobileAssetGarbageCollectionBehaviorForAttributes called");
    return NULL;
}

void* _MobileAssetLog(void)
{
    if (verbose) puts("STUB: _MobileAssetLog called");
    return NULL;
}

void* __DiscardDownloadQueueToken(void)
{
    if (verbose) puts("STUB: __DiscardDownloadQueueToken called");
    return NULL;
}

void* __ForceMigration(void)
{
    if (verbose) puts("STUB: __ForceMigration called");
    return NULL;
}

void* __RetryQueuedDownloads(void)
{
    if (verbose) puts("STUB: __RetryQueuedDownloads called");
    return NULL;
}

void* __StartBackgroundedDownloads(void)
{
    if (verbose) puts("STUB: __StartBackgroundedDownloads called");
    return NULL;
}

void* __TriggerAssetMigration(void)
{
    if (verbose) puts("STUB: __TriggerAssetMigration called");
    return NULL;
}

void* _getClientCallbackQueue(void)
{
    if (verbose) puts("STUB: _getClientCallbackQueue called");
    return NULL;
}

void* addObjectToMessage(void)
{
    if (verbose) puts("STUB: addObjectToMessage called");
    return NULL;
}
