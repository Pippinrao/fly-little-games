#import "RunSurfaceViewController.h"

#import "FlyNesRuntimeBridge.h"
#import "GamepadOverlayView.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

@implementation RunSurfaceViewController {
    UIView *metalHost_;
    GamepadOverlayView *overlay_;
    FlyNesRuntimeBridge *runtime_;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    self.view.multipleTouchEnabled = YES;

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
    overlay_.buttonsChanged = ^(uint32_t buttons) {
      (void)buttons;
    };
    [self.view addSubview:overlay_];

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
    ]];

    runtime_ = [[FlyNesRuntimeBridge alloc] init];
    [runtime_ createRuntime:nil];
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    const UIEdgeInsets insets = self.view.safeAreaInsets;
    overlay_.layoutMargins = insets;
    CALayer *metal = metalHost_.layer.sublayers.firstObject;
    metal.frame = metalHost_.bounds;
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
