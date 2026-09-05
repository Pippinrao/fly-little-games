#import "GamepadOverlayView.h"

#import <UIKit/UIKit.h>

#include "flynes/product/control_layout.hpp"
#include "flynes/product/gamepad_hit_map.hpp"
#include "flynes/product/nes_input_bits.hpp"

#include <optional>
#include <unordered_map>

namespace {

using flynes::product::Control;
using flynes::product::ControlLayoutV2;
using flynes::product::DirectionControlMode;
using flynes::product::GamepadHitMap;
using flynes::product::NES_A;
using flynes::product::NES_B;
using flynes::product::NES_SELECT;
using flynes::product::NES_START;

struct PointerBind
{
    bool direction = false;
    Control face = Control::None;
};

uint32_t face_bits(Control control)
{
    switch (control)
    {
    case Control::A:
        return NES_A;
    case Control::B:
        return NES_B;
    case Control::Select:
        return NES_SELECT;
    case Control::Start:
        return NES_START;
    default:
        return 0;
    }
}

DirectionControlMode direction_mode_from(FlyNesJoystickMode mode)
{
    switch (mode)
    {
    case FlyNesJoystickModeFollow:
        return DirectionControlMode::Joystick;
    case FlyNesJoystickModeFixed:
        return DirectionControlMode::FixedJoystick;
    case FlyNesJoystickModeDPad:
        return DirectionControlMode::DPad;
    }
    return DirectionControlMode::DPad;
}

float clamp_dead_zone(float value)
{
    if (value < 0.08f)
        return 0.08f;
    if (value > 0.45f)
        return 0.45f;
    return value;
}

CGRect cg_rect(const GamepadHitMap::Bounds &bounds)
{
    return CGRectMake(bounds.left(), bounds.top(), bounds.width(), bounds.height());
}

void draw_hit_control(const GamepadHitMap &map, Control control, UIColor *fill)
{
    try
    {
        const GamepadHitMap::Target &target = map.target(control);
        UIBezierPath *path = nil;
        const CGRect box = cg_rect(target.bounds());
        if (target.shape() == GamepadHitMap::Shape::Circle)
            path = [UIBezierPath bezierPathWithOvalInRect:box];
        else if (target.shape() == GamepadHitMap::Shape::Pill)
            path = [UIBezierPath bezierPathWithRoundedRect:box cornerRadius:box.size.height / 2.0];
        else
            path = [UIBezierPath bezierPathWithRoundedRect:box cornerRadius:10];
        [fill setFill];
        [path fill];
        [[UIColor colorWithWhite:0.85 alpha:1.0] setStroke];
        [path stroke];
    }
    catch (...)
    {
    }
}

} // namespace

@implementation GamepadOverlayView {
    std::unordered_map<NSUInteger, PointerBind> ownership_;
    std::optional<GamepadHitMap> hit_map_;
    uint32_t face_buttons_;
    uint32_t direction_buttons_;
    CGPoint followOrigin_;
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        self.opaque = NO;
        self.multipleTouchEnabled = YES;
        _controlOpacity = 0.52f;
        _joystickMode = FlyNesJoystickModeDPad;
        _deadZone = 0.22f;
        _hapticLevel = 3;
        _layoutUtf8 = @"";
        face_buttons_ = 0;
        direction_buttons_ = 0;
        followOrigin_ = CGPointZero;
    }
    return self;
}

- (void)setControlOpacity:(float)controlOpacity
{
    _controlOpacity = controlOpacity;
    [self setNeedsDisplay];
}

- (void)setJoystickMode:(FlyNesJoystickMode)joystickMode
{
    _joystickMode = joystickMode;
    [self rebuildHitMap];
}

- (void)setDeadZone:(float)deadZone
{
    _deadZone = deadZone;
    [self rebuildHitMap];
}

- (void)setLayoutUtf8:(NSString *)layoutUtf8
{
    _layoutUtf8 = [layoutUtf8 copy] ?: @"";
    [self rebuildHitMap];
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    [self rebuildHitMap];
}

- (void)safeAreaInsetsDidChange
{
    [super safeAreaInsetsDidChange];
    [self rebuildHitMap];
}

