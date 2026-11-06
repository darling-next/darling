#import "DIInternal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A fixed parameter field is authoritative only when it is a terminated
 * string; an unterminated or empty field is not a value. */
static NSString *DIFieldString(const char *field, size_t size)
{
	if (field == NULL || memchr(field, 0, size) == NULL || field[0] == 0) {
		return nil;
	}
	return [NSString stringWithUTF8String:field];
}

@implementation DIProducerState

- (void)dealloc
{
	[socketPath release];
	[super dealloc];
}

- (NSString *)identity
{
	return DIFieldString(reply.identity, sizeof(reply.identity));
}

- (NSString *)bsdName
{
	return DIFieldString(reply.bsdName, sizeof(reply.bsdName));
}

- (NSString *)wholeBSDName
{
	return DIFieldString(reply.wholeBSDName, sizeof(reply.wholeBSDName));
}

- (NSString *)dataPath
{
	return DIFieldString(reply.dataPath, sizeof(reply.dataPath));
}

- (BOOL)isMounted
{
	return reply.mountID != 0 && DIFieldString(reply.mount, sizeof(reply.mount)) != nil;
}

- (NSString *)mountPath
{
	if (![self isMounted]) {
		return nil;
	}
	return DIFieldString(reply.mount, sizeof(reply.mount));
}

- (unsigned long long)mediaSize
{
	if (reply.partitioned && reply.partitionSize != 0) {
		return reply.partitionSize;
	}
	return reply.imageSize;
}

- (unsigned int)blockSize
{
	return reply.blockSize;
}

- (NSString *)content
{
	return DIFieldString(reply.content, sizeof(reply.content));
}

- (NSString *)filesystem
{
	return DIFieldString(reply.filesystem, sizeof(reply.filesystem));
}

- (NSString *)volumeName
{
	return DIFieldString(reply.volumeName, sizeof(reply.volumeName));
}

- (NSString *)volumeUUID
{
	return DIFieldString(reply.volumeUUID, sizeof(reply.volumeUUID));
}

- (unsigned long long)registryEntryID
{
	return reply.registryEntryID;
}

@end

static BOOL DIProducerReadState(NSString *socketPath, struct DAImageReply *reply)
{
	if (socketPath == nil || [socketPath length] == 0) {
		return NO;
	}
	struct DAImageRequest request;
	memset(&request, 0, sizeof(request));
	request.version = DA_IMAGE_VERSION;
	request.operation = DA_IMAGE_QUERY;
	memset(reply, 0, sizeof(*reply));
	int error = DAImageCall([socketPath fileSystemRepresentation], &request, reply);
	if (error != 0) {
		return NO;
	}
	if (reply->error != 0 || reply->version != DA_IMAGE_VERSION) {
		return NO;
	}
	return YES;
}

static DIProducerState *DIProducerStateFromReply(NSString *socketPath, const struct DAImageReply *reply)
{
	DIProducerState *state = [[DIProducerState alloc] init];
	state->socketPath = [socketPath copy];
	state->reply = *reply;
	return [state autorelease];
}

NSArray *DIProducerCopyStates(void)
{
	NSMutableArray *states = [NSMutableArray array];
	char base[80];
	DAImageDirectory(base, sizeof(base));
	if (DAImageCheckDirectory(base) != 0) {
		/* No producer directory (or not ours): there is no live attachment
		 * authority to report. */
		return states;
	}
	DIR *directory = opendir(base);
	if (directory == NULL) {
		return states;
	}
	struct dirent *entry;
	int visited = 0;
	while ((entry = readdir(directory)) != NULL) {
		if (entry->d_name[0] == '.') {
			continue;
		}
		if (++visited > 128) {
			break;
		}
		char child[256];
		if (snprintf(child, sizeof(child), "%s/%s", base, entry->d_name) >= (int)sizeof(child)) {
			continue;
		}
		if (DAImageCheckDirectory(child) != 0) {
			continue;
		}
		char socket[280];
		if (snprintf(socket, sizeof(socket), "%s/s", child) >= (int)sizeof(socket)) {
			continue;
		}
		NSString *socketPath = [NSString stringWithUTF8String:socket];
		struct DAImageReply reply;
		if (!DIProducerReadState(socketPath, &reply)) {
			continue;
		}
		[states addObject:DIProducerStateFromReply(socketPath, &reply)];
	}
	closedir(directory);
	return states;
}

