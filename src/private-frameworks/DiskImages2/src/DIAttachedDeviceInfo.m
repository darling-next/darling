#import "DIInternal.h"

static NSMutableDictionary *DI_attachedDevices = nil;
static NSRecursiveLock *DI_attachedDevicesLock = nil;

static void DIEnsureDeviceRegistry(void)
{
	if (DI_attachedDevicesLock == nil) {
		DI_attachedDevicesLock = [[NSRecursiveLock alloc] init];
	}
	if (DI_attachedDevices == nil) {
		DI_attachedDevices = [[NSMutableDictionary alloc] init];
	}
}

void DIRegisterAttachedDevice(DIAttachedDeviceInfo *info)
{
	NSString *identity = [info di_identity];
	if (identity == nil) {
		return;
	}
	DIEnsureDeviceRegistry();
	[DI_attachedDevicesLock lock];
	[DI_attachedDevices setObject:info forKey:identity];
	[DI_attachedDevicesLock unlock];
}

DIAttachedDeviceInfo *DIAttachedDeviceForIdentity(NSString *identity)
{
	if (identity == nil) {
		return nil;
	}
	DIEnsureDeviceRegistry();
	[DI_attachedDevicesLock lock];
	DIAttachedDeviceInfo *info = [[[DI_attachedDevices objectForKey:identity] retain] autorelease];
	[DI_attachedDevicesLock unlock];
	return info;
}

DIAttachedDeviceInfo *DIAttachedDeviceForBSDName(NSString *BSDName)
{
	if (BSDName == nil) {
		return nil;
	}
	DIEnsureDeviceRegistry();
	DIAttachedDeviceInfo *result = nil;
	[DI_attachedDevicesLock lock];
	NSArray *infos = [DI_attachedDevices allValues];
	for (DIAttachedDeviceInfo *info in infos) {
		if ([[info BSDName] isEqualToString:BSDName]) {
			result = [[info retain] autorelease];
			break;
		}
	}
	[DI_attachedDevicesLock unlock];
	return result;
}

void DIForgetAttachedDevice(NSString *identity)
{
	if (identity == nil) {
		return;
	}
	DIEnsureDeviceRegistry();
	[DI_attachedDevicesLock lock];
	[DI_attachedDevices removeObjectForKey:identity];
	[DI_attachedDevicesLock unlock];
}

static DIProducerState *DIStateForIdentityAndName(NSString *identity, NSString *BSDName)
{
	DIProducerState *state = nil;
	if (identity != nil) {
		state = DIProducerStateByIdentity(identity);
	}
	if (state == nil && BSDName != nil) {
		state = DIProducerStateByBSDName(BSDName);
	}
	return state;
}

@implementation DIAttachedDeviceInfo

- (instancetype)di_initWithIdentity:(NSString *)identity
	BSDName:(NSString *)BSDName
	imageURL:(NSURL *)imageURL
{
	self = [super init];
	if (self != nil) {
		_di_identity = [identity copy];
		_BSDName = [BSDName copy];
		_imageURL = [imageURL copy];
	}
	return self;
}

- (instancetype)initWithBSDName:(NSString *)BSDName error:(NSError **)error
{
	if (BSDName == nil || [BSDName length] == 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeMissingBSDName, @"No BSD name was supplied");
		}
		[self release];
		return nil;
	}
	DIProducerState *state = DIProducerStateByBSDName(BSDName);
	if (state == nil) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"No live image producer backs BSD device %@", BSDName);
		}
		[self release];
		return nil;
	}
	NSString *producerBSDName = [state bsdName];
	if (producerBSDName == nil) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeMissingBSDName, @"The image producer for BSD device %@ published no device identity", BSDName);
		}
		[self release];
		return nil;
	}
	NSURL *imageURL = nil;
	DIAttachedDeviceInfo *registered = DIAttachedDeviceForIdentity([state identity]);
	if (registered != nil) {
		imageURL = [registered imageURL];
	}
	if (imageURL == nil) {
		NSString *path = DIProducerImagePath([state identity], producerBSDName);
		if (path != nil) {
			imageURL = [NSURL fileURLWithPath:path];
		}
	}
	return [self di_initWithIdentity:[state identity]
		BSDName:producerBSDName
		imageURL:imageURL];
}

- (void)dealloc
{
	[_BSDName release];
	[_imageURL release];
	[_di_identity release];
	[super dealloc];
}

- (NSString *)BSDName { return _BSDName; }
- (NSString *)di_identity { return _di_identity; }

- (NSURL *)imageURL
{
	if (_imageURL == nil) {
		NSString *path = DIProducerImagePath(_di_identity, _BSDName);
		if (path != nil) {
			_imageURL = [[NSURL fileURLWithPath:path] retain];
		}
	}
	return _imageURL;
}

