#include <DiskArbitration/DiskArbitration.h>
#include <stddef.h>
#include <string.h>
#include <CoreFoundation/CFRuntime.h>
#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFURL.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFUUID.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include "DAImageInternal.h"
#include "../../external/darling-dmg/src/DAImageProtocol.h"
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <pthread.h>

#define CONST_STRING_DECL(name, value) const CFStringRef name = CFSTR(value);

CONST_STRING_DECL(kDADiskDescriptionVolumeKindKey, "DAVolumeKind");
CONST_STRING_DECL(kDADiskDescriptionVolumeMountableKey, "DAVolumeMountable");
CONST_STRING_DECL(kDADiskDescriptionVolumeNameKey, "DAVolumeName");
CONST_STRING_DECL(kDADiskDescriptionVolumeNetworkKey, "DAVolumeNetwork");
CONST_STRING_DECL(kDADiskDescriptionVolumePathKey, "DAVolumePath");
CONST_STRING_DECL(kDADiskDescriptionVolumeTypeKey, "DAVolumeType");
CONST_STRING_DECL(kDADiskDescriptionVolumeUUIDKey, "DAVolumeUUID");

CONST_STRING_DECL(kDADiskDescriptionMediaBlockSizeKey, "DAMediaBlockSize");
CONST_STRING_DECL(kDADiskDescriptionMediaBSDMajorKey, "DAMediaBSDMajor");
CONST_STRING_DECL(kDADiskDescriptionMediaBSDMinorKey, "DAMediaBSDMinor");
CONST_STRING_DECL(kDADiskDescriptionMediaBSDNameKey, "DAMediaBSDName");
CONST_STRING_DECL(kDADiskDescriptionMediaBSDUnitKey, "DAMediaBSDUnit");
CONST_STRING_DECL(kDADiskDescriptionMediaContentKey, "DAMediaContent");
CONST_STRING_DECL(kDADiskDescriptionMediaEjectableKey, "DAMediaEjectable");
CONST_STRING_DECL(kDADiskDescriptionMediaIconKey, "DAMediaIcon");
CONST_STRING_DECL(kDADiskDescriptionMediaKindKey, "DAMediaKind");
CONST_STRING_DECL(kDADiskDescriptionMediaLeafKey, "DAMediaLeaf");
CONST_STRING_DECL(kDADiskDescriptionMediaNameKey, "DAMediaName");
CONST_STRING_DECL(kDADiskDescriptionMediaPathKey, "DAMediaPath");
CONST_STRING_DECL(kDADiskDescriptionMediaRemovableKey, "DAMediaRemovable");
CONST_STRING_DECL(kDADiskDescriptionMediaSizeKey, "DAMediaSize");
CONST_STRING_DECL(kDADiskDescriptionMediaTypeKey, "DAMediaType");
CONST_STRING_DECL(kDADiskDescriptionMediaUUIDKey, "DAMediaUUID");
CONST_STRING_DECL(kDADiskDescriptionMediaWholeKey, "DAMediaWhole");
CONST_STRING_DECL(kDADiskDescriptionMediaWritableKey, "DAMediaWritable");
CONST_STRING_DECL(kDADiskDescriptionMediaEncryptedKey, "DAMediaEncrypted");
CONST_STRING_DECL(kDADiskDescriptionMediaEncryptionDetailKey, "DAMediaEncryptionDetail");

CONST_STRING_DECL(kDADiskDescriptionDeviceGUIDKey, "DADeviceGUID");
CONST_STRING_DECL(kDADiskDescriptionDeviceInternalKey, "DADeviceInternal");
CONST_STRING_DECL(kDADiskDescriptionDeviceModelKey, "DADeviceModel");
CONST_STRING_DECL(kDADiskDescriptionDevicePathKey, "DADevicePath");
CONST_STRING_DECL(kDADiskDescriptionDeviceProtocolKey, "DADeviceProtocol");
CONST_STRING_DECL(kDADiskDescriptionDeviceRevisionKey, "DADeviceRevision");
CONST_STRING_DECL(kDADiskDescriptionDeviceUnitKey, "DADeviceUnit");
CONST_STRING_DECL(kDADiskDescriptionDeviceVendorKey, "DADeviceVendor");
CONST_STRING_DECL(kDADiskDescriptionDeviceTDMLockedKey, "DADeviceTDMLocked");

