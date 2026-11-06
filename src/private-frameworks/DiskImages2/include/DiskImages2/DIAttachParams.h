/*
 * DiskImages2 private framework: DIAttachParams.
 *
 * Only the selectors the preserved DVTDownloads consumer sends are provided
 * (consumer-disassembly attach path): the two initializers, setOnDiskCache:,
 * setReadPassphraseFlags:, setFileMode:, setAutoMount: and
 * setPassphrase:error:. Objective-C type encodings follow the captured
 * native metadata. This provider attaches real read-only image media;
 * shadow images and passphrases are refused with an error because the reader
 * genuinely cannot serve them.
 */

#ifndef DISKIMAGES2_DIATTACHPARAMS_H
#define DISKIMAGES2_DIATTACHPARAMS_H

#import <Foundation/Foundation.h>

@interface DIAttachParams : NSObject {
@private
	BOOL _autoMount;
	BOOL _onDiskCache;
	long long _fileMode;
	NSURL *_URL;
	NSArray *_shadowURLs;
}

- (instancetype)initWithURL:(NSURL *)url error:(NSError **)error;
- (instancetype)initWithURL:(NSURL *)url
	shadowURLs:(NSArray *)shadowURLs
	error:(NSError **)error;

- (NSURL *)URL;
- (NSArray *)shadowURLs;

- (BOOL)autoMount;
- (void)setAutoMount:(BOOL)value;
- (BOOL)onDiskCache;
- (void)setOnDiskCache:(BOOL)value;
- (long long)fileMode;
- (void)setFileMode:(long long)fileMode;

/* Consumer-observed selector (the Swift bridge passes the Int as 64 bits).
 * The flag selects how a passphrase would be read; passphrases are refused,
 * so no passphrase is ever read and the value has no effect. */
- (void)setReadPassphraseFlags:(unsigned long long)flags;

/* Non-NULL passphrases are refused: this provider serves unencrypted image
 * media. NULL means "no passphrase" and succeeds. */
- (BOOL)setPassphrase:(const char *)passphrase error:(NSError **)error;

@end

#endif /* DISKIMAGES2_DIATTACHPARAMS_H */