- (NSDictionary *)di_entityForState:(DIProducerState *)state
{
	NSString *BSDName = [state bsdName];
	if (BSDName == nil) {
		return nil;
	}
	NSMutableDictionary *entity = [NSMutableDictionary dictionary];
	[entity setObject:BSDName forKey:@"BSD Name"];
	[entity setObject:[@"/dev" stringByAppendingPathComponent:BSDName] forKey:@"Dev Entry"];
	[entity setObject:[NSNumber numberWithBool:YES] forKey:@"Potentially Mountable"];
	NSString *mountPath = [state mountPath];
	if (mountPath != nil) {
		[entity setObject:mountPath forKey:@"Mount Point"];
	}
	NSString *content = [state content];
	if (content != nil) {
		[entity setObject:content forKey:@"Content Hint"];
	}
	NSString *filesystem = [state filesystem];
	if (filesystem != nil) {
		[entity setObject:filesystem forKey:@"Volume Kind"];
	}
	NSString *volumeName = [state volumeName];
	if (volumeName != nil) {
		[entity setObject:volumeName forKey:@"Volume Name"];
	}
	NSString *volumeUUID = [state volumeUUID];
	if (volumeUUID != nil) {
		[entity setObject:volumeUUID forKey:@"Volume UUID"];
	}
	NSURL *imageURL = [self imageURL];
	if (imageURL != nil && [[imageURL path] length] > 0) {
		[entity setObject:[imageURL path] forKey:@"Image Path"];
	}
	return entity;
}

- (NSArray *)copyEntitiesList
{
	DIProducerState *state = DIStateForIdentityAndName(_di_identity, _BSDName);
	if (state == nil) {
		return [[NSArray alloc] init];
	}
	NSDictionary *entity = [self di_entityForState:state];
	if (entity == nil) {
		return [[NSArray alloc] init];
	}
	return [[NSArray alloc] initWithObjects:entity, nil];
}

static NSDictionary *DIImagePathMap(void)
{
	NSMutableDictionary *map = [NSMutableDictionary dictionary];
	NSDictionary *plist = DIHdiutilInfoPlist(NULL);
	if (plist == nil) {
		return map;
	}
	NSArray *images = [plist objectForKey:@"images"];
	for (NSDictionary *image in images) {
		if (![image isKindOfClass:[NSDictionary class]]) {
			continue;
		}
		NSString *path = [image objectForKey:@"image-path"];
		if (![path isKindOfClass:[NSString class]] || [path length] == 0) {
			continue;
		}
		NSString *identity = [image objectForKey:@"darling-image-id"];
		if ([identity isKindOfClass:[NSString class]]) {
			NSRange colon = [identity rangeOfString:@":"];
			if (colon.location != NSNotFound) {
				identity = [identity substringToIndex:colon.location];
			}
			[map setObject:path forKey:identity];
		}
		NSArray *entities = [image objectForKey:@"system-entities"];
		if ([entities isKindOfClass:[NSArray class]]) {
			for (NSDictionary *entity in entities) {
				if (![entity isKindOfClass:[NSDictionary class]]) {
					continue;
				}
				NSString *name = [entity objectForKey:@"bsd-name"];
				if ([name isKindOfClass:[NSString class]]) {
					[map setObject:path forKey:[@"bsd:" stringByAppendingString:name]];
				}
			}
		}
	}
	return map;
}

+ (NSArray *)newDevicesArrayWithError:(NSError **)error
{
	(void)error;
	NSArray *states = DIProducerCopyStates();
	NSMutableArray *devices = [NSMutableArray array];
	NSMutableSet *liveIdentities = [NSMutableSet set];
	NSDictionary *imagePaths = DIImagePathMap();
	for (DIProducerState *state in states) {
		NSString *BSDName = [state bsdName];
		NSString *identity = [state identity];
		if (BSDName == nil || identity == nil) {
			continue;
		}
		[liveIdentities addObject:identity];
		DIAttachedDeviceInfo *registered = DIAttachedDeviceForIdentity(identity);
		if (registered != nil) {
			[devices addObject:registered];
			continue;
		}
		NSString *path = [imagePaths objectForKey:identity];
		if (path == nil) {
			path = [imagePaths objectForKey:[@"bsd:" stringByAppendingString:BSDName]];
		}
		NSURL *url = path != nil ? [NSURL fileURLWithPath:path] : nil;
		DIAttachedDeviceInfo *created = [[DIAttachedDeviceInfo alloc] di_initWithIdentity:identity
			BSDName:BSDName
			imageURL:url];
		if (created != nil) {
			[devices addObject:created];
			[created release];
		}
	}
	/* The producer state is authoritative: records whose producer is gone are
	 * detached and must not be reported. */
	DIEnsureDeviceRegistry();
	[DI_attachedDevicesLock lock];
	NSArray *identities = [DI_attachedDevices allKeys];
	for (NSString *identity in identities) {
		if (![liveIdentities containsObject:identity]) {
			[DI_attachedDevices removeObjectForKey:identity];
		}
	}
	[DI_attachedDevicesLock unlock];
	return [[NSArray alloc] initWithArray:devices];
}

@end
