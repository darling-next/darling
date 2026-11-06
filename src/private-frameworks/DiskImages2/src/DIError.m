#import "DIInternal.h"

#include <stdarg.h>

NSString *const DIErrorDomain = @"com.apple.DiskImages2.ErrorDomain";

NSError *DIErrorMake(NSInteger code, NSString *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	NSString *description = [[NSString alloc] initWithFormat:format arguments:arguments];
	va_end(arguments);

	NSDictionary *userInfo = [NSDictionary dictionaryWithObjectsAndKeys:
		description, NSLocalizedDescriptionKey,
		description, NSLocalizedFailureReasonErrorKey,
		nil];
	NSError *error = [NSError errorWithDomain:DIErrorDomain code:code userInfo:userInfo];
	[description release];
	return error;
}
