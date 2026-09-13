#import "RunSurfaceViewController.h"

#import "FlyNesAppBridge.h"
#import "FlyNesRuntimeBridge.h"
#import "GamepadOverlayView.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "flynes/product/pause_actions.hpp"

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
        return @"Resume";
    case PauseCommand::GameCenter:
        return @"Game Center";
    case PauseCommand::Settings:
        return @"Settings";
    }
    return @"Resume";
}

// One in-game status row: the §2.4 label and, while the session ABI has no
// value to give, the blocked reason beneath it. Never a fabricated value
// (spec §4).
void add_nearby_status_row(UIStackView *stack, NSString *labelKey, NSString *reasonKey)
{
    UILabel *label = [[UILabel alloc] init];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    label.text = NSLocalizedString(labelKey, nil);
    label.font = [UIFont systemFontOfSize:13.0 weight:UIFontWeightRegular];
    label.textColor = [UIColor colorWithWhite:0.96 alpha:1.0];
    label.numberOfLines = 0;
    [stack addArrangedSubview:label];
    if (reasonKey != nil)
    {
        UILabel *reason = [[UILabel alloc] init];
        reason.translatesAutoresizingMaskIntoConstraints = NO;
        reason.text = NSLocalizedString(reasonKey, nil);
        reason.font = [UIFont systemFontOfSize:11.5 weight:UIFontWeightRegular];
        reason.textColor = [UIColor colorWithWhite:0.62 alpha:1.0];
        reason.numberOfLines = 0;
        [stack addArrangedSubview:reason];
    }
}

} // namespace

@implementation RunSurfaceViewController {
    UIView *metalHost_;
    GamepadOverlayView *overlay_;
    UIButton *pauseButton_;
    UIView *pauseLayer_;
    UIView *nearbyBanner_;
    FlyNesRuntimeBridge *runtime_;
    BOOL paused_;
    BOOL drawerOpen_;
    BOOL checkpointFailed_;
    BOOL autosaveRestored_;
    BOOL romReady_;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    self.view.multipleTouchEnabled = YES;
    paused_ = NO;
    drawerOpen_ = NO;
    checkpointFailed_ = NO;
    autosaveRestored_ = NO;
    romReady_ = NO;

    metalHost_ = [[UIView alloc] initWithFrame:self.view.bounds];
    metalHost_.translatesAutoresizingMaskIntoConstraints = NO;
    metalHost_.backgroundColor = UIColor.blackColor;
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    metalHost_.layer.sublayers = @[ layer ];
    [self.view addSubview:metalHost_];

    overlay_ = [[GamepadOverlayView alloc] initWithFrame:self.view.bounds];
    overlay_.translatesAutoresizingMaskIntoConstraints = NO;
    __weak typeof(self) weakSelf = self;
    overlay_.buttonsChanged = ^(uint32_t buttons) {
      RunSurfaceViewController *strong = weakSelf;
      if (strong == nil)
          return;
      [strong applyOverlayButtons:buttons];
    };
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
    pauseButton_.accessibilityLabel = @"Pause";
    [pauseButton_ addTarget:self action:@selector(openPauseDrawer) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:pauseButton_];
    [self buildNearbyBanner];

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
    NSData *rom = nil;
    if ([self canonicalIdMapsToBuiltinFromBelow])
        rom = [self bundledFromBelowRom];
    NSError *romError = nil;
    if (rom.length > 0)
        romReady_ = [runtime_ loadRom:rom error:&romError];
    if (romReady_)
        [self restoreAutosave];
    else
        [self surfaceRomOpenFailure];
    [self reloadProductSettings];
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
    CALayer *metal = metalHost_.layer.sublayers.firstObject;
    metal.frame = metalHost_.bounds;
}

- (void)reloadProductSettings
{
    NSDictionary<NSString *, id> *snapshot = FlyNesAppBridge.sharedInstance.settingsGet;
    NSNumber *direction = snapshot[@"direction_mode"];
    NSNumber *haptic = snapshot[@"haptic_level"];
    NSNumber *dead = snapshot[@"dead_zone"];
    if (direction != nil)
        overlay_.joystickMode = static_cast<FlyNesJoystickMode>(direction.unsignedIntValue);
    if (haptic != nil)
        overlay_.hapticLevel = haptic.unsignedIntValue;
    if (dead != nil)
        overlay_.deadZone = dead.floatValue;
    overlay_.layoutUtf8 = FlyNesAppBridge.sharedInstance.controlLayoutGet;
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
    [runtime_ loadCheckpoint:blob error:nil];
}

