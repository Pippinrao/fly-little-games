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
@property(nonatomic, copy) NSString *layoutUtf8;
@property(nonatomic, copy, nullable) void (^buttonsChanged)(uint32_t buttons);

@end

NS_ASSUME_NONNULL_END

#endif
