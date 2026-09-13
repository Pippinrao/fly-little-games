#import "GamepadOverlayView.h"
#import "AppLocalization.h"

#import <UIKit/UIKit.h>

#include "flynes/product/control_layout.hpp"
#include "flynes/product/gamepad_hit_map.hpp"
#include "flynes/product/nes_input_bits.hpp"
#include "DirectionSession.hpp"

#include <optional>
#include <algorithm>
#include <unordered_map>
#include <array>

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
    uint32_t face = 0;
    NSTimeInterval downTime = 0;
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

UIColor *control_color(uint32_t rgb, CGFloat alpha)
{
    return [UIColor colorWithRed:((rgb >> 16) & 255)/255.0
        green:((rgb >> 8) & 255)/255.0 blue:(rgb & 255)/255.0 alpha:alpha];
}

void draw_hit_control(const GamepadHitMap &map, Control control, uint32_t buttons, float opacity)
{
    try
    {
        const GamepadHitMap::Target &target = map.target(control);
        UIBezierPath *path = nil;
        const bool pressed = (buttons & face_bits(control)) != 0;
        CGRect box = cg_rect(target.bounds());
        if (target.shape() == GamepadHitMap::Shape::Pill) {
            box.origin.y = CGRectGetMidY(box)-14; box.size.height = 28;
        } else if (pressed) box = CGRectInset(box,2,2);
        if (target.shape() == GamepadHitMap::Shape::Circle)
            path = [UIBezierPath bezierPathWithOvalInRect:box];
        else if (target.shape() == GamepadHitMap::Shape::Pill)
            path = [UIBezierPath bezierPathWithRoundedRect:box cornerRadius:box.size.height / 2.0];
        else
            path = [UIBezierPath bezierPathWithRoundedRect:box cornerRadius:12];
        UIColor *fill = control_color(pressed ? 0xF4EFE6 : 0x25282F,pressed ? MAX(.88f,opacity) : opacity);
        [fill setFill];
        [path fill];
        UIColor *outline = control_color(control == Control::A ? 0xFF6B5E : 0xBEB8AE,opacity);
        [outline setStroke];
        path.lineWidth = control == Control::A ? 3 : 2;
        [path stroke];
        NSString *label = control == Control::A ? @"A" : control == Control::B ? @"B"
            : control == Control::Start ? @"START" : @"SELECT";
        NSDictionary *attributes = @{NSFontAttributeName:[UIFont boldSystemFontOfSize:target.shape() == GamepadHitMap::Shape::Pill ? 11 : 22],
            NSForegroundColorAttributeName:control_color(pressed ? 0x121316 : 0xFFFFFF,opacity)};
        CGSize textSize = [label sizeWithAttributes:attributes];
        [label drawAtPoint:CGPointMake(CGRectGetMidX(box)-textSize.width/2,
            CGRectGetMidY(box)-textSize.height/2) withAttributes:attributes];
    }
    catch (...)
    {
    }
}

} // namespace

@interface FlyNesAccessibleControl : UIAccessibilityElement
@property(nonatomic, copy) BOOL (^activation)(void);
@end
@implementation FlyNesAccessibleControl
- (BOOL)accessibilityActivate { return self.activation ? self.activation() : NO; }
@end

@interface GamepadOverlayView ()
- (BOOL)activateAccessibleBits:(uint32_t)bits;
- (void)moveTouch:(UITouch *)sample owner:(UITouch *)owner;
@end

