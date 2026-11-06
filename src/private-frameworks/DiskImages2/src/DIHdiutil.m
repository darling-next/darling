#import "DIInternal.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define DI_HDIUTIL_OUTPUT_LIMIT (8u * 1024u * 1024u)

static NSString *DIReadFileString(NSString *path)
{
	int fd = open([path fileSystemRepresentation], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return nil;
	}
	NSMutableData *data = [NSMutableData data];
	char buffer[16384];
	for (;;) {
		ssize_t count = read(fd, buffer, sizeof(buffer));
		if (count < 0) {
			if (errno == EINTR) {
				continue;
			}
			close(fd);
			return nil;
		}
		if (count == 0) {
			break;
		}
		if ([data length] + (NSUInteger)count > DI_HDIUTIL_OUTPUT_LIMIT) {
			close(fd);
			return nil;
		}
		[data appendBytes:buffer length:(NSUInteger)count];
	}
	close(fd);
	NSString *string = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
	return [string autorelease];
}

static NSString *DIHdiutilErrorText(NSString *stderrText)
{
	NSString *trimmed = [stderrText stringByTrimmingCharactersInSet:
		[NSCharacterSet whitespaceAndNewlineCharacterSet]];
	if ([trimmed length] == 0) {
		return @"no diagnostics";
	}
	return trimmed;
}

static void DIStoreError(NSError **slot, NSError *error)
{
	if (slot != NULL) {
		*slot = error;
	}
}

BOOL DIHdiutilRun(NSArray *arguments,
	NSTimeInterval timeout,
	NSString **output,
	NSError **error)
{
	if (output != NULL) {
		*output = nil;
	}
	if (arguments == nil) {
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"No hdiutil arguments were supplied"));
		return NO;
	}

	char outPath[] = "/tmp/diskimages2-hdiutil-out-XXXXXX";
	int outFD = mkstemp(outPath);
	if (outFD < 0) {
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"Cannot create the hdiutil output file: %s", strerror(errno)));
		return NO;
	}
	close(outFD);
	char errPath[] = "/tmp/diskimages2-hdiutil-err-XXXXXX";
	int errFD = mkstemp(errPath);
	if (errFD < 0) {
		int saved = errno;
		unlink(outPath);
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"Cannot create the hdiutil diagnostics file: %s", strerror(saved)));
		return NO;
	}
	close(errFD);

	NSUInteger count = [arguments count];
	char **argv = calloc(count + 2, sizeof(char *));
	if (argv == NULL) {
		unlink(outPath);
		unlink(errPath);
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"Cannot allocate hdiutil arguments"));
		return NO;
	}
	argv[0] = (char *)"/usr/bin/hdiutil";
	for (NSUInteger index = 0; index < count; index++) {
		NSString *argument = [arguments objectAtIndex:index];
		argv[index + 1] = (char *)[argument fileSystemRepresentation];
	}
	argv[count + 1] = NULL;

	posix_spawn_file_actions_t actions;
	int actionError = posix_spawn_file_actions_init(&actions);
	BOOL actionsInitialized = actionError == 0;
	if (actionError == 0) {
		actionError = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO,
			"/dev/null", O_RDONLY, 0);
	}
	if (actionError == 0) {
		actionError = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO,
			outPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	}
	if (actionError == 0) {
		actionError = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
			errPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	}

	pid_t pid = 0;
	int spawnError = -1;
	if (actionError == 0) {
		spawnError = posix_spawn(&pid, "/usr/bin/hdiutil", &actions, NULL, argv, environ);
	}
	if (actionsInitialized) {
		posix_spawn_file_actions_destroy(&actions);
	}
	free(argv);

	if (actionError != 0 || spawnError != 0) {
		int saved = actionError != 0 ? actionError : spawnError;
		unlink(outPath);
		unlink(errPath);
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"Cannot start hdiutil: %s", strerror(saved)));
		return NO;
	}

	struct timeval start;
	gettimeofday(&start, NULL);
	BOOL exited = NO;
	int status = 0;
	for (;;) {
		pid_t waited = waitpid(pid, &status, WNOHANG);
		if (waited == pid) {
			exited = YES;
			break;
		}
		if (waited < 0) {
			if (errno == EINTR) {
				continue;
			}
			int saved = errno;
			unlink(outPath);
			unlink(errPath);
			DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"Cannot wait for hdiutil: %s", strerror(saved)));
			return NO;
		}
		struct timeval now;
		gettimeofday(&now, NULL);
		double elapsed = (double)(now.tv_sec - start.tv_sec) +
			(double)(now.tv_usec - start.tv_usec) / 1000000.0;
		if (elapsed >= timeout) {
			break;
		}
		struct timespec pause = { 0, 20 * 1000 * 1000 };
		nanosleep(&pause, NULL);
	}

	if (!exited) {
		/* The child is ours; kill and reap it so no hdiutil keeps running
		 * without an owner. */
		kill(pid, SIGKILL);
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		}
		unlink(outPath);
		unlink(errPath);
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"hdiutil did not finish within %.0f seconds", timeout));
		return NO;
	}

	NSString *stdoutText = DIReadFileString([NSString stringWithUTF8String:outPath]);
	NSString *stderrText = DIReadFileString([NSString stringWithUTF8String:errPath]);
	unlink(outPath);
	unlink(errPath);

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		NSString *reason;
		if (WIFEXITED(status)) {
			reason = [NSString stringWithFormat:@"hdiutil exited with status %d: %@",
				WEXITSTATUS(status), DIHdiutilErrorText(stderrText)];
		} else if (WIFSIGNALED(status)) {
			reason = [NSString stringWithFormat:@"hdiutil was terminated by signal %d: %@",
				WTERMSIG(status), DIHdiutilErrorText(stderrText)];
		} else {
			reason = [NSString stringWithFormat:@"hdiutil did not exit normally: %@",
				DIHdiutilErrorText(stderrText)];
		}
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"%@", reason));
		return NO;
	}

	if (stdoutText == nil) {
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"hdiutil reported success but its output could not be read"));
		return NO;
	}
	if (output != NULL) {
		*output = stdoutText;
	}
	return YES;
}

