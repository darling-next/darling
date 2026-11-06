#import "DIInternal.h"

static void DICleanupFailedHandle(DIDeviceHandle *handle)
{
	if (handle == nil) {
		return;
	}
	NSString *identity = [handle di_producerIdentity];
	DIProducerState *state = nil;
	if (identity != nil) {
		state = DIProducerStateByIdentity(identity);
	}
	if (state == nil) {
		state = DIProducerStateByBSDName([handle BSDName]);
	}
	if (state != nil) {
		DIProducerEject(state, NULL);
	}
	if (identity != nil) {
		DIForgetAttachedDevice(identity);
	}
}

@implementation DiskImages2

+ (BOOL)attachWithParams:(DIAttachParams *)params
	handle:(DIDeviceHandle **)handle
	error:(NSError **)error
{
	if (handle == NULL) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"attachWithParams:handle:error: requires a handle out-parameter");
		}
		return NO;
	}
	*handle = nil;
	if (![params isKindOfClass:[DIAttachParams class]]) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"attachWithParams: requires a DIAttachParams");
		}
		return NO;
	}
	NSError *attachError = nil;
	DIDeviceHandle *device = DIAttachImage(params, &attachError);
	if (device == nil) {
		if (error != NULL) {
			*error = attachError;
		}
		return NO;
	}
	if ([params autoMount]) {
		NSError *mountError = nil;
		if (!DIMountDeviceHandle(device, nil, &mountError)) {
			DICleanupFailedHandle(device);
			[device release];
			if (error != NULL) {
				*error = mountError;
			}
			return NO;
		}
	}
	*handle = [device autorelease];
	return YES;
}

+ (BOOL)managedAttachWithParams:(DIAttachParams *)params
	handle:(DIDeviceHandle **)handle
	error:(NSError **)error
{
	if (handle == NULL) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"managedAttachWithParams:handle:error: requires a handle out-parameter");
		}
		return NO;
	}
	*handle = nil;
	if (![params isKindOfClass:[DIAttachParams class]]) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"managedAttachWithParams: requires a DIAttachParams");
		}
		return NO;
	}
	NSError *attachError = nil;
	DIDeviceHandle *device = DIAttachImage(params, &attachError);
	if (device == nil) {
		if (error != NULL) {
			*error = attachError;
		}
		return NO;
	}

	/* Managed attach owns the mount: report success only when the device's
	 * BSD name resolves to a genuinely mounted entity. */
	NSError *mountError = nil;
	if (!DIMountDeviceHandle(device, nil, &mountError)) {
		DICleanupFailedHandle(device);
		[device release];
		if (error != NULL) {
			*error = mountError;
		}
		return NO;
	}

	NSString *BSDName = [device BSDName];
	DIAttachedDeviceInfo *info = DIAttachedDeviceForBSDName(BSDName);
	if (info == nil) {
		info = [[[DIAttachedDeviceInfo alloc] initWithBSDName:BSDName error:NULL] autorelease];
	}
	NSArray *entities = [info copyEntitiesList];
	BOOL found = NO;
	for (NSDictionary *entity in entities) {
		if (![entity isKindOfClass:[NSDictionary class]]) {
			continue;
		}
		NSString *name = [entity objectForKey:@"BSD Name"];
		NSString *mount = [entity objectForKey:@"Mount Point"];
		if ([name isKindOfClass:[NSString class]] && [name isEqualToString:BSDName] &&
			[mount isKindOfClass:[NSString class]] && [mount length] != 0 &&
			DIMountPointIsReal(mount)) {
			found = YES;
			break;
		}
	}
	[entities release];
	if (!found) {
		DICleanupFailedHandle(device);
		[device release];
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Managed attach could not find a mounted entity for BSD device %@", BSDName);
		}
		return NO;
	}
	*handle = [device autorelease];
	return YES;
}

@end