@implementation GamepadOverlayView {
    std::unordered_map<NSUInteger, PointerBind> ownership_;
    std::optional<GamepadHitMap> hit_map_;
    std::optional<flynes::ios::DirectionSession> direction_;
    uint32_t face_buttons_;
    uint32_t direction_buttons_;
    flynes::ios::DirectionFeedbackGate directionFeedback_;
    flynes::ios::JoystickReturnAnimation joystickReturn_;
    uint32_t accessibility_buttons_;
    std::array<uint64_t,8> accessibilityVersions_;
    CGSize lastLayoutSize_;
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
        accessibility_buttons_ = 0;
        accessibilityVersions_.fill(0);
        lastLayoutSize_ = CGSizeZero;
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
    if (!CGSizeEqualToSize(lastLayoutSize_,CGSizeZero) && !CGSizeEqualToSize(lastLayoutSize_,bounds.size))
        [self releaseAllButtons];
    lastLayoutSize_ = bounds.size;

    const UIEdgeInsets safeArea = self.safeAreaInsets;
    const char *utf8 = _layoutUtf8.UTF8String;
    const ControlLayoutV2 layout =
        ControlLayoutV2::decode_or_recommended(utf8 != nullptr ? utf8 : "");
    _controlOpacity = layout.opacity();
    const DirectionControlMode mode = direction_mode_from(_joystickMode);
    const float dead_zone = clamp_dead_zone(_deadZone);
    const bool modeChanged = hit_map_ && hit_map_->direction_mode() != mode;
    const CGPoint previousCenter = hit_map_ ? CGPointMake(hit_map_->dpad_bounds().center_x(),hit_map_->dpad_bounds().center_y()) : CGPointZero;
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
    if (!direction_) direction_.emplace(*hit_map_);
    else direction_->reconfigure(*hit_map_);
    if (!direction_->owned) {
        for (auto it = ownership_.begin(); it != ownership_.end();) {
            if (it->second.direction) it = ownership_.erase(it); else ++it;
        }
        [self setDirectionBits:0];
    }
    const CGPoint currentCenter = CGPointMake(hit_map_->dpad_bounds().center_x(),hit_map_->dpad_bounds().center_y());
    if (modeChanged || !CGPointEqualToPoint(previousCenter,currentCenter)) joystickReturn_.cancel();
    if (modeChanged) directionFeedback_.reset(direction_->feedbackRevision);
    NSMutableArray *accessible = [NSMutableArray array];
    __weak GamepadOverlayView *weakSelf = self;
    for (Control control : {Control::Up,Control::Down,Control::Left,Control::Right,Control::B,Control::A,Control::Select,Control::Start}) {
        FlyNesAccessibleControl *element = [[FlyNesAccessibleControl alloc] initWithAccessibilityContainer:self];
        NSString *label = control == Control::A ? @"A" : control == Control::B ? @"B"
            : control == Control::Start ? @"START" : control == Control::Select ? @"SELECT"
            : control == Control::Up ? @"UP" : control == Control::Down ? @"DOWN" : control == Control::Left ? @"LEFT" : @"RIGHT";
        const uint32_t bit = control == Control::Up ? flynes::product::NES_UP
            : control == Control::Down ? flynes::product::NES_DOWN : control == Control::Left ? flynes::product::NES_LEFT
            : control == Control::Right ? flynes::product::NES_RIGHT : face_bits(control);
        element.activation = ^BOOL { return [weakSelf activateAccessibleBits:bit]; };
        NSString *key = [@"control." stringByAppendingString:label];
        NSString *localized = FlyNesLocalizedString(key);
        element.accessibilityLabel = [localized isEqualToString:key] ? label : localized;
        element.accessibilityIdentifier = [@"NES_" stringByAppendingString:label];
        element.accessibilityTraits = UIAccessibilityTraitButton;
        element.accessibilityFrameInContainerSpace = cg_rect(hit_map_->target(control).bounds());
        [accessible addObject:element];
    }
    self.accessibilityElements = accessible;
    [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect
{
    (void)rect;
    if (!hit_map_ || !UIGraphicsGetCurrentContext()) return;
    const float pressedAlpha = std::max(.88f,_controlOpacity);
    UIColor *idle = control_color(0x25282F,_controlOpacity);
    UIColor *pressed = control_color(0xF4EFE6,pressedAlpha);
    UIColor *outline = control_color(0xBEB8AE,_controlOpacity);
    UIColor *accent = control_color(0xFF6B5E,pressedAlpha);
    const bool owned = direction_ && direction_->owned;
    [idle setFill]; [(owned ? accent : outline) setStroke];
    if (hit_map_->joystick_mode()) {
        CGRect base = cg_rect(hit_map_->dpad_bounds());
        CGPoint knob = CGPointMake(CGRectGetMidX(base),CGRectGetMidY(base));
        if (owned) {
            base.origin = CGPointMake(direction_->centerX-base.size.width/2,direction_->centerY-base.size.height/2);
            knob = CGPointMake(direction_->knobX,direction_->knobY);
        } else if (hit_map_->fixed_joystick_mode() && joystickReturn_.active()) {
            const auto point = joystickReturn_.sample(NSProcessInfo.processInfo.systemUptime);
            knob = CGPointMake(point.first,point.second);
            if (joystickReturn_.active()) {
                __weak GamepadOverlayView *weakSelf = self;
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/60),dispatch_get_main_queue(),^{ [weakSelf setNeedsDisplay]; });
            }
        }
        UIBezierPath *basePath = [UIBezierPath bezierPathWithOvalInRect:base];
        basePath.lineWidth = owned ? 3 : 2;
        [basePath fill]; [basePath stroke];
        if (owned && direction_->saturated()) {
            UIBezierPath *ring = [UIBezierPath bezierPathWithOvalInRect:CGRectInset(base,-4,-4)];
            ring.lineWidth = 4; [control_color(0xFF6B5E,std::min(220.0f/255,pressedAlpha)) setStroke]; [ring stroke];
        }
        [(direction_ && direction_->activated() ? pressed : idle) setFill];
        [(owned ? accent : outline) setStroke];
        const CGFloat radius = hit_map_->joystick_radius()*.4375f;
        UIBezierPath *knobPath = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(knob.x-radius,knob.y-radius,2*radius,2*radius)];
        knobPath.lineWidth = owned ? 3 : 2;
        [knobPath fill]; [knobPath stroke];
    } else {
        const CGRect box = cg_rect(hit_map_->dpad_bounds());
        const CGFloat arm = box.size.width/3;
        for (NSValue *value in @[[NSValue valueWithCGRect:CGRectMake(CGRectGetMidX(box)-arm/2,box.origin.y,arm,box.size.height)],
                [NSValue valueWithCGRect:CGRectMake(box.origin.x,CGRectGetMidY(box)-arm/2,box.size.width,arm)]]) {
            UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:value.CGRectValue cornerRadius:8];
            path.lineWidth = owned ? 3 : 2; [path fill]; [path stroke];
        }
        const uint32_t bits = [self publishedButtons];
        [pressed setFill];
        for (Control control : {Control::Up,Control::Down,Control::Left,Control::Right}) {
            const uint32_t bit = control == Control::Up ? flynes::product::NES_UP : control == Control::Down ? flynes::product::NES_DOWN
                : control == Control::Left ? flynes::product::NES_LEFT : flynes::product::NES_RIGHT;
            if (bits & bit) [[UIBezierPath bezierPathWithRoundedRect:cg_rect(hit_map_->target(control).bounds()) cornerRadius:8] fill];
        }
        [control_color(0x121316,_controlOpacity) setFill];
        [[UIBezierPath bezierPathWithOvalInRect:CGRectMake(CGRectGetMidX(box)-8,CGRectGetMidY(box)-8,16,16)] fill];
    }
    for (Control control : {Control::B,Control::A,Control::Select,Control::Start})
        draw_hit_control(*hit_map_,control,[self publishedButtons],_controlOpacity);
}

