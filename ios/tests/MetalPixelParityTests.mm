// Runs against the product's bundled default.metallib in FlyNESRuntimeTests.
// Compare complete binary-geometry images with independent Android goldens.
// This does not certify full-color thresholds, device performance or display quality.
#import <XCTest/XCTest.h>
#import "../app/metal/FlyNesMetalRenderer.h"
#include "MetalBinaryGoldenFixtures.hpp"
#include <cstdlib>
#include <cstring>
#include <vector>

@interface FlyNesMetalRenderer (PixelParityTesting)
- (BOOL)encodeToTexture:(id<MTLTexture>)target commandBuffer:(id<MTLCommandBuffer>)buffer;
@end

@interface MetalPixelParityTests : XCTestCase
@end

@implementation MetalPixelParityTests

- (void)setUp
{
    [super setUp];
    self.continueAfterFailure = NO;
}

- (void)verifyFixture:(const flynes::ios::tests::MetalBinaryGoldenFixture&)fixture
             spatial:(FlyNesSpatialMode)mode
{
    const BOOL mmpx = mode == FlyNesSpatialMmpx;
    XCTAssertTrue(mmpx || mode == FlyNesSpatialScalefx);
    const int scale = mmpx ? 2 : 3;
    const char* expected = mmpx ? fixture.mmpx : fixture.scalefx;
    const NSUInteger width = static_cast<NSUInteger>(fixture.width * scale);
    const NSUInteger height = static_cast<NSUInteger>(fixture.height * scale);
    XCTAssertEqual(std::strlen(fixture.input), static_cast<size_t>(fixture.width * fixture.height));
    XCTAssertEqual(std::strlen(expected), static_cast<size_t>(width * height));

    // The Android fixtures use two opaque colors, not exactly representable in
    // RGB565. Their binary masks are mapped to black/white in the helper rather
    // than incorrectly comparing quantized RGB565 against RGBA8888 goldens.
    // MMPX preserves the same equality and luma ordering. In these ScaleFX
    // patterns the only distances are zero or one common distance > SFX_CLR
    // (0.5); the color mapping preserves that split and corner-strength ordering.
    // No production color conversion or shader code computes the expected image.
    std::vector<uint8_t> input(static_cast<size_t>(fixture.width * fixture.height) * 2);
    for (size_t pixel = 0; pixel < input.size() / 2; ++pixel) {
        XCTAssertTrue(fixture.input[pixel] == '0' || fixture.input[pixel] == '1');
        const uint8_t value = fixture.input[pixel] == '1' ? 255 : 0;
        input[pixel * 2] = value;
        input[pixel * 2 + 1] = value;
    }

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    XCTAssertNotNil(device, @"Metal is required; this parity test must not silently skip");
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = device;
    FlyNesMetalRenderer* renderer = [[FlyNesMetalRenderer alloc] initWithLayer:layer];
    XCTAssertNotNil([renderer valueForKey:@"library_"], @"The product default.metallib must be bundled");
    XCTAssertTrue([renderer uploadRgb565:[NSData dataWithBytes:input.data() length:input.size()]
                                  width:fixture.width height:fixture.height]);
    [renderer setAspectMode:FlyNesAspectSquarePixels];
    [renderer setSpatialMode:mode postEffect:FlyNesPostNone];

    // White-box assertion is deliberate: flat or thin-line goldens can also
    // match Nearest. PipelineKind is private; these values correspond to Mmpx
    // and Scalefx in FlyNesMetalRenderer.mm, NOT the public spatial enum values.
    // Keep this guard in sync if that private enum is reordered.
    const NSInteger expectedKind = mmpx ? 3 : 8;
    XCTAssertEqual([[renderer valueForKey:@"requestedKind_"] integerValue], expectedKind);
    XCTAssertEqual([[renderer valueForKey:@"committedKind_"] integerValue], expectedKind,
                   @"%s: requested advanced filter silently fell back", fixture.name);

    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                    width:width height:height mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> output = [device newTextureWithDescriptor:descriptor];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> buffer = [queue commandBuffer];
    XCTAssertNotNil(output);
    XCTAssertNotNil(buffer);
    XCTAssertTrue([renderer encodeToTexture:output commandBuffer:buffer], @"%s", fixture.name);
    [buffer commit];
    [buffer waitUntilCompleted];
    XCTAssertEqual(buffer.status, MTLCommandBufferStatusCompleted, @"%@", buffer.error);
    XCTAssertEqual([[renderer valueForKey:@"committedKind_"] integerValue], expectedKind,
                   @"%s: mode changed while encoding", fixture.name);

    // Target is exactly 2x/3x the source, so the product's final Sharp pass is
    // sampled at reconstructed texel centers. No viewport rounding or display
    // scaling is hidden in the oracle comparison.
    std::vector<uint8_t> actual(width * height * 4);
    [output getBytes:actual.data() bytesPerRow:width * 4
         fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
    const int tolerance = mmpx ? 0 : 1; // Same allowance as the Android ScaleFX suite.
    NSUInteger mismatchedPixels = 0;
    NSUInteger firstMismatch = NSNotFound;
    for (NSUInteger pixel = 0; pixel < width * height; ++pixel) {
        XCTAssertTrue(expected[pixel] == '0' || expected[pixel] == '1');
        const int wanted = expected[pixel] == '1' ? 255 : 0;
        const uint8_t* bgra = &actual[pixel * 4]; // Binary RGB channels are equal.
        const bool mismatch = std::abs(static_cast<int>(bgra[0]) - wanted) > tolerance
            || std::abs(static_cast<int>(bgra[1]) - wanted) > tolerance
            || std::abs(static_cast<int>(bgra[2]) - wanted) > tolerance || bgra[3] != 255;
        if (mismatch) {
            if (firstMismatch == NSNotFound) firstMismatch = pixel;
            ++mismatchedPixels;
        }
    }
    if (mismatchedPixels != 0) {
        const uint8_t* value = &actual[firstMismatch * 4];
        XCTFail(@"%s %s: %lu/%lu pixels differ; first=(%lu,%lu), expected RGB=%d alpha=255, "
                "actual BGRA=(%u,%u,%u,%u), tolerance=%d",
                fixture.name, mmpx ? "MMPX 2x" : "ScaleFX 3x",
                static_cast<unsigned long>(mismatchedPixels), static_cast<unsigned long>(width * height),
                static_cast<unsigned long>(firstMismatch % width), static_cast<unsigned long>(firstMismatch / width),
                expected[firstMismatch] == '1' ? 255 : 0,
                static_cast<unsigned>(value[0]), static_cast<unsigned>(value[1]),
                static_cast<unsigned>(value[2]), static_cast<unsigned>(value[3]), tolerance);
    }
}

- (void)testMmpxDiagonalMatchesIndependentBinaryGolden
{
    [self verifyFixture:flynes::ios::tests::metalBinaryGoldens[0] spatial:FlyNesSpatialMmpx];
}
- (void)testMmpxCheckerboardMatchesIndependentBinaryGolden
{
    [self verifyFixture:flynes::ios::tests::metalBinaryGoldens[1] spatial:FlyNesSpatialMmpx];
}
- (void)testMmpxOddThinLineMatchesIndependentBinaryGolden
{
    [self verifyFixture:flynes::ios::tests::metalBinaryGoldens[2] spatial:FlyNesSpatialMmpx];
}
- (void)testScaleFxDiagonalMatchesIndependentBinaryGolden
{
    [self verifyFixture:flynes::ios::tests::metalBinaryGoldens[0] spatial:FlyNesSpatialScalefx];
}
- (void)testScaleFxCheckerboardMatchesIndependentBinaryGolden
{
    [self verifyFixture:flynes::ios::tests::metalBinaryGoldens[1] spatial:FlyNesSpatialScalefx];
}
- (void)testScaleFxOddThinLineMatchesIndependentBinaryGolden
{
    [self verifyFixture:flynes::ios::tests::metalBinaryGoldens[2] spatial:FlyNesSpatialScalefx];
}
@end
