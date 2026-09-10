#import "RunSurfaceViewController.h"

#import "FlyNesAppBridge.h"
#import "FlyNesRuntimeBridge.h"
#import "GamepadOverlayView.h"
#import "FlyNesMetalRenderer.h"
#import "FlyNesDisplayLinkPacer.h"
#import "FlyNesAudioPlayer.h"
#import "AppLocalization.h"
#import <AVFoundation/AVFoundation.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "flynes/product/pause_actions.hpp"
#include "PlaybackClock.hpp"
#include "FrameInputLatch.hpp"

namespace {

NSString *pause_command_id(flynes::product::PauseCommand command)
{
    using flynes::product::PauseCommand;
    switch (command)
    {
    case PauseCommand::Resume:
        return @"resume";
    case PauseCommand::GameCenter:
        return @"game_center";
    case PauseCommand::Settings:
        return @"settings";
    }
    return @"resume";
}

NSString *pause_command_title(flynes::product::PauseCommand command)
{
    using flynes::product::PauseCommand;
    switch (command)
    {
    case PauseCommand::Resume:
        return FlyNesLocalizedString(@"pause.resume");
    case PauseCommand::GameCenter:
        return FlyNesLocalizedString(@"pause.game_center");
    case PauseCommand::Settings:
        return FlyNesLocalizedString(@"pause.settings");
    }
    return FlyNesLocalizedString(@"pause.resume");
}

} // namespace