- (void)fireHapticHook:(uint32_t)pressed
{
    if (_hapticLevel <= 1 || pressed == 0)
        return;
    UIImpactFeedbackStyle style = UIImpactFeedbackStyleMedium;
    if (_hapticLevel <= 2)
        style = UIImpactFeedbackStyleLight;
    else if (_hapticLevel >= 4)
        style = UIImpactFeedbackStyleHeavy;
    if (_distinctAbHaptics && (pressed & NES_A)) style = UIImpactFeedbackStyleRigid;
    else if (_distinctAbHaptics && (pressed & NES_B)) style = UIImpactFeedbackStyleSoft;
    UIImpactFeedbackGenerator *generator = [[UIImpactFeedbackGenerator alloc] initWithStyle:style];
    [generator impactOccurredWithIntensity:_hapticLevel == 2 ? 0.4 : _hapticLevel == 3 ? 0.7 : 1.0];
}

- (uint32_t)publishedButtons
{
    return face_buttons_ | direction_buttons_ | accessibility_buttons_;
}

- (BOOL)activateAccessibleBits:(uint32_t)bits
{
    if (!bits) return NO;
    accessibility_buttons_ |= bits;
    [self fireHapticHook:bits]; [self publish]; [self setNeedsDisplay];
    for (unsigned index = 0; index < 8; ++index) {
        const uint32_t bit = 1u << index;
        if (!(bits & bit)) continue;
        const uint64_t version = ++accessibilityVersions_[index];
        __weak GamepadOverlayView *weakSelf = self;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,17*NSEC_PER_MSEC),dispatch_get_main_queue(),^{
            GamepadOverlayView *strong = weakSelf;
            if (!strong || strong->accessibilityVersions_[index] != version) return;
            strong->accessibility_buttons_ &= ~bit;
            [strong publish]; [strong setNeedsDisplay];
        });
    }
    return YES;
}

