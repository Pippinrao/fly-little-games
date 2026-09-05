#ifndef FLYNES_RUNTIME_BRIDGE_H
#define FLYNES_RUNTIME_BRIDGE_H

#import <Foundation/Foundation.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

@interface FlyNesRuntimeBridge : NSObject

- (BOOL)createRuntime:(NSError * _Nullable * _Nullable)error;
- (BOOL)loadRom:(NSData *)rom error:(NSError * _Nullable * _Nullable)error;
- (BOOL)stepFrameWithButtons:(uint32_t)buttons
                       error:(NSError * _Nullable * _Nullable)error;
- (BOOL)saveCheckpoint:(NSError * _Nullable * _Nullable)error;
- (nullable NSData *)copyLatestRgb565Frame;
- (void)destroyRuntime;

@end

NS_ASSUME_NONNULL_END

#endif
