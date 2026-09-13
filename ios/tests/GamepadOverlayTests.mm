#import <XCTest/XCTest.h>
#import "GamepadOverlayView.h"
#include "FrameInputLatch.hpp"
@interface GamepadOverlayView (Testing)
- (uint32_t)publishedButtons;
@end
@interface FlyTestTouch : UITouch
@property(nonatomic) CGPoint point;
@property(nonatomic) NSTimeInterval sampleTime;
@end
@implementation FlyTestTouch
- (CGPoint)locationInView:(UIView *)view { (void)view; return self.point; }
- (NSTimeInterval)timestamp { return self.sampleTime; }
@end
@interface FlyTestEvent : UIEvent
@property(nonatomic, copy) NSSet<UITouch *> *testTouches;
@property(nonatomic, copy) NSArray<UITouch *> *history;
@end
@implementation FlyTestEvent
- (NSSet<UITouch *> *)allTouches { return self.testTouches; }
- (NSArray<UITouch *> *)coalescedTouchesForTouch:(UITouch *)touch { (void)touch; return self.history; }
@end
@interface FlyHapticSpy : GamepadOverlayView
@property(nonatomic) NSUInteger feedbackCount;
@end
@implementation FlyHapticSpy
- (void)fireHapticHook:(uint32_t)bits { if (bits) ++self.feedbackCount; }
@end
@interface GamepadOverlayTests : XCTestCase
@end
@implementation GamepadOverlayTests
- (GamepadOverlayView *)overlay {
    GamepadOverlayView *view = [[GamepadOverlayView alloc] initWithFrame:CGRectMake(0,0,1000,500)];
    view.hapticLevel = 1;
    [view layoutSubviews];
    return view;
}
- (FlyTestTouch *)touchAt:(CGPoint)point {
    FlyTestTouch *touch = [[FlyTestTouch alloc] init]; touch.point = point; return touch;
}
- (void)testTwoFingersOnOneButtonRetainPressUntilBothReleased {
    GamepadOverlayView *view = [self overlay];
    FlyTestTouch *first = [self touchAt:CGPointMake(940,320)];
    FlyTestTouch *second = [self touchAt:CGPointMake(940,320)];
    [view touchesBegan:[NSSet setWithObjects:first,second,nil] withEvent:nil];
    XCTAssertEqual([view publishedButtons] & 1, 1u);
    [view touchesEnded:[NSSet setWithObject:first] withEvent:nil];
    XCTAssertEqual([view publishedButtons] & 1, 1u);
    [view touchesCancelled:[NSSet setWithObject:second] withEvent:nil];
    XCTAssertEqual([view publishedButtons], 0u);
}
- (void)testSlidingOffButtonEndsOwnershipUntilNextTouchDown {
    GamepadOverlayView *view = [self overlay];
    FlyTestTouch *touch = [self touchAt:CGPointMake(940,320)];
    [view touchesBegan:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertEqual([view publishedButtons] & 1, 1u);
    touch.point = CGPointMake(500,250);
    [view touchesMoved:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertEqual([view publishedButtons], 0u);
    touch.point = CGPointMake(940,320);
    [view touchesMoved:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertEqual([view publishedButtons], 0u);
    [view releaseAllButtons];
    XCTAssertEqual([view publishedButtons], 0u);
}
- (void)testRollFromBToAAccumulatesBothButtons {
    GamepadOverlayView *view = [self overlay];
    FlyTestTouch *touch = [self touchAt:CGPointMake(870,430)];
    [view touchesBegan:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertEqual([view publishedButtons], 2u);
    touch.point = CGPointMake(940,320);
    [view touchesMoved:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertEqual([view publishedButtons], 3u);
}
- (void)testFollowingJoystickEdgeCaptureIsNeutral {
    GamepadOverlayView *view = [self overlay]; view.joystickMode = FlyNesJoystickModeFollow;
    FlyTestTouch *touch = [self touchAt:CGPointMake(10,250)];
    [view touchesBegan:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertEqual([view publishedButtons], 0u);
    touch.point = CGPointMake(30,250);
    [view touchesMoved:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertEqual([view publishedButtons], 128u);
}
- (void)testEndingOnAConsumesFinalCoordinatesOfBRoll {
    GamepadOverlayView *view = [self overlay];
    NSMutableArray<NSNumber *> *masks = [NSMutableArray array];
    view.buttonsChanged = ^(uint32_t bits) { [masks addObject:@(bits)]; };
    FlyTestTouch *touch = [self touchAt:CGPointMake(870,430)];
    [view touchesBegan:[NSSet setWithObject:touch] withEvent:nil];
    touch.point = CGPointMake(940,320);
    [view touchesEnded:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertTrue([masks containsObject:@3], @"final release position must complete B+A roll");
    XCTAssertEqual([view publishedButtons], 0u);
}
- (void)testLocalCancellationDoesNotInvokeGlobalInputReset {
    GamepadOverlayView *view = [self overlay];
    __block NSUInteger resets = 0;
    view.buttonsCancelled = ^{ ++resets; };
    FlyTestTouch *direction = [self touchAt:CGPointMake(150,380)];
    [view touchesBegan:[NSSet setWithObject:direction] withEvent:nil];
    [view touchesCancelled:[NSSet setWithObject:direction] withEvent:nil];
    XCTAssertEqual(resets, 0u, @"a local cancellation must not erase another pointer's completed tap");
    [view releaseAllButtons];
    XCTAssertEqual(resets, 1u);
}
- (void)testAllEightAccessibleControlsCanActivate {
    GamepadOverlayView *view = [self overlay];
    NSDictionary<NSString *,NSNumber *> *bits = @{@"NES_UP":@16,@"NES_DOWN":@32,
        @"NES_LEFT":@64,@"NES_RIGHT":@128,@"NES_A":@1,@"NES_B":@2,
        @"NES_SELECT":@4,@"NES_START":@8};
    XCTAssertEqual(view.accessibilityElements.count, 8u);
    for (UIAccessibilityElement *element in view.accessibilityElements) {
        [view releaseAllButtons];
        NSNumber *expected = bits[element.accessibilityIdentifier];
        XCTAssertNotNil(expected);
        XCTAssertTrue([element accessibilityActivate]);
        XCTAssertEqual([view publishedButtons], expected.unsignedIntValue);
    }
    [view releaseAllButtons];
}
- (void)testResizingCancelsHeldInput {
    GamepadOverlayView *view = [self overlay];
    FlyTestTouch *touch = [self touchAt:CGPointMake(940,320)];
    [view touchesBegan:[NSSet setWithObject:touch] withEvent:nil];
    view.bounds = CGRectMake(0,0,800,500);
    [view layoutSubviews];
    XCTAssertEqual([view publishedButtons], 0u);
}
- (NSData *)renderPixels:(GamepadOverlayView *)view {
    const size_t width = (size_t)view.bounds.size.width, height = (size_t)view.bounds.size.height;
    NSMutableData *pixels = [NSMutableData dataWithLength:width*height*4];
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(pixels.mutableBytes,width,height,8,width*4,
        colorSpace,kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    UIGraphicsPushContext(context); [view drawRect:view.bounds]; UIGraphicsPopContext();
    CGContextRelease(context); CGColorSpaceRelease(colorSpace);
    return pixels;
}
- (void)testPressedFaceButtonChangesRenderedAppearance {
    GamepadOverlayView *view = [self overlay];
    NSData *idle = [self renderPixels:view];
    FlyTestTouch *touch = [self touchAt:CGPointMake(940,320)];
    [view touchesBegan:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertNotEqualObjects(idle,[self renderPixels:view]);
}
- (void)testCompletedShortTapSurvivesAnotherPointersCancellation {
    GamepadOverlayView *view = [self overlay];
    __block flynes::ios::FrameInputLatch input;
    view.buttonsChanged = ^(uint32_t bits) { input.update(bits); };
    view.buttonsReleased = ^(uint32_t bits,NSTimeInterval down,NSTimeInterval up) { input.release(bits,down,up); };
    view.buttonsCancelled = ^{ input.clear(); };
    FlyTestTouch *a = [self touchAt:CGPointMake(940,320)]; a.sampleTime = 1;
    FlyTestTouch *pad = [self touchAt:CGPointMake(150,380)]; pad.sampleTime = 1;
    [view touchesBegan:[NSSet setWithObjects:a,pad,nil] withEvent:nil];
    a.sampleTime = 1.002;
    [view touchesEnded:[NSSet setWithObject:a] withEvent:nil];
    [view touchesCancelled:[NSSet setWithObject:pad] withEvent:nil];
    XCTAssertEqual(input.sample(1.005),1u);
    [view releaseAllButtons]; XCTAssertEqual(input.sample(1.006),0u);
}
- (void)testCoalescedSamplesRetainTheDeliveredTouchOwner {
    GamepadOverlayView *view = [self overlay]; view.joystickMode = FlyNesJoystickModeFollow;
    FlyTestTouch *touch = [self touchAt:CGPointMake(100,250)];
    [view touchesBegan:[NSSet setWithObject:touch] withEvent:nil];
    FlyTestTouch *historical = [self touchAt:CGPointMake(300,250)];
    FlyTestEvent *event = [[FlyTestEvent alloc] init]; event.history = @[historical];
    [view touchesMoved:[NSSet setWithObject:touch] withEvent:event];
    XCTAssertEqual([view publishedButtons],64u,@"out-and-back history moves the floating base before the final sample");
}
- (void)testDirectionHapticsHaveAnEightyMillisecondCooldown {
    FlyHapticSpy *view = [[FlyHapticSpy alloc] initWithFrame:CGRectMake(0,0,1000,500)];
    [view layoutSubviews];
    FlyTestTouch *touch = [self touchAt:CGPointMake(140,380)]; touch.sampleTime = 1;
    [view touchesBegan:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertEqual(view.feedbackCount,1u);
    touch.point = CGPointMake(140,420); touch.sampleTime = 1.020;
    [view touchesMoved:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertEqual(view.feedbackCount,1u);
    touch.point = CGPointMake(60,380); touch.sampleTime = 1.100;
    [view touchesMoved:[NSSet setWithObject:touch] withEvent:nil];
    XCTAssertEqual(view.feedbackCount,2u);
}
- (void)testDpadCornerIsTransparentInsteadOfSolidSquare {
    GamepadOverlayView *view = [self overlay];
    NSData *pixels = [self renderPixels:view];
    const uint8_t *bytes = (const uint8_t *)pixels.bytes;
    XCTAssertEqual(bytes[(315*1000+35)*4+3],0u);
}
@end
