// Run in an iOS XCTest target linked with FlyNesRuntimeBridge and flynes_runtime.
#import <XCTest/XCTest.h>
#import "FlyNesRuntimeBridge.h"
#include <flynes/flynes_runtime.h>
#include <vector>

@interface PlaybackRuntimeBridgeTests : XCTestCase
@end
@implementation PlaybackRuntimeBridgeTests
- (FlyNesRuntimeBridge *)loadedRuntime {
    FlyNesRuntimeBridge *runtime = [[FlyNesRuntimeBridge alloc] init];
    XCTAssertTrue([runtime createRuntime:nil]);
    NSURL *url = [NSBundle.mainBundle URLForResource:@"thwaite" withExtension:@"nes"];
    NSData *rom = [NSData dataWithContentsOfURL:url];
    XCTAssertNotNil(rom);
    XCTAssertTrue([runtime loadRom:rom error:nil]);
    return runtime;
}
- (void)testFramesProduceVideoAndBoundedPCMWithoutInputChanges {
    FlyNesRuntimeBridge *runtime = [self loadedRuntime];
    NSUInteger total = 0;
    for (int i = 0; i < 120; ++i) {
        XCTAssertTrue([runtime stepFrameWithButtons:0 error:nil]);
        NSData *pcm = [runtime pullPCM];
        XCTAssertEqual(pcm.length / 2, runtime.lastFrameSampleCount);
        total += pcm.length / 2;
        XCTAssertEqual([runtime pullPCM].length, 0u);
    }
    XCTAssertEqualWithAccuracy(total, 120.0 * 48000 / 60.0988, 2);
    XCTAssertEqual([runtime copyLatestRgb565Frame].length, 256u * 240u * 2u);
}
- (void)testCheckpointBeforeFirstFrameCanResume {
    FlyNesRuntimeBridge *runtime = [self loadedRuntime];
    NSData *checkpoint = [runtime saveCheckpoint:nil];
    XCTAssertNotNil(checkpoint);
    XCTAssertTrue([runtime stepFrameWithButtons:1 error:nil]);
    XCTAssertTrue([runtime loadCheckpoint:checkpoint error:nil]);
    XCTAssertEqual([runtime pullPCM].length, 0u);
    XCTAssertTrue([runtime stepFrameWithButtons:0 error:nil]);
}
- (void)testCheckpointRestoresStableNextFrameAndClearsAudio {
    FlyNesRuntimeBridge *runtime = [self loadedRuntime];
    for (int i = 0; i < 90; ++i) XCTAssertTrue([runtime stepFrameWithButtons:0 error:nil]);
    [runtime clearInput];
    NSData *checkpoint = [runtime saveCheckpoint:nil];
    XCTAssertTrue([runtime stepFrameWithButtons:0 error:nil]);
    NSData *expected = [runtime copyLatestRgb565Frame];
    XCTAssertTrue([runtime loadCheckpoint:checkpoint error:nil]);
    XCTAssertEqual([runtime pullPCM].length, 0u);
    XCTAssertTrue([runtime stepFrameWithButtons:0 error:nil]);
    XCTAssertEqualObjects(expected, [runtime copyLatestRgb565Frame]);
    [runtime destroyRuntime];
    XCTAssertFalse([runtime stepFrameWithButtons:0 error:nil]);
}
- (void)testUnconsumedAudioCannotAccumulateAcrossFrames {
    FlyNesRuntimeBridge *runtime = [self loadedRuntime];
    for (int i = 0; i < 100; ++i) XCTAssertTrue([runtime stepFrameWithButtons:0 error:nil]);
    XCTAssertEqual([runtime pullPCM].length / 2, runtime.lastFrameSampleCount);
    XCTAssertEqual([runtime pullPCM].length, 0u);
}
- (void)testCheckpointRestoresAnExternalTimelineEpoch {
    FlyNesRuntimeBridge *runtime = [self loadedRuntime];
    NSData *rom = [NSData dataWithContentsOfURL:[NSBundle.mainBundle URLForResource:@"thwaite" withExtension:@"nes"]];
    fly_runtime_config config{};
    config.struct_size = FLY_RUNTIME_CONFIG_V1_SIZE;
    config.version = FLY_RUNTIME_CONFIG_VERSION_1;
    fly_runtime_t *external = nullptr;
    XCTAssertEqual(fly_runtime_create(&config, &external), FLY_RESULT_OK);
    XCTAssertEqual(fly_runtime_load_rom(external, static_cast<const uint8_t *>(rom.bytes), rom.length, nullptr), FLY_RESULT_OK);
    fly_frame_input_v1 input{};
    input.struct_size = FLY_FRAME_INPUT_V1_SIZE;
    input.version = FLY_FRAME_INPUT_VERSION_1;
    input.timeline_epoch = 7;
    fly_frame_result_v1 result{};
    result.struct_size = FLY_FRAME_RESULT_V1_SIZE;
    result.version = FLY_FRAME_RESULT_VERSION_1;
    XCTAssertEqual(fly_runtime_step_frame(external, &input, &result), FLY_RESULT_OK);
    size_t written = 0, needed = 0;
    XCTAssertEqual(fly_runtime_save_checkpoint(external, nullptr, 0, &written, &needed), FLY_RESULT_BUFFER_TOO_SMALL);
    std::vector<uint8_t> bytes(needed);
    XCTAssertEqual(fly_runtime_save_checkpoint(external, bytes.data(), bytes.size(), &written, &needed), FLY_RESULT_OK);
    fly_runtime_destroy(external);
    XCTAssertTrue([runtime loadCheckpoint:[NSData dataWithBytes:bytes.data() length:written] error:nil]);
    XCTAssertTrue([runtime stepFrameWithButtons:0 error:nil]);
}
@end
