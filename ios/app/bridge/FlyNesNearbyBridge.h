#ifndef FLYNES_NEARBY_BRIDGE_H
#define FLYNES_NEARBY_BRIDGE_H

#import <Foundation/Foundation.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

/// Process-scoped owner of the shared LAN MVP session. UI layers remain native;
/// protocol, role, lockstep, pause and lobby transitions stay in shared code.
@interface FlyNesNearbyBridge : NSObject

@property(class, nonatomic, readonly) FlyNesNearbyBridge *sharedInstance;
@property(nonatomic, readonly, copy) NSString *gameTitle;
@property(nonatomic, readonly, copy) NSString *canonicalId;

- (BOOL)startHost:(NSError * _Nullable * _Nullable)error;
- (BOOL)joinInvite:(NSString *)invite error:(NSError * _Nullable * _Nullable)error;
- (nullable NSString *)inviteText;
- (NSDictionary<NSString *, id> *)snapshot;
- (NSString *)configureLocalGameIfNeeded:(NSError * _Nullable * _Nullable)error;
- (BOOL)confirm;
- (BOOL)setPaused:(BOOL)paused;
- (BOOL)returnLobby;
- (BOOL)stepWithButtons:(uint32_t)buttons;
- (nullable NSData *)copyLatestRgb565Frame;
- (NSData *)pullPCM;
- (void)cancel;

@end

NS_ASSUME_NONNULL_END

#endif
