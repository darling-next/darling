#import <ImageIO/CGImageSource.h>
#import <Onyx2D/O2ImageSource.h>

const CFStringRef kCGImageSourceCreateThumbnailFromImageAlways = CFSTR("kCGImageSourceCreateThumbnailFromImageAlways");
const CFStringRef kCGImageSourceCreateThumbnailFromImageIfAbsent = CFSTR("kCGImageSourceCreateThumbnailFromImageIfAbsent");
const CFStringRef kCGImageSourceCreateThumbnailWithTransform = CFSTR("kCGImageSourceCreateThumbnailWithTransform");
const CFStringRef kCGImageSourceShouldAllowFloat = CFSTR("kCGImageSourceShouldAllowFloat");
const CFStringRef kCGImageSourceShouldCache = CFSTR("kCGImageSourceShouldCache");
const CFStringRef kCGImageSourceThumbnailMaxPixelSize = CFSTR("kCGImageSourceThumbnailMaxPixelSize");

@interface _O2ImageSource : O2ImageSource
@end

CGImageSourceRef CGImageSourceCreateWithData(CFDataRef data,CFDictionaryRef options) {
	return (CGImageSourceRef)[O2ImageSource newImageSourceWithData:data options:options];
}

CGImageSourceRef CGImageSourceCreateWithURL(CFURLRef url,CFDictionaryRef options) {
	return (CGImageSourceRef)[O2ImageSource newImageSourceWithURL:(NSURL *)url options:options];
}

size_t CGImageSourceGetCount(CGImageSourceRef self) {
   return [self count];
}

CGImageRef CGImageSourceCreateImageAtIndex(CGImageSourceRef self,size_t index,CFDictionaryRef options) {
   return [self createImageAtIndex:index options:options];
}

CFDictionaryRef CGImageSourceCopyProperties(CGImageSourceRef self,CFDictionaryRef options) {
   /* Apple returns the CONTAINER's properties here (CGImageSource.h: "These properties apply to the
      container in general").  Onyx2D's O2ImageSource exposes only its per-image properties
      (copyPropertiesAtIndex:), so the honest answer is the real, parsed properties of the source's
      own first image -- exactly the facts the family can produce from a file it parsed -- and never
      an invented dictionary: the base O2ImageSource and the formats whose source does not populate
      properties (PNG, BMP, ICNS) return an EMPTY dictionary, so a caller that reads PixelWidth /
      PixelHeight observes absence rather than a fabricated size. */
   return (CFDictionaryRef)[self copyPropertiesAtIndex:0 options:options];
}

CFDictionaryRef CGImageSourceCopyPropertiesAtIndex(CGImageSourceRef self, size_t index,CFDictionaryRef options) {
   return (CFDictionaryRef)[self copyPropertiesAtIndex:index options:options];
}

CFStringRef CGImageSourceGetType(CGImageSourceRef self)
{
    return [self type];
}

CGImageSourceRef CGImageSourceCreateWithDataProvider(CGDataProviderRef provider, CFDictionaryRef options)
{
    printf("STUB %s\n", __PRETTY_FUNCTION__);
    return nil;
}