CONST_STRING_DECL(kDADiskDescriptionBusNameKey, "DABusName");
CONST_STRING_DECL(kDADiskDescriptionBusPathKey, "DABusPath");

/* Apple's client obtains media from its Mach daemon/IOKit. The sibling fork
 * supplies only dummy media. This implementation instead contacts the genuine
 * opened-image producer in darling-dmg, and never invents BSD device nodes. */
struct __DADisk {
	CFRuntimeBase base;
	DASessionRef session;
	char *identity;
	char *bsdName;
	CFDictionaryRef snapshot;
};

static CFTypeID diskType;

static void DADiskFinalize(CFTypeRef object)
{
	DADiskRef disk = (DADiskRef)object;
	if (disk->session) CFRelease(disk->session);
	if (disk->snapshot) CFRelease(disk->snapshot);
	free(disk->identity);
	free(disk->bsdName);
}

static Boolean DADiskEqual(CFTypeRef a, CFTypeRef b)
{
	return strcmp(((DADiskRef)a)->identity, ((DADiskRef)b)->identity) == 0;
}

static CFHashCode DADiskHash(CFTypeRef object)
{
	const unsigned char *p = (const unsigned char *)((DADiskRef)object)->identity;
	CFHashCode hash = 5381;
	while (*p) hash = hash * 33 + *p++;
	return hash;
}

static const CFRuntimeClass diskClass = {
	0, "DADisk", NULL, NULL, DADiskFinalize, DADiskEqual, DADiskHash, NULL, NULL
};

__attribute__((constructor)) static void DADiskInitialize(void)
{
	diskType = _CFRuntimeRegisterClass(&diskClass);
}

CFTypeID DADiskGetTypeID(void) { return diskType; }

static DADiskRef DACreateDisk(CFAllocatorRef allocator, DASessionRef session,
	const char *identity, const char *bsdName)
{
	if (!session || !identity) return NULL;
	DADiskRef disk = (DADiskRef)_CFRuntimeCreateInstance(allocator, diskType,
		sizeof(*disk) - sizeof(CFRuntimeBase), NULL);
	if (!disk) return NULL;
	disk->session = (DASessionRef)CFRetain(session);
	disk->identity = strdup(identity);
	disk->bsdName = bsdName ? strdup(bsdName) : NULL;
	disk->snapshot = NULL;
	if (!disk->identity || (bsdName && !disk->bsdName)) {
		CFRelease(disk);
		return NULL;
	}
	return disk;
}

DASessionRef _DADiskGetSession(DADiskRef disk) { return disk->session; }
const char *_DADiskGetID(DADiskRef disk) { return disk->identity; }

static pthread_key_t callbackDiskKey;
static pthread_once_t callbackDiskOnce = PTHREAD_ONCE_INIT;
static int callbackDiskKeyError;
static void DACreateCallbackDiskKey(void)
{
	callbackDiskKeyError = pthread_key_create(&callbackDiskKey, NULL);
}
static DADiskRef DAGetCallbackDisk(void)
{
	pthread_once(&callbackDiskOnce, DACreateCallbackDiskKey);
	return callbackDiskKeyError ? NULL : pthread_getspecific(callbackDiskKey);
}
DADiskRef _DADiskSetCallbackDisk(DADiskRef disk)
{
	DADiskRef previous = DAGetCallbackDisk();
	if (!callbackDiskKeyError) pthread_setspecific(callbackDiskKey, disk);
	return previous;
}

DADiskRef _DADiskCreateImageSnapshot(DASessionRef session, CFStringRef identity,
	CFDictionaryRef description)
{
	CFIndex capacity = CFStringGetMaximumSizeForEncoding(CFStringGetLength(identity), kCFStringEncodingUTF8) + 1;
	char *bytes = malloc((size_t)capacity);
	if (!bytes) return NULL;
	if (!CFStringGetCString(identity, bytes, capacity, kCFStringEncodingUTF8)) { free(bytes); return NULL; }
	const char *bsdName = !strncmp(bytes, "/dev/", 5) ? bytes + 5 : NULL;
	char bsdNameBuffer[128];
	if (description) {
		CFTypeRef value = CFDictionaryGetValue(description, kDADiskDescriptionMediaBSDNameKey);
		if (value && CFGetTypeID(value) == CFStringGetTypeID() &&
			CFStringGetCString(value, bsdNameBuffer, sizeof(bsdNameBuffer), kCFStringEncodingUTF8))
			bsdName = bsdNameBuffer;
	}
	DADiskRef disk = DACreateDisk(NULL, session, bytes, bsdName);
	free(bytes);
	if (disk && description) disk->snapshot = (CFDictionaryRef)CFRetain(description);
	return disk;
}