- (void)releaseAllButtons
{
    ownership_.clear();
    if (direction_) direction_->clear();
    directionFeedback_.reset(direction_ ? direction_->feedbackRevision : 0);
    joystickReturn_.cancel();
    for (auto &version : accessibilityVersions_) ++version;
    face_buttons_ = direction_buttons_ = accessibility_buttons_ = 0;
    [self publish];
    if (self.buttonsCancelled) self.buttonsCancelled();
    [self setNeedsDisplay];
}

- (void)publish
{
    if (self.buttonsChanged) self.buttonsChanged([self publishedButtons]);
}

- (void)refreshFaceButtons
{
    const uint32_t before = [self publishedButtons];
    face_buttons_ = 0;
    for (const auto &entry : ownership_)
        if (!entry.second.direction) face_buttons_ |= entry.second.face;
    if (before != [self publishedButtons]) { [self publish]; [self setNeedsDisplay]; }
}

- (void)setDirectionBits:(uint32_t)bits
{
    const uint32_t before = [self publishedButtons];
    direction_buttons_ = bits;
    if (before != [self publishedButtons]) { [self publish]; [self setNeedsDisplay]; }
}

- (void)emitDirectionFeedbackAt:(NSTimeInterval)time
{
    if (direction_ && directionFeedback_.shouldEmit(direction_->feedbackRevision,time))
        [self fireHapticHook:direction_->bits];
}

- (void)moveTouch:(UITouch *)sample owner:(UITouch *)owner
{
    auto found = ownership_.find(reinterpret_cast<NSUInteger>((__bridge void *)owner));
    if (found == ownership_.end() || !hit_map_) return;
    const CGPoint point = [sample locationInView:self];
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return;
    if (!found->second.direction) {
        const uint32_t nextBits = face_bits(hit_map_->button_hit((float)point.x,(float)point.y));
        if ((found->second.face & (NES_A|NES_B)) && (nextBits & (NES_A|NES_B)))
            found->second.face |= nextBits;
        else if (!nextBits) ownership_.erase(found);
        [self refreshFaceButtons];
    } else {
        direction_->move((float)point.x,(float)point.y);
        [self setDirectionBits:direction_->bits];
    }
    [self setNeedsDisplay];
}