- (void)rebuildHitMap
{
    const CGRect bounds = self.bounds;
    if (bounds.size.width < 1.0 || bounds.size.height < 1.0)
        return;

    const UIEdgeInsets safeArea = self.safeAreaInsets;
    const char *utf8 = _layoutUtf8.UTF8String;
    const ControlLayoutV2 layout =
        ControlLayoutV2::decode_or_recommended(utf8 != nullptr ? utf8 : "");
    _controlOpacity = layout.opacity();
    const DirectionControlMode mode = direction_mode_from(_joystickMode);
    const float dead_zone = clamp_dead_zone(_deadZone);
    try
    {
        hit_map_ = GamepadHitMap::from_layout(static_cast<int>(bounds.size.width),
                                              static_cast<int>(bounds.size.height),
                                              1.0f,
                                              static_cast<int>(safeArea.left),
                                              static_cast<int>(safeArea.right),
                                              static_cast<int>(safeArea.top),
                                              static_cast<int>(safeArea.bottom),
                                              layout,
                                              mode,
                                              dead_zone);
    }
    catch (...)
    {
        hit_map_ = GamepadHitMap::from_layout(static_cast<int>(bounds.size.width),
                                              static_cast<int>(bounds.size.height),
                                              1.0f,
                                              static_cast<int>(safeArea.left),
                                              static_cast<int>(safeArea.right),
                                              static_cast<int>(safeArea.top),
                                              static_cast<int>(safeArea.bottom),
                                              ControlLayoutV2::recommended(),
                                              DirectionControlMode::DPad,
                                              0.22f);
    }
    [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect
{
    (void)rect;
    if (!hit_map_.has_value())
        return;
    CGContextRef context = UIGraphicsGetCurrentContext();
    if (context == nullptr)
        return;
    CGContextSetAlpha(context, static_cast<CGFloat>(_controlOpacity));
    [[UIColor colorWithWhite:0.18 alpha:0.9] setFill];
    [[UIColor colorWithWhite:0.85 alpha:1.0] setStroke];

    if (hit_map_->joystick_mode())
    {
        UIBezierPath *stick = [UIBezierPath bezierPathWithOvalInRect:cg_rect(hit_map_->dpad_bounds())];
        [stick fill];
        [stick stroke];
        const CGFloat knob = hit_map_->joystick_travel_radius();
        CGPoint origin = followOrigin_;
        if (CGPointEqualToPoint(origin, CGPointZero) || hit_map_->fixed_joystick_mode())
        {
            origin = CGPointMake(hit_map_->dpad_bounds().center_x(),
                                 hit_map_->dpad_bounds().center_y());
        }
        UIBezierPath *knob_path = [UIBezierPath
            bezierPathWithOvalInRect:CGRectMake(origin.x - knob, origin.y - knob, knob * 2.0, knob * 2.0)];
        [[UIColor colorWithWhite:0.85 alpha:0.85] setFill];
        [knob_path fill];
    }
    else
    {
        UIBezierPath *dpad =
            [UIBezierPath bezierPathWithRoundedRect:cg_rect(hit_map_->dpad_bounds()) cornerRadius:12];
        [dpad fill];
        [dpad stroke];
    }

    [[UIColor colorWithRed:1.0 green:0.42 blue:0.37 alpha:0.95] setFill];
    draw_hit_control(*hit_map_, Control::A, [UIColor colorWithRed:1.0 green:0.42 blue:0.37 alpha:0.95]);
    draw_hit_control(*hit_map_, Control::B, [UIColor colorWithRed:1.0 green:0.42 blue:0.37 alpha:0.95]);
    draw_hit_control(*hit_map_, Control::Start, [UIColor colorWithWhite:0.22 alpha:0.9]);
    draw_hit_control(*hit_map_, Control::Select, [UIColor colorWithWhite:0.22 alpha:0.9]);
}

- (void)fireHapticHook
{
    if (_hapticLevel == 0)
        return;
    UIImpactFeedbackStyle style = UIImpactFeedbackStyleMedium;
    if (_hapticLevel <= 2)
        style = UIImpactFeedbackStyleLight;
    else if (_hapticLevel >= 4)
        style = UIImpactFeedbackStyleHeavy;
    UIImpactFeedbackGenerator *generator = [[UIImpactFeedbackGenerator alloc] initWithStyle:style];
    [generator impactOccurred];
}

- (uint32_t)publishedButtons
{
    return face_buttons_ | direction_buttons_;
}

- (void)publish
{
    if (self.buttonsChanged != nil)
        self.buttonsChanged([self publishedButtons]);
}

- (void)setFace:(Control)control pressed:(BOOL)pressed
{
    const uint32_t mask = face_bits(control);
    const uint32_t before = [self publishedButtons];
    if (pressed)
        face_buttons_ |= mask;
    else
        face_buttons_ &= ~mask;
    if (before != [self publishedButtons])
    {
        [self fireHapticHook];
        [self publish];
        [self setNeedsDisplay];
    }
}

- (void)setDirectionBits:(uint32_t)bits
{
    const uint32_t before = [self publishedButtons];
    direction_buttons_ = bits;
    if (before != [self publishedButtons])
    {
        [self fireHapticHook];
        [self publish];
        [self setNeedsDisplay];
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    (void)event;
    if (!hit_map_.has_value())
        return;
    for (UITouch *touch in touches)
    {
        const CGPoint point = [touch locationInView:self];
        const Control face = hit_map_->button_hit(static_cast<float>(point.x), static_cast<float>(point.y));
        PointerBind bind;
        if (face != Control::None)
        {
            bind.direction = false;
            bind.face = face;
            ownership_[touch.hash] = bind;
            [self setFace:face pressed:YES];
            continue;
        }
        const bool follow = hit_map_->following_joystick_mode();
        const bool can_direction = hit_map_->can_start_direction(static_cast<float>(point.x),
                                                                static_cast<float>(point.y));
        if (!can_direction)
            continue;
        bind.direction = true;
        bind.face = Control::None;
        ownership_[touch.hash] = bind;
        if (follow)
        {
            followOrigin_ = CGPointMake(hit_map_->clamp_joystick_center_x(static_cast<float>(point.x)),
                                        hit_map_->clamp_joystick_center_y(static_cast<float>(point.y)));
        }
        else
        {
            followOrigin_ = CGPointMake(hit_map_->dpad_bounds().center_x(),
                                        hit_map_->dpad_bounds().center_y());
        }
        uint32_t bits = 0;
        if (hit_map_->joystick_mode())
        {
            bits = hit_map_->joystick_direction_bits(static_cast<float>(followOrigin_.x),
                                                     static_cast<float>(followOrigin_.y),
                                                     static_cast<float>(point.x),
                                                     static_cast<float>(point.y),
                                                     direction_buttons_);
        }
        else
        {
            bits = hit_map_->direction_bits(static_cast<float>(point.x), static_cast<float>(point.y),
                                            direction_buttons_);
        }
        [self setDirectionBits:bits];
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    (void)event;
    if (!hit_map_.has_value())
        return;
    for (UITouch *touch in touches)
    {
        auto found = ownership_.find(touch.hash);
        if (found == ownership_.end())
            continue;
        const CGPoint point = [touch locationInView:self];
        if (!found->second.direction)
        {
            const Control next = hit_map_->button_hit(static_cast<float>(point.x), static_cast<float>(point.y));
            if (next != found->second.face)
                continue;
            [self setFace:found->second.face pressed:YES];
            continue;
        }
        uint32_t bits = 0;
        if (hit_map_->joystick_mode())
        {
            bits = hit_map_->joystick_direction_bits(static_cast<float>(followOrigin_.x),
                                                     static_cast<float>(followOrigin_.y),
                                                     static_cast<float>(point.x),
                                                     static_cast<float>(point.y),
                                                     direction_buttons_);
        }
        else
        {
            bits = hit_map_->direction_bits(static_cast<float>(point.x), static_cast<float>(point.y),
                                            direction_buttons_);
        }
        [self setDirectionBits:bits];
    }
}

- (void)cancelTouch:(UITouch *)touch
{
    auto found = ownership_.find(touch.hash);
    if (found == ownership_.end())
        return;
    if (found->second.direction)
    {
        followOrigin_ = CGPointZero;
        [self setDirectionBits:0];
    }
    else
    {
        [self setFace:found->second.face pressed:NO];
    }
    ownership_.erase(found);
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    (void)event;
    for (UITouch *touch in touches)
        [self cancelTouch:touch];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    (void)event;
    for (UITouch *touch in touches)
        [self cancelTouch:touch];
}

@end