/* Disk identity remains the opened-image identity; a BSD name or a live
 * registry entry is an additional, independently validated alias. */
typedef int (*DAImageVisitor)(const char *, const struct DAImageReply *, void *);

struct DAImageLookup {
	const char *identity;
	const char *mount;
	const char *bsdName;
	uint64_t registryID;
	struct DAImageReply state;
	char socket[280];
	Boolean found;
	Boolean whole;
};

static int DAVisitImages(DAImageVisitor visitor, void *context);
static int DAFindImage(const char *socket, const struct DAImageReply *reply, void *opaque);

DADiskRef DADiskCreateFromBSDName(CFAllocatorRef allocator, DASessionRef session,
	const char *name)
{
	if (!name) return NULL;
	if (!strncmp(name, "/dev/", 5)) name += 5;
	/* A published producer reply is the only authority for a real device. */
	struct DAImageLookup lookup = { .bsdName = name };
	int error = DAVisitImages(DAFindImage, &lookup);
	if (!error && lookup.found) {
		char identity[DA_IMAGE_ID + 16];
		if (lookup.whole)
			strlcpy(identity, lookup.state.identity, sizeof(identity));
		else
			snprintf(identity, sizeof(identity), "%s:%u",
				lookup.state.identity, lookup.state.partitionIndex);
		const char *bsdName = lookup.whole ? lookup.state.wholeBSDName : lookup.state.bsdName;
		return DACreateDisk(allocator, session, identity, bsdName[0] ? bsdName : name);
	}
	/* Native construction creates a token even for nonexistent BSD names. */
	size_t length = strlen(name);
	char *identity = malloc(length + 6);
	if (!identity) return NULL;
	memcpy(identity, "/dev/", 5);
	memcpy(identity + 5, name, length + 1);
	DADiskRef disk = DACreateDisk(allocator, session, identity, name);
	free(identity);
	return disk;
}

const char *DADiskGetBSDName(DADiskRef disk) { return disk ? disk->bsdName : NULL; }

/* Resolve a live iokitd IOMedia service for the state's selected or whole
 * object. Registry IDs are authoritative when the producer supplies them;
 * the BSD name remains the stable alias and is always cross-checked. */
static Boolean DAServiceHasBSDName(io_service_t service, const char *name)
{
	if (!service || MACH_PORT_NULL == service || !name || !name[0]) return false;
	CFTypeRef value = IORegistryEntryCreateCFProperty(service, CFSTR("BSD Name"), NULL, 0);
	Boolean matches = false;
	if (value && CFGetTypeID(value) == CFStringGetTypeID()) {
		char buffer[128];
		if (CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8))
			matches = !strcmp(buffer, name);
	}
	if (value) CFRelease(value);
	return matches;
}

static io_service_t DACopyImageService(const struct DAImageReply *state, Boolean whole)
{
	const char *name = whole ? state->wholeBSDName : state->bsdName;
	if (!name[0] && !whole) name = state->wholeBSDName;
	uint64_t registryID = whole ? state->wholeRegistryEntryID : state->registryEntryID;
	io_service_t service = MACH_PORT_NULL;
	if (registryID) {
		CFMutableDictionaryRef matching = IORegistryEntryIDMatching(registryID);
		if (matching) service = IOServiceGetMatchingService(kIOMasterPortDefault, matching);
		if (service && name[0] && !DAServiceHasBSDName(service, name)) {
			IOObjectRelease(service);
			service = MACH_PORT_NULL;
		}
	}
	if (MACH_PORT_NULL == service && name[0]) {
		CFMutableDictionaryRef matching = IOBSDNameMatching(kIOMasterPortDefault, 0, name);
		if (matching) service = IOServiceGetMatchingService(kIOMasterPortDefault, matching);
		if (service && !DAServiceHasBSDName(service, name)) {
			IOObjectRelease(service);
			service = MACH_PORT_NULL;
		}
	}
	return service;
}

