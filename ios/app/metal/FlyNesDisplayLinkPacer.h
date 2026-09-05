#ifndef FLYNES_DISPLAY_LINK_PACER_H
#define FLYNES_DISPLAY_LINK_PACER_H

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

NS_ASSUME_NONNULL_BEGIN

@interface FlyNesDisplayLinkPacer : NSObject

@property(nonatomic, readonly) CFTimeInterval lastTimestamp;
@property(nonatomic, readonly) CFTimeInterval lastPresentedTime;
@property(nonatomic, readonly) NSUInteger presentedFrames;
@property(nonatomic, copy, nullable) void (^onTick)(CFTimeInterval timestamp, CFTimeInterval presentedTime);

- (void)attachToView:(UIView *)view;
- (void)invalidate;
- (BOOL)motionInterpolationAllowed;

@end

NS_ASSUME_NONNULL_END

#endif
