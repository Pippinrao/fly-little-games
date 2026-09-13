#ifndef FLYNES_GAMEPAD_OVERLAY_VIEW_H
#define FLYNES_GAMEPAD_OVERLAY_VIEW_H

#import <UIKit/UIKit.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, FlyNesJoystickMode) {
    FlyNesJoystickModeFollow = 1,
    FlyNesJoystickModeFixed = 2,
    FlyNesJoystickModeDPad = 3,
};

@interface GamepadOverlayView : UIView

@property(nonatomic) float controlOpacity;
@property(nonatomic) FlyNesJoystickMode joystickMode;
@property(nonatomic) float deadZone;
@property(nonatomic) uint32_t hapticLevel;
@property(nonatomic) BOOL distinctAbHaptics;
@property(nonatomic, copy) NSString *layoutUtf8;
@property(nonatomic, copy, nullable) void (^buttonsChanged)(uint32_t buttons);
@property(nonatomic, copy, nullable) void (^buttonsReleased)(uint32_t buttons, NSTimeInterval downTime, NSTimeInterval upTime);
// Global resets only. A canceled individual touch must not erase another touch's completed tap.
@property(nonatomic, copy, nullable) void (^buttonsCancelled)(void);

- (void)releaseAllButtons;

@end

NS_ASSUME_NONNULL_END

#endif