- (void)surfaceRomOpenFailure
{
    NSString *message = NSLocalizedString(@"library.rom_open_failed", nil);
    UIAlertController *alert =
        [UIAlertController alertControllerWithTitle:nil
                                            message:message
                                     preferredStyle:UIAlertControllerStyleAlert];
    alert.view.accessibilityIdentifier = @"library_rom_open_failed";
    __weak typeof(self) weakSelf = self;
    [alert addAction:[UIAlertAction actionWithTitle:NSLocalizedString(@"pause.game_center", nil)
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *_Nonnull action) {
                                              RunSurfaceViewController *strong = weakSelf;
                                              if (strong == nil || strong.onPauseCommand == nil)
                                                  return;
                                              strong.onPauseCommand(@"game_center");
                                            }]];
    dispatch_async(dispatch_get_main_queue(), ^{
      [self presentViewController:alert animated:YES completion:nil];
    });
}

// Whether a nearby session exists at all. `FlyNesAppBridge` exposes no
// `fly_session` handle or snapshot yet (spec §3, §10 D4), so no `FlyNES`
// build can prove that one does — which is why local single-player shows no
// nearby block at all rather than a permanently blocked row (spec §10 D7).
// When the shared/session line adds the session surface to the binding, this
// becomes the read of that snapshot and the banner, drawer block and rows
// start answering on their own.
- (BOOL)nearbySessionActive
{
    return NO;
}

// The states that demand user action — 冻结, 重连倒计时 and the
// authority-timeout options — live in a non-dismissible banner pinned to the
// top of the run surface, not in a modal and not only in the pause drawer: a
// frozen end must not have to hunt through the drawer to recover (spec §4,
// §10 D6). It is built once here and shown only once a session exists (§10 D7).
- (void)buildNearbyBanner
{
    nearbyBanner_ = [[UIView alloc] init];
    nearbyBanner_.translatesAutoresizingMaskIntoConstraints = NO;
    nearbyBanner_.backgroundColor = [UIColor colorWithWhite:0.11 alpha:0.92];
    nearbyBanner_.accessibilityIdentifier = @"nearby_status_banner";
    nearbyBanner_.hidden = YES;

    UIScrollView *scroll = [[UIScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;

    UIStackView *stack = [[UIStackView alloc] init];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 6.0;
    [scroll addSubview:stack];
    [nearbyBanner_ addSubview:scroll];
    [self populateNearbyBanner:stack];

    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [nearbyBanner_.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [nearbyBanner_.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [nearbyBanner_.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:nearbyBanner_.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:nearbyBanner_.trailingAnchor],
        [scroll.topAnchor constraintEqualToAnchor:nearbyBanner_.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:nearbyBanner_.bottomAnchor],
        [scroll.heightAnchor constraintEqualToConstant:140.0],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.leadingAnchor constant:16.0],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.trailingAnchor constant:-16.0],
        [stack.topAnchor constraintEqualToAnchor:scroll.topAnchor constant:10.0],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.bottomAnchor constant:-10.0],
        [stack.widthAnchor constraintEqualToAnchor:scroll.widthAnchor constant:-32.0],
    ]];
    // Attached only while a session exists, so local single-player loses no
    // vertical play space to an empty band (spec §10 D7).
    [self updateNearbyBanner];
}

// Banner content. The banner carries no dismiss control by design: while the
// state persists it stays. 接管 / 继续单人 / 保存结束 are the three
// authority-timeout actions (spec §10 D9); each is disabled with a specific
// reason until the ABI can say whether it is available, and none is ever
// rendered from wire text (spec §10 D9).
- (void)populateNearbyBanner:(UIStackView *)stack
{
    if (![self nearbySessionActive])
        return;

    add_nearby_status_row(stack, @"nearby.ingame.frozen", @"nearby.blocked.session_read");
    add_nearby_status_row(stack, @"nearby.ingame.reconnect_countdown",
                          @"nearby.ingame.reconnect_countdown.blocked");
    add_nearby_status_row(stack, @"nearby.ingame.authority_timeout",
                          @"nearby.ingame.authority_timeout.available");
    add_nearby_status_row(stack, @"nearby.ingame.takeover", @"nearby.blocked.session_read");
    add_nearby_status_row(stack, @"nearby.ingame.continue_solo", @"nearby.blocked.session_read");
    add_nearby_status_row(stack, @"nearby.ingame.save_and_end", @"nearby.blocked.session_read");
    add_nearby_status_row(stack, @"nearby.ingame.option_unavailable_reason",
                          @"nearby.blocked.session_read");
    add_nearby_status_row(stack, @"nearby.ingame.branch_no_auto_merge", nil);
}

