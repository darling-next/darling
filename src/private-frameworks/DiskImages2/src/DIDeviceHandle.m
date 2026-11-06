#import "DIInternal.h"

@implementation DIDeviceHandle

- (instancetype)initWithRegEntryID:(unsigned long long)regEntryID
{
	self = [super init];
	if (self != nil) {
		_regEntryID = regEntryID;
	}
	return self;
}

- (void)dealloc
{
	[_BSDName release];
	[_di_imageURL release];
	[_di_producerIdentity release];
	[super dealloc];
}

- (NSString *)BSDName { return _BSDName; }
- (void)setBSDName:(NSString *)BSDName
{
	if (_BSDName != BSDName) {
		[_BSDName release];
		_BSDName = [BSDName copy];
	}
}

- (unsigned long long)regEntryID { return _regEntryID; }

- (void)di_setProducerIdentity:(NSString *)identity imageURL:(NSURL *)imageURL
{
	NSString *identityCopy = [identity copy];
	NSURL *imageURLCopy = [imageURL copy];

	[_di_producerIdentity release];
	_di_producerIdentity = identityCopy;
	[_di_imageURL release];
	_di_imageURL = imageURLCopy;
}

- (NSString *)di_producerIdentity { return _di_producerIdentity; }
- (NSURL *)di_imageURL { return _di_imageURL; }


@end
