#import "GamepadOverlayView.h"

#import <UIKit/UIKit.h>

#include <unordered_map>

namespace {

constexpr uint32_t kButtonA = 1u << 0;
constexpr uint32_t kButtonB = 1u << 1;
constexpr uint32_t kSelect = 1u << 2;
constexpr uint32_t kStart = 1u << 3;
constexpr uint32_t kUp = 1u << 4;
constexpr uint32_t kDown = 1u << 5;
constexpr uint32_t kLeft = 1u << 6;
constexpr uint32_t kRight = 1u << 7;

enum class ControlId {
    None,
    DPad,
    Joystick,
    A,
    B,
    Start,
    Select,
};

ControlId hit_test(CGPoint point, CGRect dpad, CGRect joystick, CGRect a, CGRect b,
                   CGRect start, CGRect select)
{
    if (CGRectContainsPoint(a, point))
        return ControlId::A;
    if (CGRectContainsPoint(b, point))
        return ControlId::B;
    if (CGRectContainsPoint(start, point))
        return ControlId::Start;
    if (CGRectContainsPoint(select, point))
        return ControlId::Select;
    if (CGRectContainsPoint(dpad, point))
        return ControlId::DPad;
    if (CGRectContainsPoint(joystick, point))
        return ControlId::Joystick;
    return ControlId::None;
}

} // namespace

@implementation GamepadOverlayView {
    std::unordered_map<NSUInteger, ControlId> ownership_;
    uint32_t buttons_;
    CGPoint followOrigin_;
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        self.opaque = NO;
        self.multipleTouchEnabled = YES;
        _controlOpacity = 0.85f;
        _joystickMode = FlyNesJoystickModeDPad;
        _hapticLevel = 3;
        buttons_ = 0;
        followOrigin_ = CGPointZero;
    }
    return self;
}

- (void)setControlOpacity:(float)controlOpacity
{
    _controlOpacity = controlOpacity;
    [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect
{
    (void)rect;
    CGContextRef context = UIGraphicsGetCurrentContext();
    if (context == nullptr)
        return;
    CGContextSetAlpha(context, static_cast<CGFloat>(_controlOpacity));
    [[UIColor colorWithWhite:0.18 alpha:0.9] setFill];
    [[UIColor colorWithWhite:0.85 alpha:1.0] setStroke];
    UIBezierPath *dpad = [UIBezierPath bezierPathWithRoundedRect:[self dpadRect] cornerRadius:12];
    [dpad fill];
    [dpad stroke];
    UIBezierPath *stick = [UIBezierPath bezierPathWithOvalInRect:[self joystickRect]];
    [stick fill];
    [stick stroke];
    UIBezierPath *a = [UIBezierPath bezierPathWithOvalInRect:[self aRect]];
    UIBezierPath *b = [UIBezierPath bezierPathWithOvalInRect:[self bRect]];
    [[UIColor colorWithRed:1.0 green:0.42 blue:0.37 alpha:0.95] setFill];
    [a fill];
    [b fill];
}

- (CGRect)dpadRect
{
    const UIEdgeInsets safe = self.safeAreaInsets;
    const CGFloat size = 132;
    return CGRectMake(24 + safe.left, CGRectGetHeight(self.bounds) - size - 24 - safe.bottom, size, size);
}

- (CGRect)joystickRect
{
    const CGFloat size = 120;
    return CGRectMake(36, CGRectGetHeight(self.bounds) - size - 170, size, size);
}

- (CGRect)aRect
{
    return CGRectMake(CGRectGetWidth(self.bounds) - 108, CGRectGetHeight(self.bounds) - 150, 72, 72);
}

- (CGRect)bRect
{
    return CGRectMake(CGRectGetWidth(self.bounds) - 188, CGRectGetHeight(self.bounds) - 110, 72, 72);
}

- (CGRect)startRect
{
    return CGRectMake(CGRectGetMidX(self.bounds) - 20, CGRectGetHeight(self.bounds) - 56, 72, 28);
}

- (CGRect)selectRect
{
    return CGRectMake(CGRectGetMidX(self.bounds) - 108, CGRectGetHeight(self.bounds) - 56, 72, 28);
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

- (void)publish
{
    if (self.buttonsChanged != nil)
        self.buttonsChanged(buttons_);
}

- (void)applyControl:(ControlId)control atPoint:(CGPoint)point pressed:(BOOL)pressed
{
    uint32_t mask = 0;
    switch (control)
    {
    case ControlId::A:
        mask = kButtonA;
        break;
    case ControlId::B:
        mask = kButtonB;
        break;
    case ControlId::Start:
        mask = kStart;
        break;
    case ControlId::Select:
        mask = kSelect;
        break;
    case ControlId::DPad: {
        CGRect box = [self dpadRect];
        CGPoint center = CGPointMake(CGRectGetMidX(box), CGRectGetMidY(box));
        if (point.y < center.y - 16)
            mask |= kUp;
        if (point.y > center.y + 16)
            mask |= kDown;
        if (point.x < center.x - 16)
            mask |= kLeft;
        if (point.x > center.x + 16)
            mask |= kRight;
        break;
    }
    case ControlId::Joystick: {
        CGRect box = [self joystickRect];
        CGPoint origin = CGPointMake(CGRectGetMidX(box), CGRectGetMidY(box));
        if (_joystickMode == FlyNesJoystickModeFollow && !CGPointEqualToPoint(followOrigin_, CGPointZero))
            origin = followOrigin_;
        else if (_joystickMode == FlyNesJoystickModeFixed)
            origin = CGPointMake(CGRectGetMidX(box), CGRectGetMidY(box));
        const CGFloat dx = point.x - origin.x;
        const CGFloat dy = point.y - origin.y;
        if (dy < -18)
            mask |= kUp;
        if (dy > 18)
            mask |= kDown;
        if (dx < -18)
            mask |= kLeft;
        if (dx > 18)
            mask |= kRight;
        break;
    }
    case ControlId::None:
        break;
    }
    const uint32_t before = buttons_;
    if (control == ControlId::DPad || control == ControlId::Joystick)
        buttons_ &= ~(kUp | kDown | kLeft | kRight);
    if (pressed)
        buttons_ |= mask;
    else
        buttons_ &= ~mask;
    if (before != buttons_)
    {
        [self fireHapticHook];
        [self publish];
        [self setNeedsDisplay];
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    (void)event;
    for (UITouch *touch in touches)
    {
        const CGPoint point = [touch locationInView:self];
        ControlId control = hit_test(point, [self dpadRect], [self joystickRect], [self aRect],
                                     [self bRect], [self startRect], [self selectRect]);
        if (_joystickMode == FlyNesJoystickModeFollow && control == ControlId::None)
        {
            control = ControlId::Joystick;
            followOrigin_ = point;
        }
        if (control == ControlId::None)
            continue;
        ownership_[touch.hash] = control;
        [self applyControl:control atPoint:point pressed:YES];
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    (void)event;
    for (UITouch *touch in touches)
    {
        auto found = ownership_.find(touch.hash);
        if (found == ownership_.end())
            continue;
        [self applyControl:found->second atPoint:[touch locationInView:self] pressed:YES];
    }
}

- (void)cancelTouch:(UITouch *)touch
{
    auto found = ownership_.find(touch.hash);
    if (found == ownership_.end())
        return;
    [self applyControl:found->second atPoint:[touch locationInView:self] pressed:NO];
    ownership_.erase(found);
    followOrigin_ = CGPointZero;
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