- (void)updateNearbyBanner
{
    if (nearbyBanner_ == nil)
        return;
    if ([self nearbySessionActive])
    {
        nearbyBanner_.hidden = NO;
        if (nearbyBanner_.superview == nil)
            [self.view addSubview:nearbyBanner_];
        return;
    }
    nearbyBanner_.hidden = YES;
    [nearbyBanner_ removeFromSuperview];
}

// The in-game status block inside the existing pause drawer: every §2.4 field
// as a row labelled per the vocabulary, each with the blocked reason that
// follows from the session ABI gap (spec §3, §4). Multi-branch visualisation is
// deferred, but the always-known no-auto-merge invariant stays (spec §10 D10).
//
// These rows are built when the drawer opens, so they pick the session up on
// the next open — unlike the banner, which needs one `populateNearbyBanner:`
// call at the moment a session actually begins.
- (void)addNearbyDrawerRowsToStack:(UIStackView *)stack
{
    if (![self nearbySessionActive])
        return;

    UILabel *heading = [[UILabel alloc] init];
    heading.translatesAutoresizingMaskIntoConstraints = NO;
    heading.text = NSLocalizedString(@"nearby.ingame.section", nil);
    heading.font = [UIFont systemFontOfSize:12.0 weight:UIFontWeightSemibold];
    heading.textColor = [UIColor colorWithWhite:0.62 alpha:1.0];
    heading.numberOfLines = 0;
    [stack addArrangedSubview:heading];

    add_nearby_status_row(stack, @"nearby.ingame.mode", @"nearby.blocked.mode_gate");
    add_nearby_status_row(stack, @"nearby.ingame.connection_quality",
                          @"nearby.blocked.connection_quality");
    add_nearby_status_row(stack, @"nearby.ingame.seat", @"nearby.blocked.mode_gate");
    add_nearby_status_row(stack, @"nearby.ingame.pause_state", @"nearby.blocked.session_read");
    add_nearby_status_row(stack, @"nearby.ingame.frozen", @"nearby.blocked.session_read");
    add_nearby_status_row(stack, @"nearby.ingame.reconnect_countdown",
                          @"nearby.ingame.reconnect_countdown.blocked");
    add_nearby_status_row(stack, @"nearby.ingame.authority_timeout",
                          @"nearby.blocked.authority_recovery");
    add_nearby_status_row(stack, @"nearby.ingame.save_and_end", @"nearby.blocked.session_read");
    add_nearby_status_row(stack, @"nearby.ingame.branch_reunion",
                          @"nearby.blocked.branch_merge");
    add_nearby_status_row(stack, @"nearby.ingame.branch_no_auto_merge", nil);
    add_nearby_status_row(stack, @"nearby.ingame.local_mute", @"nearby.blocked.local_mute");
}

- (void)applyOverlayButtons:(uint32_t)buttons
{
    if (!romReady_ || paused_ || drawerOpen_)
        return;
    if (!autosaveRestored_)
    {
        [self restoreAutosave];
        autosaveRestored_ = YES;
    }
    [runtime_ stepFrameWithButtons:buttons error:nil];
}

- (void)openPauseDrawer
{
    if (drawerOpen_)
        return;
    paused_ = YES;
    drawerOpen_ = YES;
    overlay_.hidden = YES;
    pauseButton_.hidden = YES;
    [runtime_ stepFrameWithButtons:0 error:nil];
    NSData *blob = [runtime_ saveCheckpoint:nil];
    checkpointFailed_ = blob == nil || ![self persistAutosave:blob];

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
        failure.text = NSLocalizedString(@"pause.checkpoint_failed", nil);
        failure.accessibilityIdentifier = @"pause_checkpoint_failed";
        failure.font = [UIFont systemFontOfSize:14.0 weight:UIFontWeightRegular];
        failure.textColor = [UIColor colorWithRed:1.0 green:0.42 blue:0.37 alpha:1.0];
        failure.numberOfLines = 0;
        [stack addArrangedSubview:failure];
    }

    // In-game status rows for a nearby session, added above the command stack
    // (spec §4, §10 D6). Nothing is added at all in local single-player: no
    // session means nothing to report, and a permanent blocked block would be
    // noise (spec §10 D7).
    [self addNearbyDrawerRowsToStack:stack];

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
    [self dismissPauseLayerKeepingPaused:NO];
    if (self.onPauseCommand != nil)
        self.onPauseCommand(commandId);
}

- (void)resumeFromPause
{
    paused_ = NO;
    checkpointFailed_ = NO;
    [self dismissPauseLayerKeepingPaused:NO];
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

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    return UIInterfaceOrientationMaskLandscape;
}

- (BOOL)prefersStatusBarHidden
{
    return YES;
}

@end
