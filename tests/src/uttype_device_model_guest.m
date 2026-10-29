#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UTType.h>
#include <stdio.h>

/* The same consumer must compile with the old installed SDK for runtime RED. */
@interface UTType (DeviceModelConsumer)
+ (UTType *)_typeWithDeviceModelCode:(NSString *)modelCode;
@end
extern NSString *const _UTTagClassDeviceModelCode;

#define REQUIRE(condition, label) do { \
    if (!(condition)) { fprintf(stderr, "FAIL UTType device model: %s\n", label); return 1; } \
} while (0)

int main(void)
{
    @autoreleasepool {
        REQUIRE([UTType respondsToSelector:@selector(_typeWithDeviceModelCode:)], "private lookup unavailable");
        REQUIRE([_UTTagClassDeviceModelCode isEqualToString:@"com.apple.device-model-code"], "model tag class");
        UTType *model = [UTType _typeWithDeviceModelCode:@"iPhone3,1"];
        REQUIRE([[model identifier] isEqualToString:@"com.apple.iphone-4"]
                && [model isDeclared] && ![model isDynamic], "registered model identity");
        UTType *alias = [UTType typeWithTag:@"N90AP" tagClass:_UTTagClassDeviceModelCode conformingToType:nil];
        UTType *identified = [UTType typeWithIdentifier:@"com.apple.iphone-4"];
        REQUIRE([model isEqual:alias] && [model isEqual:identified]
                && [model hash] == [alias hash], "alias and identifier equality");
        REQUIRE([model isEqual:[UTType _typeWithDeviceModelCode:@"iphone3,1"]], "case-folded model tag");
        NSArray *tags = [[model tags] objectForKey:_UTTagClassDeviceModelCode];
        REQUIRE([tags containsObject:@"iPhone3,1"] && [tags containsObject:@"N90AP"], "original declaration aliases");
        for (NSString *parent in @[@"com.apple.iphone", @"com.apple.ios-device", @"com.apple.device", @"public.device"])
            REQUIRE([model conformsToType:[UTType typeWithIdentifier:parent]], "transitive device conformance");
        UTType *data = [UTType typeWithIdentifier:@"public.data"];
        REQUIRE(![model conformsToType:data], "hardware must not become file data");
        REQUIRE([UTType typeWithTag:@"iPhone3,1" tagClass:_UTTagClassDeviceModelCode conformingToType:data] == nil,
                "incompatible conformance constraint");
        REQUIRE([UTType _typeWithDeviceModelCode:nil] == nil
                && [UTType _typeWithDeviceModelCode:@""] == nil
                && [UTType _typeWithDeviceModelCode:@"Darling-Unregistered-Model"] == nil,
                "private absent model lookup");
        REQUIRE([UTType typeWithTag:@"Darling-Unregistered-Model" tagClass:_UTTagClassDeviceModelCode conformingToType:nil] == nil
                && [[UTType typesWithTag:@"Darling-Unregistered-Model" tagClass:_UTTagClassDeviceModelCode conformingToType:nil] count] == 0,
                "public absent model must not invent hardware identity");
        UTType *png = [UTType typeWithIdentifier:@"public.png"];
        REQUIRE([png isEqual:[UTType typeWithFilenameExtension:@"png"]]
                && [png isEqual:[UTType typeWithMIMEType:@"image/png"]], "unrelated image lookup");
        UTType *dynamic = [UTType typeWithFilenameExtension:@"darling-unregistered-model-consumer"];
        REQUIRE([dynamic isDynamic] && [dynamic conformsToType:data], "unrelated dynamic file tag");
        puts("UTTYPE_DEVICE_MODEL_GUEST_OK");
        return 0;
    }
}