static int DAVisitImages(DAImageVisitor visitor, void *context)
{
	char base[80];
	DAImageDirectory(base, sizeof(base));
	int error = DAImageCheckDirectory(base);
	if (error == ENOENT) return 0;
	if (error) return error;
	DIR *directory = opendir(base);
	if (!directory) return errno;
	struct dirent *entry;
	while ((entry = readdir(directory))) {
		if (entry->d_name[0] == '.') continue;
		char child[256], socketPath[280];
		if (snprintf(child, sizeof(child), "%s/%s", base, entry->d_name) >= sizeof(child))
			continue;
		if (DAImageCheckDirectory(child)) continue;
		snprintf(socketPath, sizeof(socketPath), "%s/s", child);
		struct DAImageRequest request = { .version = DA_IMAGE_VERSION, .operation = DA_IMAGE_QUERY };
		struct DAImageReply reply;
		int status = DAImageCall(socketPath, &request, &reply);
		if (status == ENOENT || status == ECONNREFUSED) continue;
		if (status || reply.error) { error = status ? status : reply.error; break; }
		int result = visitor(socketPath, &reply, context);
		if (result) { error = result < 0 ? -result : 0; break; }
	}
	closedir(directory);
	return error;
}

static void DASetNumber(CFMutableDictionaryRef description, CFStringRef key, uint64_t value)
{
	CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt64Type, &value);
	if (number) { CFDictionarySetValue(description, key, number); CFRelease(number); }
}

static void DASetString(CFMutableDictionaryRef description, CFStringRef key, const char *value)
{
	if (!value || !value[0]) return;
	CFStringRef string = CFStringCreateWithCString(NULL, value, kCFStringEncodingUTF8);
	if (string) { CFDictionarySetValue(description, key, string); CFRelease(string); }
}

static CFDictionaryRef DAImageDescription(const struct DAImageReply *state, Boolean whole)
{
	CFMutableDictionaryRef description = CFDictionaryCreateMutable(NULL, 0,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (!description) return NULL;
	Boolean volume = !whole || !state->partitioned;
	const char *diskBSDName = whole ? state->wholeBSDName : state->bsdName;
	if (!diskBSDName[0]) diskBSDName = state->bsdName[0] ? state->bsdName : state->wholeBSDName;
	CFDictionarySetValue(description, kDADiskDescriptionMediaWholeKey,
		whole ? kCFBooleanTrue : kCFBooleanFalse);
	CFDictionarySetValue(description, kDADiskDescriptionMediaLeafKey,
		volume ? kCFBooleanTrue : kCFBooleanFalse);
	CFDictionarySetValue(description, kDADiskDescriptionMediaEjectableKey, kCFBooleanTrue);
	CFDictionarySetValue(description, kDADiskDescriptionMediaWritableKey, kCFBooleanFalse);
	DASetNumber(description, kDADiskDescriptionMediaSizeKey,
		whole ? state->imageSize : state->partitionSize);
	if (state->blockSize)
		DASetNumber(description, kDADiskDescriptionMediaBlockSizeKey, state->blockSize);
	DASetString(description, kDADiskDescriptionMediaBSDNameKey, diskBSDName);
	/* The producer publishes one minor for the selected data node. A whole
	 * partitioned media has a different minor that is not in this reply. */
	if (volume && state->deviceMinor)
		DASetNumber(description, kDADiskDescriptionMediaBSDMinorKey, state->deviceMinor);
	/* The producer publishes the selected partition's content; a partitioned
	 * whole media has a different (unpublished) partition-map content. */
	if (volume) DASetString(description, kDADiskDescriptionMediaContentKey, state->content);
	if (!volume) {
		CFDictionarySetValue(description, kDADiskDescriptionVolumeMountableKey, kCFBooleanFalse);
		return description;
	}
	CFDictionarySetValue(description, kDADiskDescriptionVolumeMountableKey,
		state->filesystem[0] ? kCFBooleanTrue : kCFBooleanFalse);
	DASetString(description, kDADiskDescriptionVolumeKindKey, state->filesystem);
	/* VolumeType is the human filesystem type, derived only from the actually
	 * probed filesystem; the DVTDownloads consumer compares it with "APFS". */
	if (!strcmp(state->filesystem, "apfs"))
		DASetString(description, kDADiskDescriptionVolumeTypeKey, "APFS");
	else if (!strcmp(state->filesystem, "hfs"))
		DASetString(description, kDADiskDescriptionVolumeTypeKey, "HFS");
	CFDictionarySetValue(description, kDADiskDescriptionVolumeNetworkKey, kCFBooleanFalse);
	DASetString(description, kDADiskDescriptionVolumeNameKey, state->volumeName);
	if (state->volumeUUID[0]) {
		CFStringRef text = CFStringCreateWithCString(NULL, state->volumeUUID, kCFStringEncodingUTF8);
		CFUUIDRef uuid = text ? CFUUIDCreateFromString(NULL, text) : NULL;
		if (uuid) { CFDictionarySetValue(description, kDADiskDescriptionVolumeUUIDKey, uuid); CFRelease(uuid); }
		if (text) CFRelease(text);
	}
	if (state->mountID && state->mount[0]) {
		CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,
			(const UInt8 *)state->mount, strlen(state->mount), true);
		if (url) { CFDictionarySetValue(description, kDADiskDescriptionVolumePathKey, url); CFRelease(url); }
	}
	return description;
}

