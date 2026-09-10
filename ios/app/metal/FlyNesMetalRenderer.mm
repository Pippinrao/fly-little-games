#import "FlyNesMetalRenderer.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

enum class PipelineKind { Nearest, Sharp, Crt, Mmpx, Scalefx };

NSString *function_name(PipelineKind kind, bool vertex)
{
    switch (kind)
    {
    case PipelineKind::Nearest:
        return vertex ? @"flynes_nearest_vs" : @"flynes_nearest_fs";
    case PipelineKind::Sharp:
        return vertex ? @"flynes_sharp_vs" : @"flynes_sharp_fs";
    case PipelineKind::Crt:
        return vertex ? @"flynes_crt_vs" : @"flynes_crt_fs";
    case PipelineKind::Mmpx:
        return vertex ? @"flynes_mmpx_vs" : @"flynes_mmpx_fs";
    case PipelineKind::Scalefx:
        return vertex ? @"flynes_scalefx_pass4_vs" : @"flynes_scalefx_pass4_fs";
    }
}

} // namespace

@implementation FlyNesMetalRenderer {
    CAMetalLayer *layer_;
    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
    id<MTLLibrary> library_;
    id<MTLTexture> frameTexture_;
    id<MTLRenderPipelineState> activePipeline_;
    PipelineKind committedKind_;
    PipelineKind requestedKind_;
    FlyNesAspectMode aspect_;
    int frameWidth_;
    int frameHeight_;
}

- (instancetype)initWithLayer:(CAMetalLayer *)layer
{
    self = [super init];
    if (self == nil)
        return nil;
    layer_ = layer;
    device_ = layer.device ?: MTLCreateSystemDefaultDevice();
    layer_.device = device_;
    layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
    queue_ = [device_ newCommandQueue];
    library_ = [device_ newDefaultLibrary];
    aspect_ = FlyNesAspectFourByThree;
    requestedKind_ = PipelineKind::Sharp;
    committedKind_ = PipelineKind::Nearest;
    frameWidth_ = 256;
    frameHeight_ = 240;
    [self commitPipeline:PipelineKind::Nearest];
    return self;
}

- (PipelineKind)kindForSpatial:(FlyNesSpatialMode)spatial post:(FlyNesPostEffect)post
{
    if (post == FlyNesPostCrt)
        return PipelineKind::Crt;
    switch (spatial)
    {
    case FlyNesSpatialNearest:
        return PipelineKind::Nearest;
    case FlyNesSpatialMmpx:
        return PipelineKind::Mmpx;
    case FlyNesSpatialScalefx:
        return PipelineKind::Scalefx;
    case FlyNesSpatialSharpBilinear:
    default:
        return PipelineKind::Sharp;
    }
}

- (id<MTLRenderPipelineState>)buildPipeline:(PipelineKind)kind
{
    if (library_ == nil)
        return nil;
    MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = [library_ newFunctionWithName:function_name(kind, true)];
    descriptor.fragmentFunction = [library_ newFunctionWithName:function_name(kind, false)];
    descriptor.colorAttachments[0].pixelFormat = layer_.pixelFormat;
    if (descriptor.vertexFunction == nil || descriptor.fragmentFunction == nil)
        return nil;
    NSError *error = nil;
    id<MTLRenderPipelineState> pipeline = [device_ newRenderPipelineStateWithDescriptor:descriptor
                                                                                  error:&error];
    if (pipeline == nil || error != nil)
        return nil;
    return pipeline;
}

- (BOOL)commitPipeline:(PipelineKind)kind
{
    id<MTLRenderPipelineState> next = [self buildPipeline:kind];
    if (next == nil)
        return NO;
    // Never leave a half-switched pipeline: the previous state remains
    // bound until this assignment replaces the whole pipeline object.
    activePipeline_ = next;
    committedKind_ = kind;
    return YES;
}

- (void)setSpatialMode:(FlyNesSpatialMode)spatial postEffect:(FlyNesPostEffect)post
{
    requestedKind_ = [self kindForSpatial:spatial post:post];
    if ([self commitPipeline:requestedKind_])
        return;
    // Atomic fallback: Sharp, then Nearest. Keep the last good pipeline.
    if (requestedKind_ != PipelineKind::Sharp && [self commitPipeline:PipelineKind::Sharp])
        return;
    [self commitPipeline:PipelineKind::Nearest];
}

- (void)setAspectMode:(FlyNesAspectMode)aspect
{
    aspect_ = aspect;
}

- (MTLPixelFormat)rgb565Format
{
    if (@available(iOS 8.0, *))
        return MTLPixelFormatB5G6R5Unorm;
    return MTLPixelFormatRGBA8Unorm;
}

- (BOOL)uploadRgb565:(NSData *)pixels width:(int)width height:(int)height
{
    if (pixels.length < static_cast<NSUInteger>(width * height * 2) || width <= 0 || height <= 0)
        return NO;
    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:[self rgb565Format]
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
    if (texture == nil)
        return NO;
    MTLRegion region = MTLRegionMake2D(0, 0, static_cast<NSUInteger>(width),
                                       static_cast<NSUInteger>(height));
    [texture replaceRegion:region
               mipmapLevel:0
                 withBytes:pixels.bytes
               bytesPerRow:static_cast<NSUInteger>(width * 2)];
    frameTexture_ = texture;
    frameWidth_ = width;
    frameHeight_ = height;
    return YES;
}

- (CGRect)destinationRect:(CGSize)drawable
{
    const CGFloat srcW = static_cast<CGFloat>(frameWidth_);
    const CGFloat srcH = static_cast<CGFloat>(frameHeight_);
    CGFloat aspect = srcW / srcH;
    if (aspect_ == FlyNesAspectFourByThree)
        aspect = 4.0 / 3.0;
    else if (aspect_ == FlyNesAspectSquarePixels)
        aspect = srcW / srcH;

    CGFloat outW = drawable.width;
    CGFloat outH = drawable.width / aspect;
    if (outH > drawable.height)
    {
        outH = drawable.height;
        outW = drawable.height * aspect;
    }
    if (aspect_ == FlyNesAspectIntegerScale)
    {
        const CGFloat scale = std::max(1.0, std::floor(std::min(drawable.width / srcW, drawable.height / srcH)));
        outW = srcW * scale;
        outH = srcH * scale;
        if (aspect_ == FlyNesAspectIntegerScale && aspect == 4.0 / 3.0)
        {
            outW = srcH * scale * (4.0 / 3.0);
            outH = srcH * scale;
        }
    }
    return CGRectMake((drawable.width - outW) * 0.5, (drawable.height - outH) * 0.5, outW, outH);
}

- (BOOL)draw
{
    if (activePipeline_ == nil || frameTexture_ == nil)
        return NO;
    id<CAMetalDrawable> drawable = [layer_ nextDrawable];
    if (drawable == nil)
        return NO;
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandBuffer> buffer = [queue_ commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [buffer renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:activePipeline_];
    [encoder setFragmentTexture:frameTexture_ atIndex:0];
    const float sizes[4] = {
        static_cast<float>(frameWidth_), static_cast<float>(frameHeight_),
        static_cast<float>(drawable.texture.width), static_cast<float>(drawable.texture.height)
    };
    [encoder setFragmentBytes:sizes length:sizeof(sizes) atIndex:0];
    (void)[self destinationRect:CGSizeMake(drawable.texture.width, drawable.texture.height)];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [encoder endEncoding];
    [buffer presentDrawable:drawable];
    [buffer commit];
    return YES;
}

@end
