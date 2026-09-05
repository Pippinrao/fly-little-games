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

} // namespace

@implementation RunSurfaceViewController {
    UIView *metalHost_;
    GamepadOverlayView *overlay_;
    UIButton *pauseButton_;
    UIView *pauseLayer_;
    FlyNesRuntimeBridge *runtime_;
    BOOL paused_;
    BOOL drawerOpen_;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    self.view.multipleTouchEnabled = YES;
    paused_ = NO;
    drawerOpen_ = NO;

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

- (void)applyOverlayButtons:(uint32_t)buttons
{
    if (paused_ || drawerOpen_)
        return;
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
        [self dismissPauseLayerKeepingPaused:YES];
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
