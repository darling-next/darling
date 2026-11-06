#import "DIInternal.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

@implementation DIAttachParams

- (id)init
{
	self = [super init];
	if (self != nil) {
		/* Native defaults: fileMode 1 (the default writable request) and
		 * autoMount on. This provider always produces read-only media. */
		_fileMode = 1;
		_autoMount = YES;
		_onDiskCache = NO;
	}
	return self;
}

- (void)dealloc
{
	[_URL release];
	[_shadowURLs release];
	[super dealloc];
}

static BOOL DIValidateImageURL(NSURL *url, NSError **error)
{
	if (![url isKindOfClass:[NSURL class]] || ![url isFileURL] || [[url path] length] == 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"No image file URL was supplied");
		}
		return NO;
	}
	struct stat status;
	if (stat([[url path] fileSystemRepresentation], &status) != 0) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Cannot access image %@: %s", [url path], strerror(errno));
		}
		return NO;
	}
	if (!S_ISREG(status.st_mode)) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Image %@ is not a regular file", [url path]);
		}
		return NO;
	}
	return YES;
}

- (instancetype)initWithURL:(NSURL *)url error:(NSError **)error
{
	return [self initWithURL:url shadowURLs:nil error:error];
}

- (instancetype)initWithURL:(NSURL *)url
	shadowURLs:(NSArray *)shadowURLs
	error:(NSError **)error
{
	if (!DIValidateImageURL(url, error)) {
		[self release];
		return nil;
	}
	if (shadowURLs != nil && ![shadowURLs isKindOfClass:[NSArray class]]) {
		if (error != NULL) {
			*error = DIErrorMake(DIErrorCodeGeneric, @"Shadow URLs must be an array of file URLs");
		}
		[self release];
		return nil;
	}
	for (id shadow in shadowURLs) {
		if (![shadow isKindOfClass:[NSURL class]] || ![shadow isFileURL]) {
			if (error != NULL) {
				*error = DIErrorMake(DIErrorCodeGeneric, @"A shadow URL is not a file URL");
			}
			[self release];
			return nil;
		}
	}
	self = [self init];
	if (self == nil) {
		return nil;
	}
	_URL = [url copy];
	_shadowURLs = [shadowURLs copy];
	return self;
}

- (NSURL *)URL { return _URL; }
- (NSArray *)shadowURLs { return _shadowURLs; }

- (BOOL)autoMount { return _autoMount; }
- (void)setAutoMount:(BOOL)value { _autoMount = value; }
- (BOOL)onDiskCache { return _onDiskCache; }
- (void)setOnDiskCache:(BOOL)value { _onDiskCache = value; }
- (long long)fileMode { return _fileMode; }
- (void)setFileMode:(long long)fileMode { _fileMode = fileMode; }

- (void)setReadPassphraseFlags:(unsigned long long)flags
{
	/* No passphrase is ever read; see -setPassphrase:error:. */
	(void)flags;
}

- (BOOL)setPassphrase:(const char *)passphrase error:(NSError **)error
{
	if (passphrase == NULL) {
		return YES;
	}
	if (error != NULL) {
		*error = DIErrorMake(DIErrorCodeGeneric, @"Encrypted image media is not supported by this image provider");
	}
	return NO;
}

@end
