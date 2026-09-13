#import <XCTest/XCTest.h>
#import <AVFoundation/AVFoundation.h>
#import "../app/audio/FlyNesAudioPlayer.h"
#import "FlyNesRuntimeBridge.h"
#include <atomic>
#include <memory>

@interface PlaybackAudioPlayerTests : XCTestCase
@end
@implementation PlaybackAudioPlayerTests
- (void)testRealGamePCMReachesRunningAudioOutput {
    XCTAssertTrue(NSThread.isMainThread);
    FlyNesRuntimeBridge *runtime = [[FlyNesRuntimeBridge alloc] init];
    XCTAssertTrue([runtime createRuntime:nil]);
    NSData *rom = [NSData dataWithContentsOfURL:[NSBundle.mainBundle URLForResource:@"thwaite" withExtension:@"nes"]];
    XCTAssertTrue([runtime loadRom:rom error:nil]);
    FlyNesAudioPlayer *audio = [[FlyNesAudioPlayer alloc] init];
    // Observe the actual AVAudioEngine mixer and player clock, rather than
    // treating a nonempty emulator PCM array as proof of playback.
    AVAudioEngine *engine = [audio valueForKey:@"engine_"];
    AVAudioPlayerNode *player = [audio valueForKey:@"player_"];
    auto nonSilent = std::make_shared<std::atomic<bool>>(false);
    [engine.mainMixerNode installTapOnBus:0 bufferSize:1024 format:nil block:^(AVAudioPCMBuffer *buffer, AVAudioTime *when) {
        (void)when;
        for (AVAudioChannelCount channel = 0; channel < buffer.format.channelCount; ++channel) {
            for (AVAudioFrameCount frame = 0; frame < buffer.frameLength; ++frame) {
                if (fabsf(buffer.floatChannelData[channel][frame]) > 0.0001f) nonSilent->store(true);
            }
        }
    }];
    NSError *failure = nil;
    XCTAssertTrue([audio start:&failure], @"%@", failure);
    XCTestExpectation *played = [self expectationWithDescription:@"Four seconds of game audio output"];
    __block NSUInteger frames = 0;
    NSTimer *timer = [NSTimer scheduledTimerWithTimeInterval:1.0/60 repeats:YES block:^(NSTimer *tick) {
        NSError *stepError = nil;
        XCTAssertTrue([runtime stepFrameWithButtons:(frames >= 60 && frames < 65 ? 8 : 0) error:&stepError], @"%@", stepError);
        [audio enqueuePCM:[runtime pullPCM]];
        if (++frames >= 240) { [tick invalidate]; [played fulfill]; }
    }];
    [self waitForExpectations:@[played] timeout:15];
    [timer invalidate];
    AVAudioTime *nodeTime = player.lastRenderTime;
    AVAudioTime *playerTime = nodeTime ? [player playerTimeForNodeTime:nodeTime] : nil;
    XCTAssertTrue(engine.running);
    XCTAssertTrue(player.playing);
    XCTAssertGreaterThan(playerTime.sampleTime, 48000);
    XCTAssertTrue(nonSilent->load(), @"Game PCM must produce nonzero samples at the output mixer");
    [engine.mainMixerNode removeTapOnBus:0];
    [audio pause];
    XCTAssertFalse(player.playing);
}
@end
