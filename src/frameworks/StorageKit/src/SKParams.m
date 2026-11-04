#import "SKInternal.h"

@implementation SKMountParams

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_readOnly = false;
	_noBrowse = false;
	_force = false;
	_recursive = false;
	_raidTraverse = false;
	_mountPoint = nil;
	_allParams = nil;
	return self;
}

- (instancetype)initWithParams:(SKMountParams *)params
{
	self = [self init];
	if (self == nil) {
		return nil;
	}
	if (params != nil) {
		_readOnly = [params readOnly];
		_noBrowse = [params noBrowse];
		_force = [params force];
		_recursive = [params recursive];
		_raidTraverse = [params raidTraverse];
		_mountPoint = [[params mountPoint] copy];
		_allParams = [[params allParams] retain];
	}
	return self;
}

- (void)dealloc
{
	[_mountPoint release];
	[_allParams release];
	[super dealloc];
}

- (bool)readOnly
{
	return _readOnly;
}

- (void)setReadOnly:(bool)value
{
	if (_readOnly == value) {
		return;
	}
	[self willChangeValueForKey:@"readOnly"];
	_readOnly = value;
	[self didChangeValueForKey:@"readOnly"];
}

- (bool)noBrowse
{
	return _noBrowse;
}

- (void)setNoBrowse:(bool)value
{
	_noBrowse = value;
}

- (bool)force
{
	return _force;
}

- (void)setForce:(bool)value
{
	_force = value;
}

- (bool)recursive
{
	return _recursive;
}

- (void)setRecursive:(bool)value
{
	_recursive = value;
}

- (bool)raidTraverse
{
	return _raidTraverse;
}

- (void)setRaidTraverse:(bool)value
{
	_raidTraverse = value;
}

- (NSURL *)mountPoint
{
	return _mountPoint;
}

- (void)setMountPoint:(NSURL *)mountPoint
{
	if (_mountPoint == mountPoint) {
		return;
	}
	NSURL *copy = [mountPoint copy];
	[_mountPoint release];
	_mountPoint = copy;
}

- (NSSet *)allParams
{
	return _allParams;
}

- (void)setAllParams:(NSSet *)allParams
{
	if (_allParams == allParams) {
		return;
	}
	[allParams retain];
	[_allParams release];
	_allParams = allParams;
}

- (id)copyWithZone:(NSZone *)zone
{
	SKMountParams *copy = [[SKMountParams allocWithZone:zone] init];
	if (copy != nil) {
		[copy setReadOnly:_readOnly];
		[copy setNoBrowse:_noBrowse];
		[copy setForce:_force];
		[copy setRecursive:_recursive];
		[copy setRaidTraverse:_raidTraverse];
		[copy setMountPoint:_mountPoint];
		[copy setAllParams:_allParams];
	}
	return copy;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<SKMountParams readOnly=%d noBrowse=%d force=%d recursive=%d mountPoint=%@>",
		_readOnly ? 1 : 0, _noBrowse ? 1 : 0, _force ? 1 : 0, _recursive ? 1 : 0, _mountPoint];
}

@end

static void *SKMountParamsReadOnlyContext = &SKMountParamsReadOnlyContext;

@implementation SKDiskImageAttachParams

- (instancetype)init
{
	SKMountParams *params = [[SKMountParams alloc] init];
	self = [self initWithParams:params];
	[params release];
	return self;
}

- (instancetype)initWithParams:(SKMountParams *)params
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_mountParams = [params retain];
	_policy = 2;
	_readOnly = params != nil ? [params readOnly] : false;
	_stdinPassPhrase = false;
	_isManagedAttach = false;
	_is4KBlockSize = false;
	_emulateExternalDisk = false;
	if (params != nil) {
		[params addObserver:self
			forKeyPath:@"readOnly"
			options:NSKeyValueObservingOptionNew
			context:SKMountParamsReadOnlyContext];
	}
	return self;
}

- (void)dealloc
{
	if (_mountParams != nil) {
		[_mountParams removeObserver:self forKeyPath:@"readOnly" context:SKMountParamsReadOnlyContext];
	}
	[_mountParams release];
	[super dealloc];
}

- (void)observeValueForKeyPath:(NSString *)keyPath
	ofObject:(id)object
	change:(NSDictionary *)change
	context:(void *)context
{
	if (context == SKMountParamsReadOnlyContext && object == _mountParams && [keyPath isEqualToString:@"readOnly"]) {
		_readOnly = _mountParams != nil ? [_mountParams readOnly] : false;
		return;
	}
	[super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
}

- (SKMountParams *)mountParams
{
	return _mountParams;
}

- (void)setMountParams:(SKMountParams *)mountParams
{
	if (_mountParams == mountParams) {
		return;
	}
	if (_mountParams != nil) {
		[_mountParams removeObserver:self forKeyPath:@"readOnly" context:SKMountParamsReadOnlyContext];
		[_mountParams release];
	}
	_mountParams = [mountParams copy];
	_readOnly = _mountParams != nil ? [_mountParams readOnly] : false;
	_stdinPassPhrase = false;
	_isManagedAttach = false;
	_is4KBlockSize = false;
	_emulateExternalDisk = false;
	if (_mountParams != nil) {
		[_mountParams addObserver:self
			forKeyPath:@"readOnly"
			options:NSKeyValueObservingOptionNew
			context:SKMountParamsReadOnlyContext];
	}
}

- (bool)readOnly
{
	return _readOnly;
}

- (void)setReadOnly:(bool)value
{
	_readOnly = value;
}

- (bool)stdinPassPhrase
{
	return _stdinPassPhrase;
}

- (void)setStdinPassPhrase:(bool)value
{
	_stdinPassPhrase = value;
}

- (bool)isManagedAttach
{
	return _isManagedAttach;
}

- (void)setIsManagedAttach:(bool)value
{
	_isManagedAttach = value;
}

- (bool)is4KBlockSize
{
	return _is4KBlockSize;
}

- (void)setIs4KBlockSize:(bool)value
{
	_is4KBlockSize = value;
}

- (bool)emulateExternalDisk
{
	return _emulateExternalDisk;
}

- (void)setEmulateExternalDisk:(bool)value
{
	_emulateExternalDisk = value;
}

- (long long)policy
{
	return _policy;
}

- (void)setPolicy:(long long)policy
{
	_policy = policy;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<SKDiskImageAttachParams managed=%d readOnly=%d policy=%lld mountParams=%@>",
		_isManagedAttach ? 1 : 0, _readOnly ? 1 : 0, _policy, _mountParams];
}

@end
