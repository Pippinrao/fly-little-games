#import "FlyNesDisplayLinkPacer.h"

#import <UIKit/UIKit.h>

@implementation FlyNesDisplayLinkPacer {
    CADisplayLink *link_;
    CFTimeInterval lastTimestamp_;
    CFTimeInterval lastPresentedTime_;
    NSUInteger presentedFrames_;
    CFTimeInterval intervalSum_;
    NSUInteger intervalCount_;
}

- (instancetype)init
{
    self = [super init];
    if (self != nil)
    {
        lastTimestamp_ = 0;
        lastPresentedTime_ = 0;
        presentedFrames_ = 0;
        intervalSum_ = 0;
        intervalCount_ = 0;
    }
    return self;
}

- (CFTimeInterval)lastTimestamp
{
    return lastTimestamp_;
}

- (CFTimeInterval)lastPresentedTime
{
    return lastPresentedTime_;
}

- (NSUInteger)presentedFrames
{
    return presentedFrames_;
}

- (void)attachToView:(UIView *)view
{
    [self invalidate];
    link_ = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [link_ addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    (void)view;
}

- (void)invalidate
{
    [link_ invalidate];
    link_ = nil;
}

- (void)tick:(CADisplayLink *)link
{
    const CFTimeInterval timestamp = link.timestamp;
    const CFTimeInterval presentedTime = link.targetTimestamp;
    if (lastTimestamp_ > 0)
    {
        intervalSum_ += timestamp - lastTimestamp_;
        intervalCount_ += 1;
    }
    lastTimestamp_ = timestamp;
    lastPresentedTime_ = presentedTime;
    presentedFrames_ += 1;
    if (self.onTick != nil)
        self.onTick(timestamp, presentedTime);
}

- (BOOL)motionInterpolationAllowed
{
    // Never treat UIScreen.maximumFramesPerSecond as 120 Hz evidence.
    // 60→120 motion compensation is allowed only when CADisplayLink
    // timestamps plus present stats show a stable ~120 Hz cadence.
    if (intervalCount_ < 30)
        return NO;
    const CFTimeInterval mean = intervalSum_ / static_cast<CFTimeInterval>(intervalCount_);
    const BOOL displayLinkSays120 = mean > 0 && fabs(mean - (1.0 / 120.0)) < 0.0015;
    const BOOL presentStatsAgree =
        lastPresentedTime_ > lastTimestamp_ && presentedFrames_ >= 30;
    return displayLinkSays120 && presentStatsAgree;
}

@end
