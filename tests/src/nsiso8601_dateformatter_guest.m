#import <Foundation/Foundation.h>
#include <stdio.h>

/*
 * Public consumer oracle for NSISO8601DateFormatter.
 *
 * The expectations come from Apple's documented ISO 8601 option semantics and
 * from swift-corelibs-foundation's reference ISO8601DateFormatter suite (the
 * formatter runs the same CFDateFormatter facade). They cover the RFC 3339
 * default, each option family, the ISO week calendar (Monday first weekday,
 * week 1 is the first week with at least four days) across a week-year
 * boundary, real instants and offsets through the parser, property changes
 * after the formatter cache is warm, and copy and keyed-coding isolation.
 *
 * Deliberately not pinned: acceptance of trailing input, lenient parsing, or
 * the exact pattern used for an option set with no date and no time.
 */

static int failures;

static void check(BOOL condition, const char *label)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL NSISO8601DateFormatter: %s\n", label);
        failures++;
    }
}

static void expectString(NSString *actual, NSString *expected, const char *label)
{
    if (actual == nil || ![actual isEqualToString:expected])
    {
        const char *got = actual != nil ? [actual UTF8String] : "(nil)";
        fprintf(stderr, "FAIL NSISO8601DateFormatter: %s (expected %s, got %s)\n",
                label, [expected UTF8String], got);
        failures++;
    }
}


