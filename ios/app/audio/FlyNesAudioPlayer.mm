#import "FlyNesAudioPlayer.h"
#import <AVFoundation/AVFoundation.h>
#include "PlaybackAudioQueue.hpp"

@implementation FlyNesAudioPlayer {
    AVAudioEngine *engine_;
    AVAudioPlayerNode *player_;
    AVAudioFormat *format_;
    flynes::ios::PlaybackAudioQueue queue_;
    BOOL active_;
}
- (instancetype)init {
    self = [super init];
    if (self) {
        _enabled = YES;
        _focusPolicy = 1;
        engine_ = [[AVAudioEngine alloc] init];
        player_ = [[AVAudioPlayerNode alloc] init];
        format_ = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000 channels:1];
        [engine_ attachNode:player_];
        [engine_ connect:player_ to:engine_.mainMixerNode format:format_];
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(configurationChanged:)
            name:AVAudioEngineConfigurationChangeNotification object:engine_];
    }
    return self;
}
- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [player_ stop];
    [engine_ stop];
}
- (BOOL)start:(NSError **)error {
    NSAssert(NSThread.isMainThread, @"Audio player is main-thread owned");
    if (!self.enabled) return YES;
    if (active_ && engine_.running) return YES;
    AVAudioSession *session = AVAudioSession.sharedInstance;
    AVAudioSessionCategoryOptions options = self.focusPolicy == 3
        ? AVAudioSessionCategoryOptionMixWithOthers
        : (self.focusPolicy == 2 ? AVAudioSessionCategoryOptionDuckOthers : 0);
    if (![session setCategory:AVAudioSessionCategoryPlayback mode:AVAudioSessionModeDefault
                     options:options error:error]) return NO;
    [session setPreferredSampleRate:48000 error:nil];
    [session setPreferredIOBufferDuration:0.01 error:nil];
    if (![session setActive:YES error:error]) return NO;
    [engine_ prepare];
    active_ = [engine_ startAndReturnError:error];
    if (!active_) [session setActive:NO withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:nil];
    return active_;
}
- (void)pause {
    NSAssert(NSThread.isMainThread, @"Audio player is main-thread owned");
    active_ = NO;
    [self flush];
    [engine_ pause];
    [AVAudioSession.sharedInstance setActive:NO
        withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:nil];
}
- (void)flush {
    NSAssert(NSThread.isMainThread, @"Audio player is main-thread owned");
    queue_.flush();
    [player_ stop];
    [player_ reset];
}
- (void)setEnabled:(BOOL)enabled {
    if (_enabled == enabled) return;
    _enabled = enabled;
    if (!enabled) [self pause];
}
- (void)setFocusPolicy:(NSUInteger)policy {
    if (_focusPolicy == policy) return;
    const BOOL restart = active_;
    [self pause];
    _focusPolicy = policy;
    if (restart) [self start:nil];
}
- (void)enqueuePCM:(NSData *)samples {
    NSAssert(NSThread.isMainThread, @"Audio player is main-thread owned");
    if (!active_ || !self.enabled || samples.length == 0 || samples.length % sizeof(int16_t)) return;
    const auto count = static_cast<AVAudioFrameCount>(samples.length / sizeof(int16_t));
    if (count > 4096 || !queue_.reserve()) return; // bounded latency if output stalls
    AVAudioPCMBuffer *buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format_ frameCapacity:count];
    if (!buffer) { queue_.finish(queue_.generation()); return; }
    buffer.frameLength = count;
    const int16_t *source = static_cast<const int16_t *>(samples.bytes);
    for (AVAudioFrameCount i = 0; i < count; ++i) buffer.floatChannelData[0][i] = source[i] / 32768.0f;
    const auto generation = queue_.generation();
    __weak FlyNesAudioPlayer *weakSelf = self;
    [player_ scheduleBuffer:buffer completionCallbackType:AVAudioPlayerNodeCompletionDataPlayedBack
        completionHandler:^(AVAudioPlayerNodeCompletionCallbackType type) {
            (void)type;
            dispatch_async(dispatch_get_main_queue(), ^{
                FlyNesAudioPlayer *strong = weakSelf;
                if (strong) strong->queue_.finish(generation);
            });
        }];
    // One frame of pre-roll absorbs alternating 60/120 Hz callback intervals.
    if (!player_.playing && queue_.count() >= 2) [player_ play];
}
- (void)configurationChanged:(NSNotification *)notification {
    (void)notification;
    __weak FlyNesAudioPlayer *weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        FlyNesAudioPlayer *strong = weakSelf;
        if (!strong) return;
        const BOOL restart = strong->active_;
        [strong pause];
        if (restart) [strong start:nil];
    });
}
@end
