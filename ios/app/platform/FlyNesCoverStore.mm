#import "FlyNesCoverStore.h"

#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>
#include <CommonCrypto/CommonDigest.h>
#include <cstdio>
#include <cstring>

NSNotificationName const FlyNesCoverStoreDidChangeNotification = @"FlyNesCoverStoreDidChange";
NSString *const FlyNesCoverStoreCanonicalIdKey = @"canonicalId";

namespace {

constexpr NSUInteger kCoverWidth = 320;
constexpr NSUInteger kCoverHeight = 240;
constexpr NSUInteger kMemoryCacheLimit = 24;

NSString *cover_digest(NSString *canonicalId)
{
    unsigned char digest[CC_SHA256_DIGEST_LENGTH] = {};
    const char *bytes = canonicalId.UTF8String;
    if (bytes != nullptr)
        CC_SHA256(bytes, static_cast<CC_LONG>(std::strlen(bytes)), digest);
    NSMutableString *hex = [NSMutableString stringWithCapacity:CC_SHA256_DIGEST_LENGTH * 2];
    for (unsigned char byte : digest)
        [hex appendFormat:@"%02x", byte];
    return hex;
}

} // namespace

@implementation FlyNesCoverStore {
    NSCache<NSString *, FlyNesCoverImage *> *memory_;
    dispatch_queue_t io_;
    NSString *directory_;
}

+ (instancetype)sharedInstance
{
    static FlyNesCoverStore *instance;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      instance = [[FlyNesCoverStore alloc] init];
    });
    return instance;
}

- (instancetype)init
{
    self = [super init];
    if (self != nil)
    {
        memory_ = [[NSCache alloc] init];
        memory_.countLimit = kMemoryCacheLimit;
        io_ = dispatch_queue_create("com.flynes.app.covers", DISPATCH_QUEUE_SERIAL);
        NSArray<NSString *> *caches =
            NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
        NSString *root = caches.firstObject ?: NSTemporaryDirectory();
        [self configureWithCacheRoot:root];
    }
    return self;
}

- (void)configureWithCacheRoot:(NSString *)root
{
    if (root.length == 0)
        return;
    @synchronized(self)
    {
        directory_ = [root stringByAppendingPathComponent:@"covers/v1"];
        [memory_ removeAllObjects];
    }
    NSFileManager *files = NSFileManager.defaultManager;
    [files createDirectoryAtPath:directory_ withIntermediateDirectories:YES attributes:nil error:nil];
    NSURL *url = [NSURL fileURLWithPath:directory_ isDirectory:YES];
    [url setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];
}

- (NSString *)directory
{
    @synchronized(self)
    {
        return directory_;
    }
}

- (NSString *)pathForCanonicalId:(NSString *)canonicalId
{
    return [[self directory] stringByAppendingPathComponent:
        [cover_digest(canonicalId) stringByAppendingPathExtension:@"png"]];
}

- (BOOL)hasCoverForCanonicalId:(NSString *)canonicalId
{
    if (canonicalId.length == 0)
        return NO;
    if ([memory_ objectForKey:canonicalId] != nil)
        return YES;
    return [NSFileManager.defaultManager fileExistsAtPath:[self pathForCanonicalId:canonicalId]];
}

- (nullable FlyNesCoverImage *)coverForCanonicalId:(NSString *)canonicalId
{
    if (canonicalId.length == 0)
        return nil;
    FlyNesCoverImage *cached = [memory_ objectForKey:canonicalId];
    if (cached != nil)
        return cached;
    NSString *path = [self pathForCanonicalId:canonicalId];
    NSData *data = [NSData dataWithContentsOfFile:path];
    if (data.length == 0)
        return nil;
    FlyNesCoverImage *image = [FlyNesCoverImage imageWithData:data];
    if (image != nil)
        [memory_ setObject:image forKey:canonicalId];
    return image;
}

- (void)removeCoverForCanonicalId:(NSString *)canonicalId
{
    if (canonicalId.length == 0)
        return;
    [memory_ removeObjectForKey:canonicalId];
    [NSFileManager.defaultManager removeItemAtPath:[self pathForCanonicalId:canonicalId] error:nil];
}

