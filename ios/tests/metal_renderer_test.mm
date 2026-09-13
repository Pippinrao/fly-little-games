#import <Foundation/Foundation.h>
#import "FlyNesMetalRenderer.h"
#include <iostream>
#include <stdexcept>
#include <vector>

@interface FlyNesMetalRenderer (Testing)
- (BOOL)encodeToTexture:(id<MTLTexture>)target commandBuffer:(id<MTLCommandBuffer>)buffer;
- (CGRect)destinationRect:(CGSize)drawable;
@end
static void check(bool yes, const char *message) { if (!yes) throw std::runtime_error(message); }
int main(int argc, char **argv) { @autoreleasepool { try {
    check(argc == 2, "shader directory required");
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    check(device != nil, "Metal device unavailable");
    NSMutableString *source = [NSMutableString string];
    NSMutableSet *types = [NSMutableSet set];
    NSRegularExpression *structures = [NSRegularExpression regularExpressionWithPattern:@"struct ([A-Za-z0-9_]+) \\{[^}]+\\};" options:0 error:nil];
    NSString *directory = @(argv[1]);
    for (NSString *name in [[NSFileManager.defaultManager contentsOfDirectoryAtPath:directory error:nil] sortedArrayUsingSelector:@selector(compare:)]) {
        if (![name.pathExtension isEqualToString:@"metal"]) continue;
        NSMutableString *part = [[NSString stringWithContentsOfFile:[directory stringByAppendingPathComponent:name] encoding:NSUTF8StringEncoding error:nil] mutableCopy];
        for (NSTextCheckingResult *match in [[structures matchesInString:part options:0 range:NSMakeRange(0, part.length)] reverseObjectEnumerator]) {
            NSString *type = [part substringWithRange:[match rangeAtIndex:1]];
            if ([types containsObject:type]) [part deleteCharactersInRange:match.range];
            else [types addObject:type];
        }
        [source appendString:part];
    }
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) std::cerr << error.description.UTF8String << '\n';
    check(library != nil, "compile Metal shaders");
    CAMetalLayer *layer = [CAMetalLayer layer]; layer.device = device;
    FlyNesMetalRenderer *renderer = [[FlyNesMetalRenderer alloc] initWithLayer:layer];
    [renderer setValue:library forKey:@"library_"];
    std::vector<uint16_t> pixels(32 * 24);
    for (int y=0; y<24; ++y) for (int x=0; x<32; ++x)
        pixels[y*32+x] = y < 12 ? (x<16 ? 0xf800 : 0x07e0) : (x<16 ? 0x001f : 0xffff);
    check([renderer uploadRgb565:[NSData dataWithBytes:pixels.data() length:pixels.size()*2] width:32 height:24], "upload");
    auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:160 height:96 mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> output = [device newTextureWithDescriptor:descriptor];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    for (int spatial=1; spatial<=4; ++spatial) {
        [renderer setSpatialMode:(FlyNesSpatialMode)spatial postEffect:FlyNesPostNone];
        [renderer setAspectMode:FlyNesAspectFourByThree];
        id<MTLCommandBuffer> buffer = [queue commandBuffer];
        check([renderer encodeToTexture:output commandBuffer:buffer], "encode render graph");
        [buffer commit]; [buffer waitUntilCompleted];
        if (buffer.error) std::cerr << buffer.error.description.UTF8String << '\n';
        check(buffer.status == MTLCommandBufferStatusCompleted, "GPU completion");
        std::vector<uint8_t> actual(160*96*4);
        [output getBytes:actual.data() bytesPerRow:160*4 fromRegion:MTLRegionMake2D(0,0,160,96) mipmapLevel:0];
        auto color = [&](int x,int y,int b,int g,int r) { auto p=&actual[(y*160+x)*4]; return p[0]==b && p[1]==g && p[2]==r && p[3]==255; };
        check(color(0,48,0,0,0) && color(159,48,0,0,0), "aspect ratio black bars");
        check(color(32,16,0,0,255) && color(128,16,0,255,0), "top colors and orientation");
        check(color(32,80,255,0,0) && color(128,80,255,255,255), "bottom colors and orientation");
    }
    std::vector<uint16_t> edge(49,0xffff);
    edge[3*7+3] = edge[2*7+4] = 0x0920;
    edge[2*7+2] = edge[2*7+3] = edge[3*7+2] = 0x0160;
    check([renderer uploadRgb565:[NSData dataWithBytes:edge.data() length:98] width:7 height:7], "MMPX edge upload");
    [renderer setSpatialMode:FlyNesSpatialMmpx postEffect:FlyNesPostNone];
    [renderer setAspectMode:FlyNesAspectSquarePixels];
    descriptor.width = 14; descriptor.height = 14;
    output = [device newTextureWithDescriptor:descriptor];
    id<MTLCommandBuffer> edgeBuffer = [queue commandBuffer];
    check([renderer encodeToTexture:output commandBuffer:edgeBuffer], "MMPX edge render");
    [edgeBuffer commit]; [edgeBuffer waitUntilCompleted];
    std::vector<uint8_t> edgeResult(14*14*4);
    [output getBytes:edgeResult.data() bytesPerRow:14*4 fromRegion:MTLRegionMake2D(0,0,14,14) mipmapLevel:0];
    const auto pixel = &edgeResult[(6*14+6)*4];
    check(pixel[0] == 0 && pixel[1] == 45 && pixel[2] == 0, "MMPX must compare original RGB565 luma");
    [renderer setAspectMode:FlyNesAspectIntegerScale];
    CGRect small = [renderer destinationRect:CGSizeMake(5,4)];
    check(small.origin.x >= 0 && small.origin.y >= 0 && small.size.width <= 5 && small.size.height <= 4,
        "integer viewport must fit smaller drawable");
    [renderer setSpatialMode:FlyNesSpatialMmpx postEffect:FlyNesPostCrt];
    [renderer setAspectMode:FlyNesAspectSquarePixels];
    id<MTLCommandBuffer> crtBuffer = [queue commandBuffer];
    check([renderer encodeToTexture:output commandBuffer:crtBuffer], "CRT graph encodes");
    [crtBuffer commit]; [crtBuffer waitUntilCompleted];
    check(crtBuffer.status == MTLCommandBufferStatusCompleted, "CRT GPU completion");
    std::vector<uint8_t> crtResult(edgeResult.size());
    [output getBytes:crtResult.data() bytesPerRow:14*4 fromRegion:MTLRegionMake2D(0,0,14,14) mipmapLevel:0];
    check(crtResult != edgeResult && crtResult[(6*14+6)*4+2] == 0, "CRT composes after MMPX spatial result");
    NSString *nearestSource = [NSString stringWithContentsOfFile:[directory stringByAppendingPathComponent:@"Nearest.metal"] encoding:NSUTF8StringEncoding error:nil];
    id<MTLLibrary> nearestLibrary = [device newLibraryWithSource:nearestSource options:nil error:nil];
    FlyNesMetalRenderer *fallback = [[FlyNesMetalRenderer alloc] initWithLayer:layer];
    [fallback setValue:nearestLibrary forKey:@"library_"];
    [[fallback valueForKey:@"pipelines_"] removeAllObjects];
    [fallback setSpatialMode:FlyNesSpatialScalefx postEffect:FlyNesPostNone];
    check([fallback uploadRgb565:[NSData dataWithBytes:edge.data() length:98] width:7 height:7], "fallback upload");
    id<MTLCommandBuffer> fallbackBuffer = [queue commandBuffer];
    check([fallback encodeToTexture:output commandBuffer:fallbackBuffer], "missing Sharp still falls back to Nearest");
    [fallbackBuffer commit]; [fallbackBuffer waitUntilCompleted];
    check(fallbackBuffer.status == MTLCommandBufferStatusCompleted, "fallback GPU completion");
    check([fallback respondsToSelector:NSSelectorFromString(@"permanentFailure")],
        "renderer must distinguish permanent failure from a skipped drawable");
    [fallback setValue:nil forKey:@"frameTexture_"];
    check(![fallback draw] && ![[fallback valueForKey:@"permanentFailure"] boolValue],
        "waiting for a first frame is not a permanent failure");
    [fallback setValue:nil forKey:@"activePipeline_"];
    check(![fallback draw] && [[fallback valueForKey:@"permanentFailure"] boolValue],
        "a missing terminal pipeline must stop video instead of silently running");
    std::cout << "ios_metal_renderer: PASS\n";
} catch (const std::exception& error) { std::cerr << "ios_metal_renderer: FAIL: " << error.what() << '\n'; return 1; } } }