int main(void)
{
    @autoreleasepool
    {
    /* 2016-10-08T22:31:00Z, the reference instant used by the corelibs suite. */
    NSDate *instant = [NSDate dateWithTimeIntervalSince1970:1475965860.0];
    /* The same instant plus 500 ms; .5 is exactly representable. */
    NSDate *fractionalInstant = [NSDate dateWithTimeIntervalSince1970:1475965860.5];
    /* 2016-01-01T00:00:00Z: ISO week 2015-W53-05. */
    NSDate *weekBoundary = [NSDate dateWithTimeIntervalSince1970:1451606400.0];
    /* 2019-12-30T00:00:00Z: ISO week 2020-W01-01. */
    NSDate *nextWeekBoundary = [NSDate dateWithTimeIntervalSince1970:1577664000.0];
    NSTimeZone *losAngeles = [NSTimeZone timeZoneWithName:@"America/Los_Angeles"];
    NSISO8601DateFormatter *formatter = [[NSISO8601DateFormatter alloc] init];

    check(losAngeles != nil, "fixture time zone resolves");

    /* Default: GMT, internet date time. */
    expectString([formatter stringFromDate:instant], @"2016-10-08T22:31:00Z", "default internet date time");

    /* Option families, mirroring the corelibs reference expectations. */
    [formatter setFormatOptions:(NSISO8601DateFormatWithInternetDateTime |
                                 NSISO8601DateFormatWithSpaceBetweenDateAndTime)];
    expectString([formatter stringFromDate:instant], @"2016-10-08 22:31:00Z", "space separator");
    [formatter setFormatOptions:NSISO8601DateFormatWithFullTime];
    expectString([formatter stringFromDate:instant], @"22:31:00Z", "full time");
    [formatter setFormatOptions:NSISO8601DateFormatWithFullDate];
    expectString([formatter stringFromDate:instant], @"2016-10-08", "full date");
    [formatter setFormatOptions:(NSISO8601DateFormatWithDay | NSISO8601DateFormatWithTime)];
    expectString([formatter stringFromDate:instant], @"282T223100", "ordinal day");
    [formatter setFormatOptions:(NSISO8601DateFormatWithWeekOfYear | NSISO8601DateFormatWithTime)];
    expectString([formatter stringFromDate:instant], @"W40T223100", "week of year");
    [formatter setFormatOptions:(NSISO8601DateFormatWithDay |
                                 NSISO8601DateFormatWithWeekOfYear |
                                 NSISO8601DateFormatWithTime)];
    expectString([formatter stringFromDate:instant], @"W4006T223100", "week day");
    [formatter setFormatOptions:(NSISO8601DateFormatWithMonth |
                                 NSISO8601DateFormatWithDay |
                                 NSISO8601DateFormatWithWeekOfYear |
                                 NSISO8601DateFormatWithDashSeparatorInDate)];
    expectString([formatter stringFromDate:instant], @"10-W40-06", "month week day with dashes");

    /* Fractional seconds are three significant digits. */
    [formatter setFormatOptions:(NSISO8601DateFormatWithInternetDateTime |
                                 NSISO8601DateFormatWithFractionalSeconds)];
    expectString([formatter stringFromDate:fractionalInstant], @"2016-10-08T22:31:00.500Z",
                 "internet date time with fractional seconds");

    /* ISO week calendar: the week year, not the calendar year. */
    [formatter setFormatOptions:(NSISO8601DateFormatWithYear |
                                 NSISO8601DateFormatWithWeekOfYear |
                                 NSISO8601DateFormatWithDay |
                                 NSISO8601DateFormatWithDashSeparatorInDate)];
    expectString([formatter stringFromDate:weekBoundary], @"2015-W53-05", "week year at January 1");
    expectString([formatter stringFromDate:nextWeekBoundary], @"2020-W01-01",
                 "week year at December 30");

    /* A time zone change after the formatter cache is warm. */
    [formatter setFormatOptions:NSISO8601DateFormatWithInternetDateTime];
    expectString([formatter stringFromDate:instant], @"2016-10-08T22:31:00Z", "warm cache default");
    [formatter setTimeZone:losAngeles];
    expectString([formatter stringFromDate:instant], @"2016-10-08T15:31:00-07:00",
                 "warmed cache time zone change");
    [formatter setFormatOptions:(NSISO8601DateFormatWithTime | NSISO8601DateFormatWithTimeZone)];
    expectString([formatter stringFromDate:instant], @"153100-0700", "basic offset without separators");
    [formatter setTimeZone:nil];
    expectString([formatter stringFromDate:instant], @"223100Z", "basic offset reset to GMT");

    /* Class method builds the same public provider. */
    expectString([NSISO8601DateFormatter stringFromDate:instant
                                               timeZone:losAngeles
                                          formatOptions:NSISO8601DateFormatWithFullDate],
                 @"2016-10-08", "class method full date");
    expectString([NSISO8601DateFormatter stringFromDate:instant
                                               timeZone:losAngeles
                                          formatOptions:NSISO8601DateFormatWithInternetDateTime],
                 @"2016-10-08T15:31:00-07:00", "class method internet date time");

    /* Parser: absolute instants, explicit offsets and fractional seconds. */
    [formatter setFormatOptions:NSISO8601DateFormatWithInternetDateTime];
    NSDate *parsed = [formatter dateFromString:@"2016-10-08T22:31:00Z"];
    check(parsed != nil && [parsed isEqualToDate:instant], "parse internet date time");
    parsed = [formatter dateFromString:@"2016-10-08T15:31:00-07:00"];
    check(parsed != nil && [parsed isEqualToDate:instant], "parse explicit offset");
    check([formatter dateFromString:@"not an iso date"] == nil, "reject non-ISO text");

    [formatter setFormatOptions:(NSISO8601DateFormatWithInternetDateTime |
                                 NSISO8601DateFormatWithFractionalSeconds)];
    parsed = [formatter dateFromString:@"2016-10-08T22:31:00.500Z"];
    check(parsed != nil && [parsed timeIntervalSince1970] == 1475965860.5,
          "parse fractional seconds");

    /* NSFormatter conversion contract. */
    [formatter setFormatOptions:NSISO8601DateFormatWithInternetDateTime];
    id converted = nil;
    NSString *description = nil;
    check([formatter getObjectValue:&converted
                          forString:@"2016-10-08T22:31:00Z"
                   errorDescription:&description],
          "formatter gets object value");
    check([converted isKindOfClass:[NSDate class]] && [converted isEqualToDate:instant],
          "formatter conversion returns the instant");
    converted = nil;
    description = nil;
    check(![formatter getObjectValue:&converted
                           forString:@"not an iso date"
                    errorDescription:&description],
          "formatter rejects invalid string");
    check(converted == nil && description != nil, "formatter reports conversion failure");


    /* Copy isolation. */
    [formatter setTimeZone:losAngeles];
    [formatter setFormatOptions:(NSISO8601DateFormatWithInternetDateTime |
                                 NSISO8601DateFormatWithFractionalSeconds)];
    NSISO8601DateFormatter *copy = [formatter copy];
    expectString([copy stringFromDate:fractionalInstant], @"2016-10-08T15:31:00.500-07:00",
                 "copy preserves formatting state");
    [copy setTimeZone:nil];
    [copy setFormatOptions:NSISO8601DateFormatWithFullDate];
    expectString([copy stringFromDate:instant], @"2016-10-08", "copy reformatted");
    expectString([formatter stringFromDate:fractionalInstant], @"2016-10-08T15:31:00.500-07:00",
                 "original unaffected by copy changes");
    [copy release];

    /* Real keyed archive/unarchive, including the nested timezone provider. */
    NSData *archive = [NSKeyedArchiver archivedDataWithRootObject:formatter];
    NSISO8601DateFormatter *restored = [NSKeyedUnarchiver unarchiveObjectWithData:archive];
    expectString([restored stringFromDate:fractionalInstant], @"2016-10-08T15:31:00.500-07:00",
                 "archive preserves formatting state");
    [restored setTimeZone:nil];
    [restored setFormatOptions:NSISO8601DateFormatWithFullDate];
    expectString([restored stringFromDate:instant], @"2016-10-08", "decoded formatter reformatted");
    expectString([formatter stringFromDate:fractionalInstant], @"2016-10-08T15:31:00.500-07:00",
                 "original unaffected by decoded formatter changes");

    /* A real non-secure unarchiver must not turn a wrong-type timezone into
     * an unrelated setter exception or a successful default formatter. */
    NSMutableData *corruptArchive = [NSMutableData data];
    NSKeyedArchiver *encoder = [[NSKeyedArchiver alloc] initForWritingWithMutableData:corruptArchive];
    [encoder encodeInteger:NSISO8601DateFormatWithInternetDateTime forKey:@"NS.formatOptions"];
    [encoder encodeObject:[NSNull null] forKey:@"NS.timeZone"];
    [encoder finishEncoding];
    [encoder release];
    NSKeyedUnarchiver *decoder = [[NSKeyedUnarchiver alloc] initForReadingWithData:corruptArchive];
    NSISO8601DateFormatter *corrupt = [[NSISO8601DateFormatter alloc] initWithCoder:decoder];
    check(corrupt == nil, "wrong-type archived timezone rejects the formatter");
    [corrupt release];
    [decoder finishDecoding];
    [decoder release];

    [formatter release];
    }

    if (failures != 0)
    {
        return 1;
    }
    puts("NSISO8601_DATEFORMATTER_PASS=1");
    return 0;
}
