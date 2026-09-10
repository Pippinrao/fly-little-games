// Sustained-playback budget: drives the real runtime for a long uninterrupted span
// and asserts that frames keep advancing, audio stays bounded, covers are only
// sampled at the Android offsets, and the process footprint stays flat.
#import <XCTest/XCTest.h>
#import "FlyNesRuntimeBridge.h"
#import "../app/bridge/FlyNesAppBridge.h"
#import "../app/platform/CoverCapturePolicy.hpp"
#import "../app/platform/GameCoverPolicy.hpp"
#include <mach/mach.h>
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace {

/// Resident bytes of the current task; the soak asserts the run does not grow.
size_t resident_bytes()
{
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t status = task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                                           reinterpret_cast<task_info_t>(&info), &count);
    return status == KERN_SUCCESS ? info.resident_size : 0u;
}

size_t every_sample_frame(const std::vector<uint64_t>& samples)
{
    return samples.size();
}

NSString *describe(const std::vector<uint64_t>& values)
{
    NSMutableArray<NSString *> *items = [NSMutableArray array];
    for (uint64_t value : values) [items addObject:[NSString stringWithFormat:@"%llu", value]];
    return [items componentsJoinedByString:@","];
}

} // namespace

@interface PlaybackSoakTests : XCTestCase
@end

@implementation PlaybackSoakTests

