#ifndef FLYNES_RUNTIME_BRIDGE_H
#define FLYNES_RUNTIME_BRIDGE_H

#import <Foundation/Foundation.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

@interface FlyNesRuntimeBridge : NSObject

@property(nonatomic, readonly) uint32_t lastFrameSampleCount;

/// Copies the latest RGB565 native game frame together with its authoritative
/// sequence and geometry. Cover capture must use this sequence rather than a
/// view-local counter so samples are taken against produced frames even when
/// display presentation skips them. Any out parameter may be NULL.
- (nullable NSData *)copyLatestRgb565FrameWithSequence:(uint64_t * _Nullable)sequence
                                                width:(uint32_t * _Nullable)width
                                               height:(uint32_t * _Nullable)height;

- (NSData *)pullPCM;
- (void)discardAudio;
- (void)clearInput;
- (BOOL)createRuntime:(NSError * _Nullable * _Nullable)error;
- (BOOL)loadRom:(NSData *)rom error:(NSError * _Nullable * _Nullable)error;
- (BOOL)stepFrameWithButtons:(uint32_t)buttons
                       error:(NSError * _Nullable * _Nullable)error;
- (nullable NSData *)saveCheckpoint:(NSError * _Nullable * _Nullable)error;
- (BOOL)loadCheckpoint:(NSData *)blob error:(NSError * _Nullable * _Nullable)error;
- (nullable NSData *)copyLatestRgb565Frame;
- (void)destroyRuntime;

@end

NS_ASSUME_NONNULL_END

#endif