- (void)updateCurrentTouches:(UIEvent *)event excluding:(NSSet<UITouch *> *)excluded
{
    for (UITouch *touch in event.allTouches)
        if (![excluded containsObject:touch]) [self moveTouch:touch owner:touch];
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    if (!hit_map_) return;
    [self updateCurrentTouches:event excluding:touches];
    BOOL buttonFeedback = NO;
    NSTimeInterval time = event ? event.timestamp : NSProcessInfo.processInfo.systemUptime;
    for (UITouch *touch in touches) {
        time = touch.timestamp;
        const CGPoint point = [touch locationInView:self];
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) continue;
        const Control face = hit_map_->button_hit((float)point.x,(float)point.y);
        PointerBind bind; bind.downTime = touch.timestamp;
        if (face != Control::None) {
            bind.face = face_bits(face);
            ownership_[reinterpret_cast<NSUInteger>((__bridge void *)touch)] = bind;
            [self refreshFaceButtons]; [self fireHapticHook:bind.face]; buttonFeedback = YES;
        } else if (direction_ && direction_->capture((float)point.x,(float)point.y)) {
            bind.direction = true;
            ownership_[reinterpret_cast<NSUInteger>((__bridge void *)touch)] = bind;
            joystickReturn_.cancel();
            [self setDirectionBits:direction_->bits]; [self setNeedsDisplay];
        }
    }
    if (buttonFeedback && direction_) directionFeedback_.consume(direction_->feedbackRevision);
    else [self emitDirectionFeedbackAt:time];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    NSTimeInterval time = event ? event.timestamp : NSProcessInfo.processInfo.systemUptime;
    for (UITouch *touch in touches) {
        // Coalesced samples have their own UITouch identity; ownership belongs to the delivered touch.
        for (UITouch *sample in [event coalescedTouchesForTouch:touch]) [self moveTouch:sample owner:touch];
        [self moveTouch:touch owner:touch];
        time = touch.timestamp;
    }
    [self emitDirectionFeedbackAt:time];
}

- (void)removeTouch:(UITouch *)touch normalRelease:(BOOL)normalRelease
{
    auto found = ownership_.find(reinterpret_cast<NSUInteger>((__bridge void *)touch));
    if (found == ownership_.end()) return;
    const PointerBind bind = found->second;
    ownership_.erase(found);
    if (bind.direction) {
        if (normalRelease && hit_map_->fixed_joystick_mode())
            joystickReturn_.start(direction_->centerX,direction_->centerY,direction_->knobX,direction_->knobY,touch.timestamp);
        else joystickReturn_.cancel();
        direction_->clear();
        [self setDirectionBits:0];
    } else {
        [self refreshFaceButtons];
        if (normalRelease && self.buttonsReleased) self.buttonsReleased(bind.face,bind.downTime,touch.timestamp);
    }
    [self setNeedsDisplay];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self updateCurrentTouches:event excluding:nil];
    // UIKit's terminal location can differ from the last move, including a final B-to-A roll.
    for (UITouch *touch in touches) [self moveTouch:touch owner:touch];
    BOOL directionEnded = NO;
    for (UITouch *touch in touches) {
        const auto found = ownership_.find(reinterpret_cast<NSUInteger>((__bridge void *)touch));
        directionEnded |= found != ownership_.end() && found->second.direction;
        [self removeTouch:touch normalRelease:YES];
    }
    if (direction_ && directionEnded) directionFeedback_.consume(direction_->feedbackRevision);
    else [self emitDirectionFeedbackAt:event ? event.timestamp : NSProcessInfo.processInfo.systemUptime];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self updateCurrentTouches:event excluding:touches];
    BOOL directionEnded = NO;
    for (UITouch *touch in touches) {
        const auto found = ownership_.find(reinterpret_cast<NSUInteger>((__bridge void *)touch));
        directionEnded |= found != ownership_.end() && found->second.direction;
        [self removeTouch:touch normalRelease:NO];
    }
    if (direction_ && directionEnded) directionFeedback_.consume(direction_->feedbackRevision);
    else [self emitDirectionFeedbackAt:event ? event.timestamp : NSProcessInfo.processInfo.systemUptime];
}

@end
