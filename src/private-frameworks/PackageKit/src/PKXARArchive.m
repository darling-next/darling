/*
This file is part of Darling.

Copyright (C) 2017 Lubos Dolezel

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

#import <PackageKit/PackageKit.h>
#import <PackageKit/PKXARArchive.h>
#import <xar/xar.h>

#include <stdlib.h>
#include <string.h>

/* The same lookup the native reader performs: compare xar_get_path() to the
 * requested subpath.  An empty subpath names no member. */
static xar_file_t PKXARArchiveFileForSubpath (xar_t xar, NSString *subpath)
{
    const char *target;
    xar_iter_t iterator;
    xar_file_t file;

    if (xar == NULL || [subpath length] == 0)
        return NULL;

    target = [subpath fileSystemRepresentation];
    if (target == NULL)
        return NULL;

    iterator = xar_iter_new();
    if (iterator == NULL)
        return NULL;

    file = xar_file_first(xar, iterator);
    while (file != NULL) {
        char *filePath = xar_get_path(file);

        if (filePath != NULL) {
            int matches = (strcmp(filePath, target) == 0);

            free(filePath);
            if (matches)
                break;
        }

        file = xar_file_next(iterator);
    }

    xar_iter_free(iterator);
    return file;
}

@implementation PKXARArchive
{
    NSString *_path;
    NSData *_tocDigest;
    xar_t _xar;
    BOOL _skipsVerifyIfUnsigned;
}

- (instancetype) initForReadingFromPath: (NSString *) path
{
    return [self initForReadingFromPath: path options: nil];
}

- (instancetype) initForReadingFromPath: (NSString *) path options: (NSDictionary *) options
{
    self = [super init];
    if (self == nil)
        return nil;

    _path = [path copy];

    id expectedDigest = [options objectForKey: PKArchiveOpenOptionValidatedDigest];
    if (expectedDigest != nil)
        _tocDigest = expectedDigest;

    if ([self _xar] == NULL)
        return nil;

    return self;
}

- (xar_t) _xar
{
    const char *fileSystemPath;

    if (_xar != NULL)
        return _xar;
    if (_path == nil)
        return NULL;

    fileSystemPath = [_path fileSystemRepresentation];
    if (fileSystemPath == NULL)
        return NULL;

    if (_tocDigest != nil) {
        _xar = xar_open_digest_verify(fileSystemPath, 0,
                                      (void *) [_tocDigest bytes], [_tocDigest length]);
    } else {
        _xar = xar_open(fileSystemPath, 0);
    }

    /* Remember the table-of-contents digest so it survives closeArchive. */
    if (_xar != NULL && _tocDigest == nil) {
        size_t size = 0;
        void *checksum = xar_get_toc_checksum(_xar, &size);

        if (checksum != NULL) {
            if (size != 0)
                _tocDigest = [NSData dataWithBytesNoCopy: checksum
                                                  length: size
                                            freeWhenDone: YES];
            else
                free(checksum);
        }
    }

    return _xar;
}

- (NSString *) archivePath
{
    return _path;
}

- (NSData *) archiveDigestData
{
    return [_tocDigest copy];
}

- (BOOL) closeArchive
{
    int result;

    if (_xar == NULL)
        return YES;

    result = xar_close(_xar);
    _xar = NULL;

    return result == 0;
}

- (void) _setSkipsVerifyIfUnsigned: (BOOL) skip
{
    _skipsVerifyIfUnsigned = skip;
}

- (BOOL) _xarFileIsValid: (xar_file_t) file
{
    xar_t xar = [self _xar];

    if (xar == NULL)
        return NO;

    /* Signature presence, not signature trust: an unsigned archive has no
     * member signatures to check when the caller allows skipping. */
    if (_skipsVerifyIfUnsigned && xar_signature_first(xar) == NULL)
        return YES;

    return xar_verify(xar, file) == 0;
}

- (NSData *) dataForPath: (NSString *) path
{
    xar_t xar = [self _xar];
    xar_file_t file;
    char *buffer = NULL;
    size_t size = 0;

    if (xar == NULL)
        return nil;

    file = PKXARArchiveFileForSubpath(xar, path);
    if (file == NULL)
        return nil;
    if (![self _xarFileIsValid: file])
        return nil;

    if (xar_extract_tobuffersz(xar, file, &buffer, &size) != 0)
        return nil;

    return [NSData dataWithBytesNoCopy: buffer length: size freeWhenDone: YES];
}

- (BOOL) fileExistsAtPath: (NSString *) path
{
    xar_t xar = [self _xar];

    if (xar == NULL)
        return NO;

    return PKXARArchiveFileForSubpath(xar, path) != NULL;
}

- (void) dealloc
{
    [self closeArchive];
}

@end