/// Runs a bounded soak and reports measured numbers. The default span is ~1 minute of
/// emulated play at maximum speed; set FLYNES_SOAK_FRAMES to cover a longer budget.
- (void)testSustainedPlaybackKeepsFramesAudioAndMemoryBounded
{
    const char *requested = getenv("FLYNES_SOAK_FRAMES");
    const uint64_t frames = requested != nullptr ? strtoull(requested, nullptr, 10) : 36000ull;

    FlyNesRuntimeBridge *runtime = [[FlyNesRuntimeBridge alloc] init];
    XCTAssertTrue([runtime createRuntime:nil]);
    NSData *rom = [NSData dataWithContentsOfURL:
        [NSBundle.mainBundle URLForResource:@"from_below" withExtension:@"nes"]];
    XCTAssertTrue([runtime loadRom:rom error:nil]);

    flynes::ios::CoverCaptureSession covers;
    std::vector<uint64_t> sample_frames;
    std::vector<uint64_t> sample_sequences;
    uint64_t total_samples = 0;
    uint64_t sequences_seen = 0;
    uint64_t previous_sequence = 0;
    uint64_t first_sequence = 0;
    size_t peak_resident = 0;
    size_t baseline_resident = 0;

    for (uint64_t frame = 0; frame < frames; ++frame)
    {
        // XCTest only wraps a whole test method in an autorelease pool, so a tight
        // frame loop must drain its own; otherwise the harness appears to leak.
        @autoreleasepool
        {
            // No input at all: Android keeps producing frames from the display clock.
            NSError *error = nil;
            XCTAssertTrue([runtime stepFrameWithButtons:0 error:&error], @"frame %llu: %@", frame, error);
            NSData *pcm = [runtime pullPCM];
            total_samples += pcm.length / 2;
            // A bounded queue means the drawn window never lags the produced one.
            XCTAssertLessThanOrEqual(pcm.length / 2, (NSUInteger)(4096));

            uint64_t sequence = 0;
            uint32_t width = 0, height = 0;
            NSData *pixels = [runtime copyLatestRgb565FrameWithSequence:&sequence width:&width height:&height];
            XCTAssertEqual(pixels.length, 256u * 240u * 2u);
            XCTAssertEqual(width, 256u);
            XCTAssertEqual(height, 240u);
            XCTAssertGreaterThan(sequence, previous_sequence, @"frame sequence must advance");
            previous_sequence = sequence;
            if (frame == 0u) first_sequence = sequence;
            sequences_seen += 1;
            if (covers.note_frame(sequence)) {
                sample_frames.push_back(frame);
                sample_sequences.push_back(sequence);
            }
        }

        if (frame == 60u) baseline_resident = resident_bytes();
        if (frame % 3000u == 0u)
            peak_resident = std::max(peak_resident, resident_bytes());
    }
    peak_resident = std::max(peak_resident, resident_bytes());
    if (getenv("FLYNES_SOAK_VERBOSE") != nullptr)
        NSLog(@"soak diagnostics: first_sequence=%llu sample_frames=%s sample_sequences=%s",
              first_sequence, describe(sample_frames).UTF8String,
              describe(sample_sequences).UTF8String);

    // Every produced frame was observed, so the cover pipeline cannot silently skip work.
    XCTAssertEqual(sequences_seen, frames);
    XCTAssertEqual(every_sample_frame(sample_sequences), 4u,
                   @"cover sampling must take exactly the four Android offsets");
    XCTAssertEqual(sample_frames, (std::vector<uint64_t>{120u, 240u, 360u, 480u}));
    XCTAssertEqual(sample_sequences.front(), first_sequence + 120u);
    XCTAssertEqual(sample_sequences[1], first_sequence + 240u);
    XCTAssertEqual(sample_sequences[2], first_sequence + 360u);
    XCTAssertEqual(sample_sequences[3], first_sequence + 480u);

    // ~735 samples per frame at the NTSC rate; allow a small rounding window.
    const double expected = static_cast<double>(frames) * 48000.0 / 60.0988;
    XCTAssertEqualWithAccuracy(static_cast<double>(total_samples), expected,
                               std::max(64.0, expected * 0.01));

    if (baseline_resident > 0u && peak_resident > baseline_resident)
    {
        // A leaked frame buffer or audio queue would show up as unbounded growth.
        const size_t growth = peak_resident - baseline_resident;
        XCTAssertLessThan(growth, 64u * 1024u * 1024u,
                          @"resident memory grew by %zu bytes over %llu frames", growth, frames);
    }
    printf("soak: frames=%llu samples=%llu audio=%llu peak_resident=%zu\n",
           frames, sequences_seen, total_samples, peak_resident);
    // Older Xcode test runners drop stdout, so the measured numbers are also written
    // where the operator can read them from the simulator container.
    NSString *report = [NSString stringWithFormat:
        @"soak frames=%llu samples=%llu audio=%llu peak_resident=%zu\n",
        frames, sequences_seen, total_samples, peak_resident];
    NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                            inDomains:NSUserDomainMask].firstObject;
    [report writeToURL:[documents URLByAppendingPathComponent:@"flynes-ios-soak.txt"]
            atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

/// Opening the same game repeatedly must not leak the app handle or its catalog.
- (void)testRepeatedPauseAndResumeCyclesKeepCatalogStable
{
    NSString *root = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    FlyNesAppBridge *app = [[FlyNesAppBridge alloc] init];
    XCTAssertTrue([app createWithDataRoot:root cacheRoot:root error:nil]);
    FlyNesRuntimeBridge *runtime = [[FlyNesRuntimeBridge alloc] init];
    XCTAssertTrue([runtime createRuntime:nil]);
    NSData *rom = [NSData dataWithContentsOfURL:
        [NSBundle.mainBundle URLForResource:@"from_below" withExtension:@"nes"]];
    XCTAssertTrue([runtime loadRom:rom error:nil]);

    NSUInteger baseline = [app catalogSnapshotGames].count;
    for (int cycle = 0; cycle < 30; ++cycle)
    {
        for (int frame = 0; frame < 120; ++frame)
            XCTAssertTrue([runtime stepFrameWithButtons:0 error:nil]);
        // Pause stores a checkpoint and drops queued audio, like the pause drawer.
        NSData *checkpoint = [runtime saveCheckpoint:nil];
        XCTAssertNotNil(checkpoint);
        [runtime discardAudio];
        XCTAssertTrue([runtime loadCheckpoint:checkpoint error:nil]);
        [runtime clearInput];
        XCTAssertEqual([runtime pullPCM].length, 0u);
        XCTAssertTrue([runtime stepFrameWithButtons:0 error:nil]);
    }
    XCTAssertEqual([app catalogSnapshotGames].count, baseline,
                   @"pause cycles must not invent or drop catalog rows");
    [NSFileManager.defaultManager removeItemAtPath:root error:nil];
}

@end