DIProducerState *DIProducerStateByIdentity(NSString *identity)
{
	if (identity == nil) {
		return nil;
	}
	NSArray *states = DIProducerCopyStates();
	NSEnumerator *enumerator = [states objectEnumerator];
	DIProducerState *state;
	while ((state = [enumerator nextObject]) != nil) {
		NSString *candidate = [state identity];
		if (candidate != nil && [candidate isEqualToString:identity]) {
			return state;
		}
	}
	return nil;
}

DIProducerState *DIProducerStateByBSDName(NSString *BSDName)
{
	if (BSDName == nil) {
		return nil;
	}
	NSArray *states = DIProducerCopyStates();
	NSEnumerator *enumerator = [states objectEnumerator];
	DIProducerState *state;
	while ((state = [enumerator nextObject]) != nil) {
		NSString *name = [state bsdName];
		NSString *whole = [state wholeBSDName];
		if ((name != nil && [name isEqualToString:BSDName]) ||
			(whole != nil && [whole isEqualToString:BSDName])) {
			return state;
		}
	}
	return nil;
}


BOOL DIProducerEject(DIProducerState *state, NSError **error)
{
	if (state == nil || state->socketPath == nil) {
		return YES;
	}
	struct DAImageRequest request;
	memset(&request, 0, sizeof(request));
	request.version = DA_IMAGE_VERSION;
	request.operation = DA_IMAGE_EJECT;
	struct DAImageReply reply;
	memset(&reply, 0, sizeof(reply));
	int callError = DAImageCall([state->socketPath fileSystemRepresentation], &request, &reply);
	if (callError == ENOENT || callError == ECONNREFUSED) {
		/* The producer already retired its namespace; nothing is left. */
		return YES;
	}
	if (callError != 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot reach the image producer to eject the device: %s", strerror(callError));
		}
		return NO;
	}
	if (reply.version != DA_IMAGE_VERSION) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"The image producer returned an incompatible reply while ejecting");
		}
		return NO;
	}
	if (reply.error != 0) {
		if (error != NULL) {
			NSString *reason = DIFieldString(reply.reason, sizeof(reply.reason));
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot eject the image device: %@", reason != nil ? reason : [NSString stringWithUTF8String:strerror(reply.error)]);
		}
		return NO;
	}
	return YES;
}

BOOL DIDeviceDataPathProbe(NSString *dataPath,
	unsigned long long expectedSize,
	unsigned int blockSize,
	NSError **error)
{
	if (dataPath == nil || [dataPath length] == 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"The image producer did not publish a data node path");
		}
		return NO;
	}
	int fd = open([dataPath fileSystemRepresentation], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot open image device data node %@: %s", dataPath, strerror(errno));
		}
		return NO;
	}
	struct stat status;
	if (fstat(fd, &status) != 0) {
		int saved = errno;
		close(fd);
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot stat image device data node %@: %s", dataPath, strerror(saved));
		}
		return NO;
	}
	if (status.st_size <= 0) {
		close(fd);
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Image device data node %@ reports no readable capacity", dataPath);
		}
		return NO;
	}
	if (expectedSize != 0 && (unsigned long long)status.st_size != expectedSize) {
		close(fd);
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Image device data node %@ is %llu bytes but the producer reported %llu", dataPath, (unsigned long long)status.st_size, expectedSize);
		}
		return NO;
	}

	size_t chunk = blockSize != 0 ? blockSize : 4096;
	if (chunk > 4096) {
		chunk = 4096;
	}
	char *buffer = malloc(chunk);
	if (buffer == NULL) {
		close(fd);
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot allocate the device probe buffer");
		}
		return NO;
	}
	ssize_t readCount = pread(fd, buffer, chunk, 0);
	if (readCount <= 0) {
		int saved = readCount < 0 ? errno : EIO;
		free(buffer);
		close(fd);
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot read image device data node %@: %s", dataPath, strerror(saved));
		}
		return NO;
	}
	readCount = pread(fd, buffer, 1, status.st_size - 1);
	if (readCount != 1) {
		int saved = readCount < 0 ? errno : EIO;
		free(buffer);
		close(fd);
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot read the last block of image device data node %@: %s", dataPath, strerror(saved));
		}
		return NO;
	}
	free(buffer);
	close(fd);
	return YES;
}
