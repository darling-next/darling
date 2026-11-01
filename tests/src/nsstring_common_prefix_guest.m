#import <Foundation/Foundation.h>
#include <stdio.h>

/*
 * Public consumer oracle for -[NSString commonPrefixWithString:options:].
 *
 * Apple documents only NSCaseInsensitiveSearch and NSLiteralSearch for this
 * method: without NSLiteralSearch comparison is canonical-equivalent, and the
 * result is a prefix of the receiver in the receiver's own spelling. Every
 * expectation below is an exact UTF-16 prefix, so a canonical-equality answer
 * in the other string's spelling, a partial composed character or a shared
 * half of a multi-unit character fails.
 */

#define REQUIRE(condition, label) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL nsstring common prefix: %s\n", label); \
        return 1; \
    } \
} while (0)

static NSString *units(const unichar *characters, NSUInteger length)
{
    return [NSString stringWithCharacters:characters length:length];
}

static BOOL equalsUnits(NSString *string, const unichar *characters, NSUInteger length)
{
    if ([string length] != length) {
        return NO;
    }
    for (NSUInteger index = 0; index < length; index++) {
        if ([string characterAtIndex:index] != characters[index]) {
            return NO;
        }
    }
    return YES;
}

int main(void)
{
    /* U+00E4 LATIN SMALL LETTER A WITH DIAERESIS. */
    static const unichar decomposedMaedchen[] = {
        'M', 'a', 0x0308, 'd', 'c', 'h', 'e', 'n', 's', 'c', 'h', 'u', 'l', 'e'
    };
    static const unichar precomposedMaedchen[] = {
        'M', 0x00E4, 'd', 'c', 'h', 'e', 'n'
    };
    static const unichar decomposedMaedchenPrefix[] = {
        'M', 'a', 0x0308, 'd', 'c', 'h', 'e', 'n'
    };
    /* U+1F600 GRINNING FACE and U+1F603 SMILING FACE WITH OPEN MOUTH. */
    static const unichar grinningReceiver[] = {
        0xD83D, 0xDE00, 's', 'm', 'i', 'l', 'e', 0xD83D, 0xDE00
    };
    static const unichar grinningOther[] = {
        0xD83D, 0xDE00, 's', 'm', 'i', 'l', 'e', 0xD83D, 0xDE03
    };
    static const unichar grinningPrefix[] = {
        0xD83D, 0xDE00, 's', 'm', 'i', 'l', 'e'
    };
    /* U+1F1FA U+1F1F8 (US flag) and U+1F1FA U+1F1F3 (UN flag). */
    static const unichar usFlag[] = { 0xD83C, 0xDDFA, 0xD83C, 0xDDF8 };
    static const unichar unFlag[] = { 0xD83C, 0xDDFA, 0xD83C, 0xDDF3 };
    static const unichar crlfPrefix[] = { 'a', '\r', '\n' };
    static const unichar uUmlaut[] = { 0x00FC };

    @autoreleasepool {
        REQUIRE([[@"" commonPrefixWithString:@"abc" options:0] isEqualToString:@""],
                "empty receiver");
        REQUIRE([[@"abc" commonPrefixWithString:@"" options:0] isEqualToString:@""],
                "empty argument");
        REQUIRE([[@"alpha" commonPrefixWithString:@"beta" options:0] isEqualToString:@""],
                "no common prefix");
        REQUIRE([[@"abc" commonPrefixWithString:@"axc" options:0] isEqualToString:@"a"],
                "one-character prefix");

        /* A shorter argument that is a full prefix of the receiver, with the
         * receiver's capitalisation preserved under case-insensitive search. */
        REQUIRE([[@"/Srv/Artifacts/Report.txt"
                  commonPrefixWithString:@"/srv/artifacts/report"
                                options:NSCaseInsensitiveSearch]
                 isEqualToString:@"/Srv/Artifacts/Report"],
                "file prefix keeps receiver spelling and full shorter string");

        NSString *mixedCase = @"LogArchive-2026";
        NSString *lowerCase = @"logarchive-2026";
        REQUIRE([[mixedCase commonPrefixWithString:lowerCase options:0] isEqualToString:@""],
                "case-sensitive search does not match case");
        REQUIRE([[mixedCase commonPrefixWithString:lowerCase
                                          options:NSCaseInsensitiveSearch]
                 isEqualToString:mixedCase],
                "case-insensitive search returns receiver spelling");
        REQUIRE([[mixedCase commonPrefixWithString:lowerCase
                                          options:(NSCaseInsensitiveSearch | NSLiteralSearch)]
                 isEqualToString:mixedCase],
                "literal case-insensitive search still matches case");

        /* Canonical equivalence by default, in the receiver's own spelling. */
        NSString *decomposed = units(decomposedMaedchen,
                                     sizeof(decomposedMaedchen) / sizeof(decomposedMaedchen[0]));
        NSString *precomposed = units(precomposedMaedchen,
                                      sizeof(precomposedMaedchen) / sizeof(precomposedMaedchen[0]));
        NSString *decomposedResult = [decomposed commonPrefixWithString:precomposed options:0];
        REQUIRE(equalsUnits(decomposedResult, decomposedMaedchenPrefix,
                            sizeof(decomposedMaedchenPrefix) / sizeof(decomposedMaedchenPrefix[0])),
                "decomposed receiver keeps decomposed spelling");
        NSString *precomposedResult = [precomposed commonPrefixWithString:decomposed options:0];
        REQUIRE(equalsUnits(precomposedResult, precomposedMaedchen,
                            sizeof(precomposedMaedchen) / sizeof(precomposedMaedchen[0])),
                "precomposed receiver keeps precomposed spelling");

        /* Literal comparison stops before the differently encoded accented letter. */
        REQUIRE([[decomposed commonPrefixWithString:precomposed options:NSLiteralSearch]
                 isEqualToString:@"M"],
                "literal search keeps different normalisations distinct");

        /* A CRLF is one character: an equal pair is part of the prefix, and a
         * mismatched pair must not leak a partial carriage return. */
        NSString *crlfResult = [@"a\r\nitem" commonPrefixWithString:@"a\r\nnext" options:0];
        REQUIRE(equalsUnits(crlfResult, crlfPrefix,
                            sizeof(crlfPrefix) / sizeof(crlfPrefix[0])),
                "equal CRLF pair is shared");
        REQUIRE([[@"a\r\nrest" commonPrefixWithString:@"a\rXrest" options:0]
                 isEqualToString:@"a"],
                "mismatched CRLF pair is not partially shared");
        /* The same rule must hold on the non-ASCII comparison path, which the
         * leading umlaut selects. */
        NSString *unicodeCrlfReceiver = [units(uUmlaut, 1) stringByAppendingString:@"\r\nrest"];
        NSString *unicodeCrlfOther = [units(uUmlaut, 1) stringByAppendingString:@"\rXrest"];
        NSString *unicodeCrlfResult = [unicodeCrlfReceiver
            commonPrefixWithString:unicodeCrlfOther options:0];
        REQUIRE(equalsUnits(unicodeCrlfResult, uUmlaut, 1),
                "non-ASCII comparison does not share a partial CRLF");

        /* Adjacent regional indicators form one flag character. */
        NSString *flagResult = [units(usFlag, sizeof(usFlag) / sizeof(usFlag[0]))
            commonPrefixWithString:units(unFlag, sizeof(unFlag) / sizeof(unFlag[0]))
                           options:0];
        REQUIRE([flagResult isEqualToString:@""],
                "differing flags share no regional indicator");

        /* Multi-unit emoji characters compare whole, never by half a surrogate. */
        NSString *emojiResult = [units(grinningReceiver,
                                       sizeof(grinningReceiver) / sizeof(grinningReceiver[0]))
                                  commonPrefixWithString:units(grinningOther,
                                       sizeof(grinningOther) / sizeof(grinningOther[0]))
                                                 options:0];
        REQUIRE(equalsUnits(emojiResult, grinningPrefix,
                            sizeof(grinningPrefix) / sizeof(grinningPrefix[0])),
                "emoji divergence keeps whole characters");

        /* A realistic shared path much longer than 1024 UTF-16 units exercises
         * the non-ASCII comparison path and result extraction beyond the usual
         * small-string boundary. */
        NSMutableString *storage = [NSMutableString string];
        [storage appendString:units(uUmlaut, 1)];
        while ([storage length] < 1200) {
            [storage appendString:@"archive/2026/region-eu/report-daily-"];
        }
        NSString *sharedPath = [storage substringToIndex:1200];
        NSString *longReceiver = [sharedPath stringByAppendingString:@"alpha-report"];
        NSString *longOther = [sharedPath stringByAppendingString:@"beta-report"];
        NSString *longResult = [longReceiver commonPrefixWithString:longOther options:0];
        REQUIRE([longResult isEqualToString:sharedPath], "long shared prefix content");
        REQUIRE([longResult characterAtIndex:0] == 0x00FC,
                "long shared prefix keeps receiver's leading spelling");
    }

    puts("NSSTRING_COMMON_PREFIX_PASS=1");
    return 0;
}