- (BOOL)storeRgb565Frame:(NSData *)pixels
               canonicalId:(NSString *)canonicalId
                     width:(NSUInteger)width
                    height:(NSUInteger)height
{
    if (canonicalId.length == 0 || width == 0 || height == 0)
        return NO;
    if (pixels.length != width * height * 2u)
        return NO;
    NSString *directory = [self directory];
    if (directory.length == 0)
        return NO;
    NSFileManager *files = NSFileManager.defaultManager;
    if (![files fileExistsAtPath:directory])
        [files createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];

    const uint8_t *source = static_cast<const uint8_t *>(pixels.bytes);
    // Nearest-neighbour box filter onto the fixed 320x240 cover size, the same
    // conversion Android performs with `Bitmap.createScaledBitmap(..., false)`.
    NSMutableData *rgba = [NSMutableData dataWithLength:kCoverWidth * kCoverHeight * 4u];
    uint8_t *target = static_cast<uint8_t *>(rgba.mutableBytes);
    for (NSUInteger y = 0; y < kCoverHeight; ++y)
    {
        const NSUInteger source_y = y * height / kCoverHeight;
        for (NSUInteger x = 0; x < kCoverWidth; ++x)
        {
            const NSUInteger source_x = x * width / kCoverWidth;
            const size_t index = (source_y * width + source_x) * 2u;
            const unsigned value = static_cast<unsigned>(source[index])
                                 | (static_cast<unsigned>(source[index + 1u]) << 8);
            const uint8_t red = static_cast<uint8_t>(((value >> 11) & 0x1Fu) * 255u / 31u);
            const uint8_t green = static_cast<uint8_t>(((value >> 5) & 0x3Fu) * 255u / 63u);
            const uint8_t blue = static_cast<uint8_t>((value & 0x1Fu) * 255u / 31u);
            uint8_t *out = target + (y * kCoverWidth + x) * 4u;
            out[0] = red;
            out[1] = green;
            out[2] = blue;
            out[3] = 0xFF;
        }
    }

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = space == nullptr ? nullptr
        : CGBitmapContextCreate(target, kCoverWidth, kCoverHeight, 8, kCoverWidth * 4u,
                                space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    if (space != nullptr)
        CGColorSpaceRelease(space);
    if (context == nullptr)
        return NO;
    CGImageRef image = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    if (image == nullptr)
        return NO;

    NSMutableData *png = [NSMutableData data];
    CGImageDestinationRef destination =
        CGImageDestinationCreateWithData((__bridge CFMutableDataRef)png, CFSTR("public.png"), 1, nullptr);
    if (destination == nullptr)
    {
        CGImageRelease(image);
        return NO;
    }
    CGImageDestinationAddImage(destination, image, nullptr);
    const bool encoded = CGImageDestinationFinalize(destination);
    CFRelease(destination);
    CGImageRelease(image);
    if (!encoded || png.length == 0)
        return NO;

    NSString *target_path = [self pathForCanonicalId:canonicalId];
    NSString *temporary = [directory stringByAppendingPathComponent:
        [NSString stringWithFormat:@"cover-%@.tmp", NSUUID.UUID.UUIDString]];
    if (![png writeToFile:temporary options:NSDataWritingAtomic error:nil])
    {
        [files removeItemAtPath:temporary error:nil];
        return NO;
    }
    // rename(2) on one filesystem is the atomic durable replace Android performs
    // with `Os.rename`, and it cannot leave a half-written cover behind.
    if (std::rename(temporary.fileSystemRepresentation, target_path.fileSystemRepresentation) != 0)
    {
        [files removeItemAtPath:temporary error:nil];
        return NO;
    }
    FlyNesCoverImage *stored = [FlyNesCoverImage imageWithData:png];
    if (stored != nil)
        [memory_ setObject:stored forKey:canonicalId];
    dispatch_async(dispatch_get_main_queue(), ^{
      [NSNotificationCenter.defaultCenter postNotificationName:FlyNesCoverStoreDidChangeNotification
                                                        object:self
                                                      userInfo:@{FlyNesCoverStoreCanonicalIdKey : canonicalId}];
    });
    return YES;
}

@end
