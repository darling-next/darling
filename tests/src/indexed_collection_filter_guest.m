#import <Foundation/Foundation.h>
#include <stdio.h>

#define REQUIRE(condition, label) do { \
    if (!(condition)) { fprintf(stderr, "FAIL indexed collection size=%lu stage=%s\n", (unsigned long)size, label); return 1; } \
} while (0)

int main(void)
{
    const NSUInteger sizes[] = {0, 1, 8, 16, 63, 64, 65, 66, 129, 130};
    for (NSUInteger sample = 0; sample < sizeof(sizes) / sizeof(sizes[0]); sample++) {
        @autoreleasepool {
            NSUInteger size = sizes[sample];
            NSMutableArray *input = [NSMutableArray arrayWithCapacity:size];
            NSMutableSet *expected = [NSMutableSet set];
            for (NSUInteger index = 0; index < size; index++) {
                NSNumber *value = [NSNumber numberWithUnsignedInteger:index];
                [input addObject:value];
                if ((index & 1) == 0) [expected addObject:value];
            }
            printf("INDEXED_COLLECTION size=%lu stage=array-selection\n", (unsigned long)size);
            fflush(stdout);
            NSIndexSet *indexes = [input indexesOfObjectsPassingTest:^BOOL(id object, NSUInteger index, BOOL *stop) {
                (void)object; (void)stop;
                return (index & 1) == 0;
            }];
            REQUIRE([indexes count] == (size + 1) / 2, "selected index count");
            NSArray *selected = [input objectsAtIndexes:indexes];
            REQUIRE([selected count] == (size + 1) / 2, "selected array count");
            for (NSUInteger index = 0; index < [selected count]; index++)
                REQUIRE([[selected objectAtIndex:index] unsignedIntegerValue] == index * 2,
                        "selected array values/order");
            if (size == 8) {
                NSMutableIndexSet *reordered = [NSMutableIndexSet indexSet];
                for (NSNumber *index in @[@6, @0, @4, @2]) [reordered addIndex:[index unsignedIntegerValue]];
                REQUIRE([[input objectsAtIndexes:reordered] isEqualToArray:selected],
                        "out-of-order index insertion");
            }
            printf("INDEXED_COLLECTION size=%lu stage=set-filter\n", (unsigned long)size);
            fflush(stdout);
            NSSet *source = [NSSet setWithArray:input];
            NSSet *filtered = [source objectsWithOptions:0 passingTest:^BOOL(id object, BOOL *stop) {
                (void)stop;
                return ([object unsignedIntegerValue] & 1) == 0;
            }];
            REQUIRE([filtered isEqualToSet:expected], "filtered set members");
            REQUIRE([source isEqualToSet:[NSSet setWithArray:input]], "source set unchanged");
        }
    }
    puts("INDEXED_COLLECTION_FILTER_GUEST_OK");
    return 0;
}
