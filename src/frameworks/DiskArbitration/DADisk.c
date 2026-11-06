#include <DiskArbitration/DiskArbitration.h>
#include <stddef.h>
#include <string.h>
#include <CoreFoundation/CFRuntime.h>
#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFURL.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CoreFoundation.h>
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
	DADiskRef disk = DACreateDisk(NULL, session, bytes,
		!strncmp(bytes, "/dev/", 5) ? bytes + 5 : NULL);
	free(bytes);
	if (disk && description) disk->snapshot = (CFDictionaryRef)CFRetain(description);
	return disk;
}

DADiskRef DADiskCreateFromBSDName(CFAllocatorRef allocator, DASessionRef session,
	const char *name)
{
	if (!name) return NULL;
	/* Native construction creates a token even for nonexistent BSD names. */
	if (!strncmp(name, "/dev/", 5)) name += 5;
	size_t length = strlen(name);
	char *identity = malloc(length + 6);
	if (!identity) return NULL;
	memcpy(identity, "/dev/", 5);
	memcpy(identity + 5, name, length + 1);
	DADiskRef disk = DACreateDisk(allocator, session, identity, name);
	free(identity);
	return disk;
}

DADiskRef DADiskCreateFromIOMedia(CFAllocatorRef allocator, DASessionRef session,
	io_service_t media)
{
	/* The image provider does not publish IOKit objects. */
	return NULL;
}

const char *DADiskGetBSDName(DADiskRef disk) { return disk ? disk->bsdName : NULL; }
io_service_t DADiskCopyIOMedia(DADiskRef disk) { return 0; }

typedef int (*DAImageVisitor)(const char *, const struct DAImageReply *, void *);

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

static CFDictionaryRef DAImageDescription(const struct DAImageReply *state, Boolean whole)
{
	CFMutableDictionaryRef description = CFDictionaryCreateMutable(NULL, 0,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (!description) return NULL;
	Boolean volume = !whole || !state->partitioned;
	CFDictionarySetValue(description, kDADiskDescriptionMediaWholeKey,
		whole ? kCFBooleanTrue : kCFBooleanFalse);
	CFDictionarySetValue(description, kDADiskDescriptionMediaLeafKey,
		volume ? kCFBooleanTrue : kCFBooleanFalse);
	CFDictionarySetValue(description, kDADiskDescriptionMediaEjectableKey, kCFBooleanTrue);
	CFDictionarySetValue(description, kDADiskDescriptionMediaWritableKey, kCFBooleanFalse);
	DASetNumber(description, kDADiskDescriptionMediaSizeKey,
		whole ? state->imageSize : state->partitionSize);
	if (volume) {
		CFDictionarySetValue(description, kDADiskDescriptionVolumeMountableKey, kCFBooleanTrue);
		CFDictionarySetValue(description, kDADiskDescriptionVolumeKindKey, CFSTR("hfs"));
		CFDictionarySetValue(description, kDADiskDescriptionVolumeNetworkKey, kCFBooleanFalse);
		CFDictionarySetValue(description, kDADiskDescriptionMediaContentKey, CFSTR("Apple_HFS"));
		if (state->mountID && state->mount[0]) {
			CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,
				(const UInt8 *)state->mount, strlen(state->mount), true);
			if (url) { CFDictionarySetValue(description, kDADiskDescriptionVolumePathKey, url); CFRelease(url); }
		}
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

struct DAImageLookup {
	const char *identity;
	const char *mount;
	struct DAImageReply state;
	char socket[280];
	Boolean found;
	Boolean whole;
};

static int DAFindImage(const char *socket, const struct DAImageReply *reply, void *opaque)
{
	struct DAImageLookup *lookup = opaque;
	char partition[DA_IMAGE_ID + 16];
	snprintf(partition, sizeof(partition), "%s:%u", reply->identity, reply->partitionIndex);
	Boolean whole = lookup->identity && !strcmp(lookup->identity, reply->identity);
	Boolean child = lookup->identity && reply->partitioned && !strcmp(lookup->identity, partition);
	Boolean mount = lookup->mount && reply->mountID && !strcmp(lookup->mount, reply->mount);
	if (!whole && !child && !mount) return 0;
	lookup->state = *reply;
	strlcpy(lookup->socket, socket, sizeof(lookup->socket));
	lookup->found = true;
	lookup->whole = whole || (!reply->partitioned && mount);
	return 1;
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
	return DACreateDisk(allocator, session, identity, NULL);
}

DADiskRef DADiskCopyWholeDisk(DADiskRef disk)
{
	if (!disk) return NULL;
	struct DAImageLookup lookup = { .identity = disk->identity };
	if (DAVisitImages(DAFindImage, &lookup) || !lookup.found) return NULL;
	return DACreateDisk(CFGetAllocator(disk), disk->session, lookup.state.identity, NULL);
}

struct DAImageEnumeration { DASessionRef session; CFMutableArrayRef disks; };

static int DAAppendImage(const char *socket, const struct DAImageReply *state, void *opaque)
{
	struct DAImageEnumeration *enumeration = opaque;
	for (int child = 0; child <= (int)state->partitioned; ++child) {
		char identity[DA_IMAGE_ID + 16];
		if (child) snprintf(identity, sizeof(identity), "%s:%u", state->identity, state->partitionIndex);
		else strlcpy(identity, state->identity, sizeof(identity));
		DADiskRef disk = DACreateDisk(NULL, enumeration->session, identity, NULL);
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
		(operation == DA_IMAGE_MOUNT && (options & ~kDADiskMountOptionWhole))) error = ENOTSUP;
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