DADiskRef _DAImageCreateApprovalDisk(DASessionRef session, const struct DAImageReply *state)
{
	char identity[DA_IMAGE_ID + 16];
	if (state->partitioned) snprintf(identity, sizeof(identity), "%s:%u", state->identity, state->partitionIndex);
	else strlcpy(identity, state->identity, sizeof(identity));
	CFStringRef key = CFStringCreateWithCString(NULL, identity, kCFStringEncodingUTF8);
	CFDictionaryRef description = DAImageDescription(state, !state->partitioned);
	DADiskRef disk = key && description ? _DADiskCreateImageSnapshot(session, key, description) : NULL;
	if (key) CFRelease(key);
	if (description) CFRelease(description);
	return disk;
}

static int DAFindImage(const char *socket, const struct DAImageReply *reply, void *opaque)
{
	struct DAImageLookup *lookup = opaque;
	char partition[DA_IMAGE_ID + 16];
	snprintf(partition, sizeof(partition), "%s:%u", reply->identity, reply->partitionIndex);
	Boolean match = false, whole = false;
	if (lookup->identity) {
		if (!strcmp(lookup->identity, reply->identity)) { match = true; whole = true; }
		else if (reply->partitioned && !strcmp(lookup->identity, partition)) { match = true; whole = false; }
	}
	if (!match && lookup->bsdName && lookup->bsdName[0]) {
		if (reply->wholeBSDName[0] && !strcmp(lookup->bsdName, reply->wholeBSDName)) { match = true; whole = true; }
		else if (reply->bsdName[0] && !strcmp(lookup->bsdName, reply->bsdName)) {
			match = true;
			whole = !reply->partitioned;
		}
	}
	if (!match && lookup->registryID) {
		if (reply->wholeRegistryEntryID && reply->wholeRegistryEntryID == lookup->registryID) {
			match = true;
			whole = true;
		} else if (reply->registryEntryID && reply->registryEntryID == lookup->registryID) {
			match = true;
			whole = !reply->partitioned;
		}
	}
	if (!match && lookup->mount && reply->mountID && !strcmp(lookup->mount, reply->mount)) {
		match = true;
		whole = !reply->partitioned;
	}
	if (!match) return 0;
	lookup->state = *reply;
	strlcpy(lookup->socket, socket, sizeof(lookup->socket));
	lookup->found = true;
	lookup->whole = whole;
	return 1;
}

DADiskRef DADiskCreateFromIOMedia(CFAllocatorRef allocator, DASessionRef session,
	io_service_t media)
{
	if (!media || MACH_PORT_NULL == media || !IOObjectConformsTo(media, kIOMediaClass))
		return NULL;
	CFTypeRef value = IORegistryEntryCreateCFProperty(media, CFSTR("BSD Name"), NULL, 0);
	if (!value || CFGetTypeID(value) != CFStringGetTypeID()) {
		if (value) CFRelease(value);
		return NULL;
	}
	char bsdName[128];
	Boolean converted = CFStringGetCString(value, bsdName, sizeof(bsdName), kCFStringEncodingUTF8);
	CFRelease(value);
	if (!converted) return NULL;
	struct DAImageLookup lookup = { .bsdName = bsdName };
	if (DAVisitImages(DAFindImage, &lookup) || !lookup.found) return NULL;
	char identity[DA_IMAGE_ID + 16];
	if (lookup.whole) strlcpy(identity, lookup.state.identity, sizeof(identity));
	else snprintf(identity, sizeof(identity), "%s:%u",
		lookup.state.identity, lookup.state.partitionIndex);
	const char *diskBSDName = lookup.whole ? lookup.state.wholeBSDName : lookup.state.bsdName;
	return DACreateDisk(allocator, session, identity, diskBSDName[0] ? diskBSDName : bsdName);
}

