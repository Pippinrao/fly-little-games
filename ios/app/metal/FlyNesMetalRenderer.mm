#import "FlyNesMetalRenderer.h"
#include "Rgb565Upload.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

namespace {

enum class PipelineKind { Nearest, Sharp, Crt, Mmpx, Scale0, Scale1, Scale2, Scale3, Scalefx };

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
    case PipelineKind::Scale0:
        return vertex ? @"flynes_scalefx_pass0_vs" : @"flynes_scalefx_pass0_fs";
    case PipelineKind::Scale1:
        return vertex ? @"flynes_scalefx_pass1_vs" : @"flynes_scalefx_pass1_fs";
    case PipelineKind::Scale2:
        return vertex ? @"flynes_scalefx_pass2_vs" : @"flynes_scalefx_pass2_fs";
    case PipelineKind::Scale3:
        return vertex ? @"flynes_scalefx_pass3_vs" : @"flynes_scalefx_pass3_fs";
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
    NSMutableDictionary<NSNumber *, id<MTLRenderPipelineState>> *pipelines_;
    FlyNesPostEffect post_;
    PipelineKind committedKind_;
    PipelineKind requestedKind_;
    FlyNesAspectMode aspect_;
    int frameWidth_;
    int frameHeight_;
    std::atomic<bool> permanentFailure_;
}

- (instancetype)initWithLayer:(CAMetalLayer *)layer
{
    self = [super init];
    if (self == nil)
        return nil;
    permanentFailure_.store(false);
    layer_ = layer;
    device_ = layer.device ?: MTLCreateSystemDefaultDevice();
    layer_.device = device_;
    layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
    queue_ = [device_ newCommandQueue];
    library_ = [device_ newDefaultLibrary];
    pipelines_ = [NSMutableDictionary dictionary];
    post_ = FlyNesPostNone;
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
    (void)post;
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
    id<MTLRenderPipelineState> cached = pipelines_[@((int)kind)];
    if (cached != nil) return cached;
    if (library_ == nil)
        return nil;
    MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = [library_ newFunctionWithName:function_name(kind, true)];
    descriptor.fragmentFunction = [library_ newFunctionWithName:function_name(kind, false)];
    descriptor.colorAttachments[0].pixelFormat = kind >= PipelineKind::Scale0
        && kind <= PipelineKind::Scale3 ? MTLPixelFormatRGBA32Float : MTLPixelFormatBGRA8Unorm;
    if (descriptor.vertexFunction == nil || descriptor.fragmentFunction == nil)
        return nil;
    NSError *error = nil;
    id<MTLRenderPipelineState> pipeline = [device_ newRenderPipelineStateWithDescriptor:descriptor
                                                                                  error:&error];
    if (pipeline == nil || error != nil)
        return nil;
    pipelines_[@((int)kind)] = pipeline;
    return pipeline;
}

- (BOOL)commitPipeline:(PipelineKind)kind
{
    id<MTLRenderPipelineState> next = [self buildPipeline:kind];
    if (next == nil)
        return NO;
    if (kind == PipelineKind::Scalefx) {
        for (int pass = (int)PipelineKind::Scale0; pass <= (int)PipelineKind::Scale3; ++pass)
            if ([self buildPipeline:(PipelineKind)pass] == nil) return NO;
    }
    const PipelineKind terminal = kind == PipelineKind::Nearest ? PipelineKind::Nearest : PipelineKind::Sharp;
    if ([self buildPipeline:terminal] == nil) return NO;
    // Never leave a half-switched pipeline: the previous state remains
    // bound until this assignment replaces the whole pipeline object.
    activePipeline_ = next;
    committedKind_ = kind;
    return YES;
}

- (void)setSpatialMode:(FlyNesSpatialMode)spatial postEffect:(FlyNesPostEffect)post
{
    post_ = post == FlyNesPostCrt && [self buildPipeline:PipelineKind::Crt] != nil
        ? FlyNesPostCrt : FlyNesPostNone;
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
    // Packed RGB565 is not supported by Simulator. A normalized RGBA8 upload
    // gives the same color channels on Intel simulator and iPhone.
    return MTLPixelFormatRGBA8Unorm;
}

- (BOOL)uploadRgb565:(NSData *)pixels width:(int)width height:(int)height
{
    const auto rgba = flynes::ios::rgb565_to_rgba8(
        static_cast<const uint8_t *>(pixels.bytes), pixels.length, width, height);
    if (rgba.empty())
        return NO;
    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:[self rgb565Format]
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
    if (texture == nil) {
        permanentFailure_.store(true);
        return NO;
    }
    MTLRegion region = MTLRegionMake2D(0, 0, static_cast<NSUInteger>(width),
                                       static_cast<NSUInteger>(height));
    [texture replaceRegion:region
               mipmapLevel:0
                 withBytes:rgba.data()
               bytesPerRow:static_cast<NSUInteger>(width * 4)];
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
        outW = std::min(drawable.width, srcW * scale);
        outH = std::min(drawable.height, srcH * scale);
    }
    return CGRectMake((drawable.width - outW) * 0.5, (drawable.height - outH) * 0.5, outW, outH);
}