static id DIPropertyListFromText(NSString *text, NSError **error)
{
	if (text == nil) {
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"hdiutil produced no output"));
		return nil;
	}
	NSData *data = [text dataUsingEncoding:NSUTF8StringEncoding];
	if (data == nil) {
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"hdiutil output is not UTF-8"));
		return nil;
	}
	NSError *parseError = nil;
	id plist = [NSPropertyListSerialization propertyListWithData:data
		options:NSPropertyListImmutable
		format:NULL
		error:&parseError];
	if (plist == nil) {
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"hdiutil output is not a property list: %@", parseError != nil ? [parseError localizedDescription] : @"unknown parse error"));
		return nil;
	}
	return plist;
}

NSString *DIHdiutilAttachNoMount(NSString *imagePath, NSError **error)
{
	if (imagePath == nil || [imagePath length] == 0) {
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"No image URL was supplied"));
		return nil;
	}
	NSString *output = nil;
	NSError *runError = nil;
	NSArray *arguments = [NSArray arrayWithObjects:
		@"attach", @"-nomount", @"-nobrowse", @"-plist", imagePath, nil];
	if (!DIHdiutilRun(arguments, 120.0, &output, &runError)) {
		DIStoreError(error, runError);
		return nil;
	}
	NSError *parseError = nil;
	id plist = DIPropertyListFromText(output, &parseError);
	if (![plist isKindOfClass:[NSDictionary class]]) {
		DIStoreError(error, parseError != nil ? parseError : DIErrorMake(DIErrorCodeGeneric, @"hdiutil attach returned an unexpected property list"));
		return nil;
	}
	NSArray *entities = [plist objectForKey:@"system-entities"];
	if (![entities isKindOfClass:[NSArray class]]) {
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"hdiutil attach returned no system entities"));
		return nil;
	}
	id identity = nil;
	for (NSDictionary *entity in entities) {
		if (![entity isKindOfClass:[NSDictionary class]]) {
			continue;
		}
		NSString *candidate = [entity objectForKey:@"darling-image-id"];
		if (![candidate isKindOfClass:[NSString class]] || [candidate length] == 0) {
			continue;
		}
		identity = candidate;
		break;
	}
	if (identity == nil || [identity length] == 0) {
		DIStoreError(error, DIErrorMake(DIErrorCodeGeneric, @"hdiutil attach did not identify the image producer"));
		return nil;
	}
	return identity;
}

NSDictionary *DIHdiutilInfoPlist(NSError **error)
{
	NSString *output = nil;
	NSError *runError = nil;
	if (!DIHdiutilRun([NSArray arrayWithObjects:@"info", @"-plist", nil],
		20.0, &output, &runError)) {
		DIStoreError(error, runError);
		return nil;
	}
	NSError *parseError = nil;
	id plist = DIPropertyListFromText(output, &parseError);
	if (![plist isKindOfClass:[NSDictionary class]] ||
		![[plist objectForKey:@"images"] isKindOfClass:[NSArray class]]) {
		DIStoreError(error, parseError != nil ? parseError : DIErrorMake(DIErrorCodeGeneric, @"hdiutil info returned no image list"));
		return nil;
	}
	return plist;
}

static BOOL DIImageMatches(NSDictionary *image, NSString *identity, NSString *BSDName)
{
	if (identity != nil) {
		NSString *candidate = [image objectForKey:@"darling-image-id"];
		if ([candidate isKindOfClass:[NSString class]]) {
			NSRange colon = [candidate rangeOfString:@":"];
			NSString *base = colon.location == NSNotFound ? candidate :
				[candidate substringToIndex:colon.location];
			if ([base isEqualToString:identity]) {
				return YES;
			}
		}
	}
	if (BSDName != nil) {
		NSArray *entities = [image objectForKey:@"system-entities"];
		if ([entities isKindOfClass:[NSArray class]]) {
			for (NSDictionary *entity in entities) {
				if (![entity isKindOfClass:[NSDictionary class]]) {
					continue;
				}
				NSString *name = [entity objectForKey:@"bsd-name"];
				if ([name isKindOfClass:[NSString class]] && [name isEqualToString:BSDName]) {
					return YES;
				}
			}
		}
	}
	return NO;
}

NSString *DIProducerImagePath(NSString *identity, NSString *BSDName)
{
	NSDictionary *plist = DIHdiutilInfoPlist(NULL);
	if (plist == nil) {
		return nil;
	}
	NSArray *images = [plist objectForKey:@"images"];
	for (NSDictionary *image in images) {
		if (![image isKindOfClass:[NSDictionary class]] || !DIImageMatches(image, identity, BSDName)) {
			continue;
		}
		NSString *path = [image objectForKey:@"image-path"];
		if ([path isKindOfClass:[NSString class]] && [path length] > 0) {
			return path;
		}
	}
	return nil;
}