io_service_t DADiskCopyIOMedia(DADiskRef disk)
{
	if (!disk) return MACH_PORT_NULL;
	struct DAImageLookup lookup = { .identity = disk->identity };
	if (DAVisitImages(DAFindImage, &lookup) || !lookup.found) return MACH_PORT_NULL;
	return DACopyImageService(&lookup.state, lookup.whole);
}

CFDictionaryRef DADiskCopyDescription(DADiskRef disk)
{
	if (!disk) return NULL;
	if (disk->snapshot && disk == DAGetCallbackDisk())
		return (CFDictionaryRef)CFRetain(disk->snapshot);
	struct DAImageLookup lookup = { .identity = disk->identity };
	if (DAVisitImages(DAFindImage, &lookup) || !lookup.found) return NULL;
	return DAImageDescription(&lookup.state, lookup.whole);
}

DADiskRef DADiskCreateFromVolumePath(CFAllocatorRef allocator, DASessionRef session,
	CFURLRef url)
{
	UInt8 path[DA_IMAGE_PATH];
	if (!url || !CFURLGetFileSystemRepresentation(url, true, path, sizeof(path))) return NULL;
	char canonical[DA_IMAGE_PATH];
	if (!realpath((const char *)path, canonical)) return NULL;
	struct DAImageLookup lookup = { .mount = canonical };
	if (DAVisitImages(DAFindImage, &lookup) || !lookup.found) return NULL;
	char identity[DA_IMAGE_ID + 16];
	if (lookup.state.partitioned)
		snprintf(identity, sizeof(identity), "%s:%u", lookup.state.identity, lookup.state.partitionIndex);
	else strlcpy(identity, lookup.state.identity, sizeof(identity));
	const char *bsdName = lookup.state.bsdName[0] ? lookup.state.bsdName :
		(lookup.state.wholeBSDName[0] ? lookup.state.wholeBSDName : NULL);
	return DACreateDisk(allocator, session, identity, bsdName);
}

DADiskRef DADiskCopyWholeDisk(DADiskRef disk)
{
	if (!disk) return NULL;
	struct DAImageLookup lookup = { .identity = disk->identity };
	if (DAVisitImages(DAFindImage, &lookup) || !lookup.found) return NULL;
	const char *bsdName = lookup.state.wholeBSDName[0] ? lookup.state.wholeBSDName : NULL;
	return DACreateDisk(CFGetAllocator(disk), disk->session, lookup.state.identity, bsdName);
}

struct DAImageEnumeration { DASessionRef session; CFMutableArrayRef disks; };

static int DAAppendImage(const char *socket, const struct DAImageReply *state, void *opaque)
{
	struct DAImageEnumeration *enumeration = opaque;
	for (int child = 0; child <= (int)state->partitioned; ++child) {
		char identity[DA_IMAGE_ID + 16];
		if (child) snprintf(identity, sizeof(identity), "%s:%u", state->identity, state->partitionIndex);
		else strlcpy(identity, state->identity, sizeof(identity));
		const char *bsdName = child ? state->bsdName : state->wholeBSDName;
		if (!bsdName[0]) bsdName = child ? state->wholeBSDName : state->bsdName;
		DADiskRef disk = DACreateDisk(NULL, enumeration->session, identity,
			bsdName[0] ? bsdName : NULL);
		if (!disk) return -ENOMEM;
		disk->snapshot = DAImageDescription(state, !child);
		if (!disk->snapshot) { CFRelease(disk); return -ENOMEM; }
		CFArrayAppendValue(enumeration->disks, disk);
		CFRelease(disk);
	}
	return 0;
}

CFArrayRef _DAImageCopyDisks(DASessionRef session)
{
	CFMutableArrayRef disks = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	if (!disks) return NULL;
	struct DAImageEnumeration enumeration = { session, disks };
	if (DAVisitImages(DAAppendImage, &enumeration)) { CFRelease(disks); return NULL; }
	return disks;
}

