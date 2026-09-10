#import "FlyNesAudioPlayer.h"
#import <AVFoundation/AVFoundation.h>
#include "PlaybackAudioQueue.hpp"
#include "PlaybackAudioFocus.hpp"

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
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(secondaryAudioHintChanged:)
            name:AVAudioSessionSilenceSecondaryAudioHintNotification object:nil];
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
    if (active_ && engine_.running) {
        [self updateGameVolume];
        return YES;
    }
    AVAudioSession *session = AVAudioSession.sharedInstance;
    // DUCK lowers this game's gain, not the volume of other audio sessions.
    const auto focus = flynes::ios::playbackAudioFocus(static_cast<unsigned>(self.focusPolicy),
        session.secondaryAudioShouldBeSilencedHint, session.otherAudioPlaying);
    AVAudioSessionCategoryOptions options = focus.mixWithOthers ? AVAudioSessionCategoryOptionMixWithOthers : 0;
    if (![session setCategory:AVAudioSessionCategoryPlayback mode:AVAudioSessionModeDefault
                     options:options error:error]) return NO;
    [session setPreferredSampleRate:48000 error:nil];
    [session setPreferredIOBufferDuration:0.01 error:nil];
    if (![session setActive:YES error:error]) return NO;
    [self updateGameVolume];
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
    [self updateGameVolume];
    if (restart) [self start:nil];
}
- (void)enqueuePCM:(NSData *)samples {
    NSAssert(NSThread.isMainThread, @"Audio player is main-thread owned");
    if (!active_ || !self.enabled || samples.length == 0 || samples.length % sizeof(int16_t)) return;
    // The hint covers nonmixable sessions. Polling also catches mixable audio,
    // which may start/stop without a silence-secondary-audio notification.
    [self updateGameVolume];
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
- (void)updateGameVolume {
    NSAssert(NSThread.isMainThread, @"Audio focus is main-thread owned");
    [self updateGameVolumeWithSecondaryHint:AVAudioSession.sharedInstance.secondaryAudioShouldBeSilencedHint];
}
- (void)updateGameVolumeWithSecondaryHint:(BOOL)hint {
    const auto focus = flynes::ios::playbackAudioFocus(static_cast<unsigned>(self.focusPolicy), hint,
        AVAudioSession.sharedInstance.otherAudioPlaying);
    player_.volume = focus.gameVolume;
}
- (void)secondaryAudioHintChanged:(NSNotification *)notification {
    const BOOL begin = [notification.userInfo[AVAudioSessionSilenceSecondaryAudioHintTypeKey] unsignedIntegerValue]
        == AVAudioSessionSilenceSecondaryAudioHintTypeBegin;
    __weak FlyNesAudioPlayer *weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        [weakSelf updateGameVolumeWithSecondaryHint:begin];
    });
}
@end
