#ifndef FLYNES_METAL_RENDERER_H
#define FLYNES_METAL_RENDERER_H

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, FlyNesSpatialMode) {
    FlyNesSpatialNearest = 1,
    FlyNesSpatialSharpBilinear = 2,
    FlyNesSpatialMmpx = 3,
    FlyNesSpatialScalefx = 4,
};

typedef NS_ENUM(NSInteger, FlyNesPostEffect) {
    FlyNesPostNone = 1,
    FlyNesPostCrt = 2,
};

typedef NS_ENUM(NSInteger, FlyNesAspectMode) {
    FlyNesAspectFourByThree = 1,
    FlyNesAspectSquarePixels = 2,
    FlyNesAspectIntegerScale = 3,
};

@interface FlyNesMetalRenderer : NSObject

- (instancetype)initWithLayer:(CAMetalLayer *)layer;
- (void)setSpatialMode:(FlyNesSpatialMode)spatial postEffect:(FlyNesPostEffect)post;
- (void)setAspectMode:(FlyNesAspectMode)aspect;
- (BOOL)uploadRgb565:(NSData *)pixels width:(int)width height:(int)height;
- (BOOL)draw;

@end

NS_ASSUME_NONNULL_END

#endif