static DAReturn DAImageError(int error)
{
	switch (error) {
	case 0: return kDAReturnSuccess;
	case EBUSY: return kDAReturnBusy;
	case ENOENT: case ECONNREFUSED: return kDAReturnNotFound;
	case ENODEV: return kDAReturnNotMounted;
	case EACCES: case EPERM: return kDAReturnNotPermitted;
	case EINVAL: case ENAMETOOLONG: return kDAReturnBadArgument;
	case ENOMEM: return kDAReturnNoResources;
	case ENOTSUP: return kDAReturnUnsupported;
	default: return kDAReturnError;
	}
}

DAReturn _DAImagePerformOperation(DADiskRef disk, unsigned operation, CFURLRef path,
	unsigned options, CFArrayRef arguments, CFStringRef *message)
{
	int error = 0;
	if ((operation == DA_IMAGE_EJECT && options) ||
		(operation == DA_IMAGE_MOUNT && (options & ~kDADiskMountOptionWhole)) ||
		(operation == DA_IMAGE_UNMOUNT && (options & ~kDADiskUnmountOptionWhole)))
		error = ENOTSUP;
	/* The existing FUSE reader is read-only; never accept a writable request. */
	if (!error && arguments) {
		for (CFIndex i = 0; i < CFArrayGetCount(arguments); ++i) {
			CFStringRef argument = CFArrayGetValueAtIndex(arguments, i);
			if (!CFEqual(argument, CFSTR("rdonly")) &&
				!CFEqual(argument, CFSTR("ro")) &&
				!CFEqual(argument, CFSTR("nobrowse"))) { error = ENOTSUP; break; }
		}
	}
	struct DAImageLookup lookup = { .identity = disk->identity };
	if (!error) error = DAVisitImages(DAFindImage, &lookup);
	if (!error && !lookup.found) error = ENOENT;
	if (!error && operation == DA_IMAGE_UNMOUNT && lookup.whole &&
		lookup.state.partitioned && !(options & kDADiskUnmountOptionWhole))
		error = ENOTSUP;
	struct DAImageRequest request = { .version = DA_IMAGE_VERSION, .operation = operation };
	if (!error && operation == DA_IMAGE_MOUNT) {
		if (lookup.whole && lookup.state.partitioned && !(options & kDADiskMountOptionWhole))
			error = ENOTSUP;
		else if (path) {
			if (!CFURLGetFileSystemRepresentation(path, true, (UInt8 *)request.mount,
					sizeof(request.mount)) || request.mount[0] != '/') error = EINVAL;
		} else {
			/* Identity-derived default avoids image pathname/basename collisions. */
			snprintf(request.mount, sizeof(request.mount), "/Volumes/%s",
				lookup.state.identity + strlen("darling-image:"));
		}
	}
	if (!error) {
		struct DAImageReply reply;
		error = DAImageCall(lookup.socket, &request, &reply);
		if (!error && reply.approvalStatus) {
			if (message && reply.reason[0])
				*message = CFStringCreateWithCString(NULL, reply.reason, kCFStringEncodingUTF8);
			return reply.approvalStatus;
		}
		if (!error) error = reply.error;
	}
	if (error && message) *message = CFStringCreateWithCString(NULL, strerror(error), kCFStringEncodingUTF8);
	return DAImageError(error);
}

void DADiskMountWithArguments(DADiskRef disk, CFURLRef path, DADiskMountOptions options,
	DADiskMountCallback callback, void *context, CFStringRef arguments[])
{
	if (!disk) return;
	CFMutableArrayRef array = NULL;
	if (arguments) {
		array = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
		if (!array) return;
		for (size_t i = 0; arguments[i]; ++i) CFArrayAppendValue(array, arguments[i]);
	}
	_DASessionQueueOperation(disk->session, disk, DA_IMAGE_MOUNT, path, options, callback, context, array);
	if (array) CFRelease(array);
}

void DADiskMount(DADiskRef disk, CFURLRef path, DADiskMountOptions options,
	DADiskMountCallback callback, void *context)
{
	DADiskMountWithArguments(disk, path, options, callback, context, NULL);
}

void DADiskEject(DADiskRef disk, DADiskEjectOptions options,
	DADiskEjectCallback callback, void *context)
{
	if (disk) _DASessionQueueOperation(disk->session, disk, DA_IMAGE_EJECT, NULL,
		options, callback, context, NULL);
}

void DADiskUnmount(DADiskRef disk, DADiskUnmountOptions options,
	DADiskUnmountCallback callback, void *context)
{
	if (disk) _DASessionQueueOperation(disk->session, disk, DA_IMAGE_UNMOUNT, NULL,
		options, callback, context, NULL);
}
