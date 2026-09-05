#ifndef FLYNES_APP_BRIDGE_H
#define FLYNES_APP_BRIDGE_H

#import <Foundation/Foundation.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

@interface FlyNesAppBridge : NSObject

+ (instancetype)sharedInstance;

- (BOOL)createWithDataRoot:(NSString *)dataRoot
                 cacheRoot:(NSString *)cacheRoot
                     error:(NSError * _Nullable * _Nullable)error;

- (NSDictionary<NSString *, id> *)settingsGet;
- (BOOL)applySettings:(NSDictionary<NSString *, id> *)settings
                error:(NSError * _Nullable * _Nullable)error;

- (NSString *)controlLayoutGet;
- (BOOL)controlLayoutApply:(NSString *)utf8
                     error:(NSError * _Nullable * _Nullable)error;

- (NSArray<NSDictionary<NSString *, id> *> *)catalogSnapshotGames;
- (NSArray<NSDictionary<NSString *, id> *> *)gameCenterFilteredGamesForCategory:(NSString *)category
                                                                         query:(NSString *)query;

- (BOOL)scanBorrowedFd:(int)borrowedFd
          relativePath:(NSString *)relativePath
           displayName:(NSString *)displayName
            sourceUUID:(NSData *)sourceUUID
           sourceScope:(uint32_t)sourceScope
                 error:(NSError * _Nullable * _Nullable)error;

@end

NS_ASSUME_NONNULL_END

#endif