@implementation RunSurfaceViewController {
    UIView *metalHost_;
    GamepadOverlayView *overlay_;
    UIButton *pauseButton_;
    UIView *pauseLayer_;
    FlyNesRuntimeBridge *runtime_;
    FlyNesMetalRenderer *renderer_;
    FlyNesDisplayLinkPacer *pacer_;
    FlyNesAudioPlayer *audio_;
    CAMetalLayer *metalLayer_;
    flynes::ios::PlaybackClock clock_;
    uint32_t buttons_;
    flynes::ios::FrameInputLatch input_;
    BOOL visible_;
    BOOL foreground_;
    BOOL running_;
    BOOL audioInterrupted_;
    BOOL paused_;
    BOOL drawerOpen_;
    BOOL checkpointFailed_;
    BOOL romReady_;
    BOOL videoFailureShown_;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    self.view.multipleTouchEnabled = YES;
    paused_ = NO;
    drawerOpen_ = NO;
    checkpointFailed_ = NO;
    romReady_ = NO;
    foreground_ = UIApplication.sharedApplication.applicationState == UIApplicationStateActive;

    metalHost_ = [[UIView alloc] initWithFrame:self.view.bounds];
    metalHost_.translatesAutoresizingMaskIntoConstraints = NO;
    metalHost_.backgroundColor = UIColor.blackColor;
    CAMetalLayer *layer = [CAMetalLayer layer];
    metalLayer_ = layer;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    metalHost_.layer.sublayers = @[ layer ];
    [self.view addSubview:metalHost_];
    renderer_ = [[FlyNesMetalRenderer alloc] initWithLayer:layer];
    audio_ = [[FlyNesAudioPlayer alloc] init];
    pacer_ = [[FlyNesDisplayLinkPacer alloc] init];

    overlay_ = [[GamepadOverlayView alloc] initWithFrame:self.view.bounds];
    overlay_.translatesAutoresizingMaskIntoConstraints = NO;
    __weak __typeof__(self) weakSelf = self;
    pacer_.onTick = ^(CFTimeInterval timestamp, CFTimeInterval presentedTime) {
        (void)presentedTime;
        [weakSelf displayTick:timestamp];
    };
    overlay_.buttonsChanged = ^(uint32_t buttons) {
      RunSurfaceViewController *strong = weakSelf;
      if (strong == nil)
          return;
      [strong applyOverlayButtons:buttons];
    };
    overlay_.buttonsReleased = ^(uint32_t buttons, NSTimeInterval downTime, NSTimeInterval upTime) {
        RunSurfaceViewController *strong = weakSelf;
        if (strong && strong->running_) strong->input_.release(buttons,downTime,upTime);
    };
    overlay_.buttonsCancelled = ^{ RunSurfaceViewController *strong = weakSelf;
        if (strong) strong->input_.clear(); };
    [self.view addSubview:overlay_];

    pauseButton_ = [UIButton buttonWithType:UIButtonTypeSystem];
    pauseButton_.translatesAutoresizingMaskIntoConstraints = NO;
    [pauseButton_ setTitle:@"II" forState:UIControlStateNormal];
    [pauseButton_ setTitleColor:[UIColor colorWithRed:0.95 green:0.94 blue:0.90 alpha:1.0]
                       forState:UIControlStateNormal];
    pauseButton_.backgroundColor = [UIColor colorWithWhite:0.11 alpha:0.72];
    pauseButton_.layer.cornerRadius = 24.0;
    pauseButton_.layer.borderWidth = 2.0;
    pauseButton_.layer.borderColor = [UIColor colorWithRed:1.0 green:0.42 blue:0.37 alpha:1.0].CGColor;
    pauseButton_.accessibilityIdentifier = @"OPEN_PAUSE";
    pauseButton_.accessibilityLabel = FlyNesLocalizedString(@"run.pause");
    [pauseButton_ addTarget:self action:@selector(openPauseDrawer) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:pauseButton_];

    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [metalHost_.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [metalHost_.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [metalHost_.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [metalHost_.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],
        [overlay_.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [overlay_.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [overlay_.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [overlay_.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [pauseButton_.topAnchor constraintEqualToAnchor:safe.topAnchor constant:16.0],
        [pauseButton_.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-16.0],
        [pauseButton_.widthAnchor constraintEqualToConstant:48.0],
        [pauseButton_.heightAnchor constraintEqualToConstant:48.0],
    ]];

    runtime_ = [[FlyNesRuntimeBridge alloc] init];
    [runtime_ createRuntime:nil];
    NSData *rom = self.romData;
    if (rom.length == 0 && [self canonicalIdMapsToBuiltinFromBelow])
        rom = [self bundledFromBelowRom];
    self.romData = rom;
    NSError *romError = nil;
    if (rom.length > 0)
        romReady_ = [runtime_ loadRom:rom error:&romError];
    if (romReady_) {
        [FlyNesAppBridge.sharedInstance markPlayedCanonicalID:self.canonicalId error:nil];
        [self restoreAutosave];
    } else
        [self surfaceRomOpenFailure];
    [self reloadProductSettings];
    NSNotificationCenter *notifications = NSNotificationCenter.defaultCenter;
    [notifications addObserver:self selector:@selector(applicationWillResignActive:)
                          name:UIApplicationWillResignActiveNotification object:nil];
    [notifications addObserver:self selector:@selector(applicationDidBecomeActive:)
                          name:UIApplicationDidBecomeActiveNotification object:nil];
    [notifications addObserver:self selector:@selector(audioInterruption:)
                          name:AVAudioSessionInterruptionNotification object:nil];
    [notifications addObserver:self selector:@selector(audioRouteChanged:)
                          name:AVAudioSessionRouteChangeNotification object:nil];
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [pacer_ invalidate];
    [audio_ pause];
    [runtime_ destroyRuntime];
}

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
    visible_ = YES;
    [self drawFrame];
    foreground_ = UIApplication.sharedApplication.applicationState == UIApplicationStateActive;
    [self updatePlayback];
}

- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
    visible_ = NO;
    [self stopPlayback];
    [self saveAutosaveIfEnabled];
}

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    [self reloadProductSettings];
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    const UIEdgeInsets insets = self.view.safeAreaInsets;
    overlay_.layoutMargins = insets;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    metalLayer_.frame = metalHost_.bounds;
    CGFloat scale = self.view.window.screen.scale > 0 ? self.view.window.screen.scale : UIScreen.mainScreen.scale;
    metalLayer_.contentsScale = scale;
    metalLayer_.drawableSize = CGSizeMake(metalHost_.bounds.size.width * scale, metalHost_.bounds.size.height * scale);
    [CATransaction commit];
    [self drawFrame];
}

- (void)reloadProductSettings
{
    NSAssert(NSThread.isMainThread, @"Playback settings are main-thread owned");
    NSDictionary<NSString *, id> *snapshot = FlyNesAppBridge.sharedInstance.settingsGet;
    NSNumber *direction = snapshot[@"direction_mode"];
    NSNumber *haptic = snapshot[@"haptic_level"];
    NSNumber *dead = snapshot[@"dead_zone"];
    if (direction != nil)
        overlay_.joystickMode = static_cast<FlyNesJoystickMode>(direction.unsignedIntValue);
    if (haptic != nil)
        overlay_.hapticLevel = haptic.unsignedIntValue;
    overlay_.distinctAbHaptics = [snapshot[@"distinct_ab_haptics"] boolValue];
    if (dead != nil)
        overlay_.deadZone = dead.floatValue;
    NSNumber *opacity = snapshot[@"control_opacity"];
    if (opacity) overlay_.controlOpacity = opacity.floatValue;
    overlay_.layoutUtf8 = FlyNesAppBridge.sharedInstance.controlLayoutGet;
    const NSUInteger preset = [snapshot[@"video_quality_preset"] unsignedIntegerValue];
    FlyNesSpatialMode spatial = preset == 1 ? FlyNesSpatialNearest : FlyNesSpatialSharpBilinear;
    FlyNesPostEffect post = FlyNesPostNone;
    if (preset == 4) {
        spatial = static_cast<FlyNesSpatialMode>([snapshot[@"custom_spatial_mode"] integerValue]);
        post = static_cast<FlyNesPostEffect>([snapshot[@"custom_post_effect"] integerValue]);
    }
    [renderer_ setSpatialMode:spatial postEffect:post];
    [renderer_ setAspectMode:static_cast<FlyNesAspectMode>([snapshot[@"aspect_mode"] integerValue])];
    audio_.enabled = snapshot[@"audio_enabled"] == nil || [snapshot[@"audio_enabled"] boolValue];
    audio_.focusPolicy = snapshot[@"audio_focus_policy"] ? [snapshot[@"audio_focus_policy"] unsignedIntegerValue] : 1;
    [self updatePlayback];
}

- (NSData *)bundledFromBelowRom
{
    NSString *path = [NSBundle.mainBundle pathForResource:@"from_below" ofType:@"nes"];
    if (path == nil)
        return nil;
    return [NSData dataWithContentsOfFile:path];
}

- (BOOL)canonicalIdMapsToBuiltinFromBelow
{
    NSString *cid = self.canonicalId.lowercaseString;
    if (cid.length == 0)
        return NO;
    return [cid isEqualToString:@"builtin"] || [cid containsString:@"from_below"]
        || [cid containsString:@"from-below"];
}

- (NSURL *)autosaveURL
{
    if (self.canonicalId.length == 0)
        return nil;
    NSString *safe = [[self.canonicalId stringByReplacingOccurrencesOfString:@"/" withString:@"_"]
        stringByReplacingOccurrencesOfString:@":"
                                  withString:@"_"];
    safe = [safe stringByReplacingOccurrencesOfString:@"\\" withString:@"_"];
    if (safe.length == 0)
        return nil;
    NSFileManager *files = NSFileManager.defaultManager;
    NSURL *documents =
        [files URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    if (documents.path == nil)
        return nil;
    return [[[documents URLByAppendingPathComponent:@"saves"] URLByAppendingPathComponent:safe]
        URLByAppendingPathComponent:@"autosave.nst"];
}

- (BOOL)persistAutosave:(NSData *)blob
{
    if (blob.length == 0)
        return NO;
    NSURL *url = [self autosaveURL];
    if (url == nil)
        return NO;
    NSError *error = nil;
    NSURL *directory = url.URLByDeletingLastPathComponent;
    if (![NSFileManager.defaultManager createDirectoryAtURL:directory
                                withIntermediateDirectories:YES
                                                 attributes:nil
                                                      error:&error])
        return NO;
    return [blob writeToURL:url options:NSDataWritingAtomic error:&error];
}

- (void)restoreAutosave
{
    if (!romReady_)
        return;
    NSDictionary<NSString *, id> *snapshot = FlyNesAppBridge.sharedInstance.settingsGet;
    NSNumber *enabled = snapshot[@"autosave_enabled"];
    if (enabled != nil && enabled.unsignedIntValue == 0)
        return;
    NSURL *url = [self autosaveURL];
    if (url == nil)
        return;
    NSData *blob = [NSData dataWithContentsOfURL:url];
    if (blob.length == 0)
        return;
    checkpointFailed_ = ![self restoreCheckpoint:blob error:nil];
}

- (void)surfaceRomOpenFailure
{
    NSString *message = FlyNesLocalizedString(@"library.rom_open_failed");
    UIAlertController *alert =
        [UIAlertController alertControllerWithTitle:nil
                                            message:message
                                     preferredStyle:UIAlertControllerStyleAlert];
    alert.view.accessibilityIdentifier = @"library_rom_open_failed";
    __weak __typeof__(self) weakSelf = self;
    [alert addAction:[UIAlertAction actionWithTitle:FlyNesLocalizedString(@"pause.game_center")
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *_Nonnull action) {
                                              (void)action;
                                              RunSurfaceViewController *strong = weakSelf;
                                              if (strong == nil || strong.onPauseCommand == nil)
                                                  return;
                                              strong.onPauseCommand(@"game_center");
                                            }]];
    dispatch_async(dispatch_get_main_queue(), ^{
      [self presentViewController:alert animated:YES completion:nil];
    });
}

- (void)applyOverlayButtons:(uint32_t)buttons
{
    buttons_ = running_ ? buttons : 0;
    input_.update(buttons_);
}

- (void)openPauseDrawer
{
    if (drawerOpen_)
        return;
    paused_ = YES;
    drawerOpen_ = YES;
    [self stopPlayback];
    overlay_.hidden = YES;
    pauseButton_.hidden = YES;
    [self saveAutosaveIfEnabled];

    pauseLayer_ = [[UIView alloc] initWithFrame:self.view.bounds];
    pauseLayer_.translatesAutoresizingMaskIntoConstraints = NO;
    pauseLayer_.backgroundColor = UIColor.clearColor;

    UIView *scrim = [[UIView alloc] init];
    scrim.translatesAutoresizingMaskIntoConstraints = NO;
    scrim.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.54];
    UITapGestureRecognizer *scrimTap =
        [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(resumeFromPause)];
    [scrim addGestureRecognizer:scrimTap];

    UIView *drawer = [[UIView alloc] init];
    drawer.translatesAutoresizingMaskIntoConstraints = NO;
    drawer.backgroundColor = [UIColor colorWithRed:0.11 green:0.11 blue:0.13 alpha:1.0];
    [pauseLayer_ addSubview:scrim];
    [pauseLayer_ addSubview:drawer];

    UIStackView *stack = [[UIStackView alloc] init];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 12.0;
    [drawer addSubview:stack];

    if (checkpointFailed_)
    {
        UILabel *failure = [[UILabel alloc] init];
        failure.translatesAutoresizingMaskIntoConstraints = NO;
        failure.text = FlyNesLocalizedString(@"pause.checkpoint_failed");
        failure.accessibilityIdentifier = @"pause_checkpoint_failed";
        failure.font = [UIFont systemFontOfSize:14.0 weight:UIFontWeightRegular];
        failure.textColor = [UIColor colorWithRed:1.0 green:0.42 blue:0.37 alpha:1.0];
        failure.numberOfLines = 0;
        [stack addArrangedSubview:failure];
    }

    for (const flynes::product::PauseCommand command : flynes::product::kPauseDrawerCommands)
    {
        UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
        NSString *commandId = pause_command_id(command);
        [button setTitle:pause_command_title(command) forState:UIControlStateNormal];
        button.accessibilityIdentifier = commandId;
        button.tag = static_cast<NSInteger>(command);
        button.backgroundColor = command == flynes::product::PauseCommand::Resume
                                     ? [UIColor colorWithRed:1.0 green:0.42 blue:0.37 alpha:1.0]
                                     : [UIColor colorWithWhite:0.16 alpha:1.0];
        UIColor *titleColor = command == flynes::product::PauseCommand::Resume
                                  ? [UIColor colorWithWhite:0.07 alpha:1.0]
                                  : [UIColor colorWithWhite:0.96 alpha:1.0];
        [button setTitleColor:titleColor forState:UIControlStateNormal];
        button.layer.cornerRadius = 14.0;
        [button.heightAnchor constraintEqualToConstant:command == flynes::product::PauseCommand::Resume
                                                           ? 52.0
                                                           : 48.0]
            .active = YES;
        [button addTarget:self action:@selector(handlePauseButton:) forControlEvents:UIControlEventTouchUpInside];
        [stack addArrangedSubview:button];
    }

    [self.view addSubview:pauseLayer_];
    [NSLayoutConstraint activateConstraints:@[
        [pauseLayer_.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [pauseLayer_.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [pauseLayer_.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [pauseLayer_.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [scrim.leadingAnchor constraintEqualToAnchor:pauseLayer_.leadingAnchor],
        [scrim.trailingAnchor constraintEqualToAnchor:drawer.leadingAnchor],
        [scrim.topAnchor constraintEqualToAnchor:pauseLayer_.topAnchor],
        [scrim.bottomAnchor constraintEqualToAnchor:pauseLayer_.bottomAnchor],
        [drawer.trailingAnchor constraintEqualToAnchor:pauseLayer_.trailingAnchor],
        [drawer.topAnchor constraintEqualToAnchor:pauseLayer_.topAnchor],
        [drawer.bottomAnchor constraintEqualToAnchor:pauseLayer_.bottomAnchor],
        [drawer.widthAnchor constraintEqualToConstant:320.0],
        [stack.leadingAnchor constraintEqualToAnchor:drawer.leadingAnchor constant:24.0],
        [stack.trailingAnchor constraintEqualToAnchor:drawer.trailingAnchor constant:-24.0],
        [stack.centerYAnchor constraintEqualToAnchor:drawer.centerYAnchor],
    ]];
}

- (void)handlePauseButton:(UIButton *)sender
{
    const auto command = static_cast<flynes::product::PauseCommand>(sender.tag);
    NSString *commandId = pause_command_id(command);
    if (command == flynes::product::PauseCommand::Resume)
    {
        [self resumeFromPause];
        return;
    }
    if (command == flynes::product::PauseCommand::Settings)
    {
        if (self.onPauseCommand != nil)
            self.onPauseCommand(commandId);
        return;
    }
    [self dismissPauseLayerKeepingPaused:YES];
    if (self.onPauseCommand != nil)
        self.onPauseCommand(commandId);
}

- (void)resumeFromPause
{
    audioInterrupted_ = NO;
    paused_ = NO;
    checkpointFailed_ = NO;
    [self dismissPauseLayerKeepingPaused:NO];
    [self reloadProductSettings];
    [self updatePlayback];
}

- (void)dismissPauseLayerKeepingPaused:(BOOL)keepPaused
{
    paused_ = keepPaused;
    [pauseLayer_ removeFromSuperview];
    pauseLayer_ = nil;
    drawerOpen_ = NO;
    overlay_.hidden = NO;
    pauseButton_.hidden = NO;
}

- (void)displayTick:(CFTimeInterval)timestamp
{
    NSAssert(NSThread.isMainThread, @"Playback is owned by the main run loop");
    if (!running_ || ![self isPlaybackAllowed] || !clock_.beginTick(timestamp)) return;
    BOOL produced = NO;
    while (clock_.frameDue()) {
        NSError *error = nil;
        if (![runtime_ stepFrameWithButtons:input_.sample(NSProcessInfo.processInfo.systemUptime) error:&error]) {
            paused_ = YES;
            [self stopPlayback];
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:FlyNesLocalizedString(@"run.emulation_paused")
                message:error.localizedDescription preferredStyle:UIAlertControllerStyleAlert];
            __weak __typeof__(self) weakSelf = self;
            [alert addAction:[UIAlertAction actionWithTitle:FlyNesLocalizedString(@"pause.game_center")
                style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
                    (void)action;
                    RunSurfaceViewController *strong = weakSelf;
                    if (strong.onPauseCommand) strong.onPauseCommand(@"game_center");
                }]];
            [self presentViewController:alert animated:YES completion:nil];
            return;
        }
        clock_.didProduceSamples(runtime_.lastFrameSampleCount, 48000);
        // Always drain the runtime PCM FIFO, including when sound is disabled.
        NSData *pcm = [runtime_ pullPCM];
        if (!audioInterrupted_) [audio_ enqueuePCM:pcm];
        produced = YES;
    }
    if (produced) {
        NSData *pixels = [runtime_ copyLatestRgb565Frame];
        if (pixels.length) [renderer_ uploadRgb565:pixels width:256 height:240];
    }
    [self drawFrame];
}

- (void)drawFrame
{
    [renderer_ draw];
    if (!renderer_.permanentFailure || videoFailureShown_) return;
    paused_ = YES;
    [self stopPlayback];
    if (!visible_ || self.presentedViewController) return;
    videoFailureShown_ = YES;
    [self saveAutosaveIfEnabled];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:FlyNesLocalizedString(@"run.video_unavailable")
        message:FlyNesLocalizedString(@"run.video_unavailable.detail") preferredStyle:UIAlertControllerStyleAlert];
    __weak __typeof__(self) weakSelf = self;
    [alert addAction:[UIAlertAction actionWithTitle:FlyNesLocalizedString(@"pause.game_center")
        style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            (void)action;
            RunSurfaceViewController *strong = weakSelf;
            if (strong.onPauseCommand) strong.onPauseCommand(@"game_center");
        }]];
    [self presentViewController:alert animated:YES completion:nil];
}
- (BOOL)isPlaybackAllowed
{
    return visible_ && foreground_ && romReady_ && !paused_ && !drawerOpen_
        && !(audioInterrupted_ && audio_.focusPolicy == 1);
}

- (void)updatePlayback
{
    if (![self isPlaybackAllowed]) { [self stopPlayback]; return; }
    if (!running_) {
        [overlay_ releaseAllButtons];
        buttons_ = 0; input_.clear();
        clock_.reset();
        running_ = YES;
        [pacer_ attachToView:metalHost_];
    }
    if (!audioInterrupted_ && audio_.enabled) {
        NSError *error = nil;
        if (![audio_ start:&error]) NSLog(@"FlyNES audio unavailable: %@", error.localizedDescription);
    } else {
        [audio_ pause];
    }
}

- (void)stopPlayback
{
    running_ = NO;
    [pacer_ invalidate];
    clock_.reset();
    buttons_ = 0; input_.clear();
    [overlay_ releaseAllButtons];
    [runtime_ clearInput];
    [runtime_ discardAudio];
    [audio_ pause];
}

- (void)saveAutosaveIfEnabled
{
    if (!romReady_) return;
    NSNumber *enabled = FlyNesAppBridge.sharedInstance.settingsGet[@"autosave_enabled"];
    if (enabled != nil && !enabled.boolValue) { checkpointFailed_ = NO; return; }
    NSData *blob = [runtime_ saveCheckpoint:nil];
    checkpointFailed_ = blob == nil || ![self persistAutosave:blob];
}

- (BOOL)restoreCheckpoint:(NSData *)checkpoint error:(NSError **)error
{
    NSAssert(NSThread.isMainThread, @"Playback checkpoints are main-thread owned");
    [self stopPlayback];
    const BOOL restored = [runtime_ loadCheckpoint:checkpoint error:error];
    if (restored) {
        NSData *pixels = [runtime_ copyLatestRgb565Frame];
        if (pixels.length) [renderer_ uploadRgb565:pixels width:256 height:240];
        [self drawFrame];
    }
    [self updatePlayback];
    return restored;
}

- (BOOL)resetGame:(NSError **)error
{
    NSAssert(NSThread.isMainThread, @"Playback reset is main-thread owned");
    [self stopPlayback];
    romReady_ = self.romData.length > 0 && [runtime_ loadRom:self.romData error:error];
    [self updatePlayback];
    return romReady_;
}

- (void)applicationWillResignActive:(NSNotification *)notification
{
    (void)notification;
    foreground_ = NO;
    [self stopPlayback];
    [self saveAutosaveIfEnabled];
}

- (void)applicationDidBecomeActive:(NSNotification *)notification
{
    (void)notification;
    foreground_ = YES;
    [self reloadProductSettings];
}

- (void)audioInterruption:(NSNotification *)notification
{
    // AVAudioSession notifications can arrive off the UI thread.
    NSDictionary *info = notification.userInfo;
    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        RunSurfaceViewController *strong = weakSelf;
        if (!strong) return;
        const BOOL began = [info[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan;
        if (began) {
            strong->audioInterrupted_ = YES;
            [strong->audio_ pause];
            [strong->overlay_ releaseAllButtons];
            strong->buttons_ = 0; strong->input_.clear();
            [strong->runtime_ clearInput];
            [strong updatePlayback];
        } else {
            const BOOL resume = ([info[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue]
                                  & AVAudioSessionInterruptionOptionShouldResume) != 0;
            strong->audioInterrupted_ = NO;
            if (!resume) [strong openPauseDrawer];
            else [strong updatePlayback];
        }
    });
}

- (void)audioRouteChanged:(NSNotification *)notification
{
    const NSUInteger reason = [notification.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];
    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        RunSurfaceViewController *strong = weakSelf;
        if (!strong) return;
        if (reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable && strong->running_) {
            [strong openPauseDrawer]; // Headphones unplugged: do not unexpectedly use the speaker.
        } else if (strong->running_) {
            [strong->audio_ flush];
        }
    });
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    return UIInterfaceOrientationMaskLandscape;
}

- (BOOL)prefersStatusBarHidden
{
    return YES;
}

@end