- (BOOL)permanentFailure { return permanentFailure_.load(); }

- (BOOL)draw
{
    if (permanentFailure_.load()) return NO;
    if (activePipeline_ == nil || queue_ == nil) {
        permanentFailure_.store(true);
        return NO;
    }
    if (frameTexture_ == nil) return NO;
    id<CAMetalDrawable> drawable = [layer_ nextDrawable];
    if (drawable == nil)
        return NO;
    id<MTLCommandBuffer> buffer = [queue_ commandBuffer];
    if (![self encodeToTexture:drawable.texture commandBuffer:buffer]) {
        permanentFailure_.store(true);
        return NO;
    }
    __weak FlyNesMetalRenderer *weakSelf = self;
    [buffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        FlyNesMetalRenderer *strong = weakSelf;
        if (strong && completed.status == MTLCommandBufferStatusError)
            strong->permanentFailure_.store(true);
    }];
    [buffer presentDrawable:drawable];
    [buffer commit];
    return YES;
}

- (BOOL)encodeToTexture:(id<MTLTexture>)target commandBuffer:(id<MTLCommandBuffer>)buffer
{
    if (!target || !buffer || !frameTexture_ || !activePipeline_) return NO;
    auto texture = [&](NSUInteger width, NSUInteger height, MTLPixelFormat format) {
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
            width:width height:height mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;
        return [device_ newTextureWithDescriptor:desc];
    };
    auto render = [&](PipelineKind kind, id<MTLTexture> first, id<MTLTexture> second,
                      id<MTLTexture> output, CGRect viewport) {
        id<MTLRenderPipelineState> pipeline = [self buildPipeline:kind];
        if (!pipeline || !output) return false;
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = output;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,1);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> encoder = [buffer renderCommandEncoderWithDescriptor:pass];
        if (!encoder) return false;
        [encoder setRenderPipelineState:pipeline];
        [encoder setViewport:MTLViewport{viewport.origin.x, viewport.origin.y,
            viewport.size.width, viewport.size.height, 0, 1}];
        [encoder setFragmentTexture:first atIndex:0];
        if (second) [encoder setFragmentTexture:second atIndex:1];
        const float sizes[4] = {(float)first.width, (float)first.height,
            (float)viewport.size.width, (float)viewport.size.height};
        [encoder setFragmentBytes:sizes length:sizeof(sizes) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [encoder endEncoding];
        return true;
    };
    id<MTLTexture> spatial = frameTexture_;
    if (committedKind_ == PipelineKind::Mmpx) {
        spatial = texture(frameWidth_*2, frameHeight_*2, MTLPixelFormatBGRA8Unorm);
        if (!render(PipelineKind::Mmpx, frameTexture_, nil, spatial,
            CGRectMake(0,0,frameWidth_*2,frameHeight_*2))) return NO;
    } else if (committedKind_ == PipelineKind::Scalefx) {
        const CGRect rect = CGRectMake(0,0,frameWidth_,frameHeight_);
        id<MTLTexture> metric = texture(frameWidth_,frameHeight_,MTLPixelFormatRGBA32Float);
        id<MTLTexture> strength = texture(frameWidth_,frameHeight_,MTLPixelFormatRGBA32Float);
        id<MTLTexture> edges = texture(frameWidth_,frameHeight_,MTLPixelFormatRGBA32Float);
        id<MTLTexture> packed = texture(frameWidth_,frameHeight_,MTLPixelFormatRGBA32Float);
        spatial = texture(frameWidth_*3,frameHeight_*3,MTLPixelFormatBGRA8Unorm);
        if (!render(PipelineKind::Scale0,frameTexture_,nil,metric,rect)
            || !render(PipelineKind::Scale1,metric,nil,strength,rect)
            || !render(PipelineKind::Scale2,metric,strength,edges,rect)
            || !render(PipelineKind::Scale3,edges,nil,packed,rect)
            || !render(PipelineKind::Scalefx,packed,frameTexture_,spatial,
                CGRectMake(0,0,frameWidth_*3,frameHeight_*3))) return NO;
    }
    const CGRect destination = [self destinationRect:CGSizeMake(target.width,target.height)];
    const PipelineKind scaling = committedKind_ == PipelineKind::Nearest
        ? PipelineKind::Nearest : PipelineKind::Sharp;
    if (post_ == FlyNesPostCrt) {
        id<MTLTexture> scaled = texture((NSUInteger)std::ceil(destination.size.width),
            (NSUInteger)std::ceil(destination.size.height),MTLPixelFormatBGRA8Unorm);
        if (!render(scaling,spatial,nil,scaled,CGRectMake(0,0,scaled.width,scaled.height))) return NO;
        return render(PipelineKind::Crt,scaled,nil,target,destination);
    }
    return render(scaling,spatial,nil,target,destination);
}

@end
