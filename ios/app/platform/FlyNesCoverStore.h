#ifndef FLYNES_COVER_STORE_H
#define FLYNES_COVER_STORE_H

#import <Foundation/Foundation.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
typedef UIImage FlyNesCoverImage;
#else
#import <AppKit/AppKit.h>
typedef NSImage FlyNesCoverImage;
#endif

NS_ASSUME_NONNULL_BEGIN

/// Posted on the main queue after a new cover is durably stored. `object` is the
/// store and `userInfo[FlyNesCoverStoreCanonicalIdKey]` is the affected game.
extern NSNotificationName const FlyNesCoverStoreDidChangeNotification;
extern NSString *const FlyNesCoverStoreCanonicalIdKey;

/// App-private, atomic cover store mirroring Android `AndroidCoverRepository`.
/// Lives in `covers/v1` under the cache root, names files by
/// `SHA256(canonicalID).png` so neither titles nor device locators leak, converts
/// accepted 256x240 native frames to 320x240, and keeps a 24-image memory cache.
@interface FlyNesCoverStore : NSObject

+ (instancetype)sharedInstance;

/// `root` is the app cache root; covers land in `<root>/covers/v1`.
- (void)configureWithCacheRoot:(NSString *)root;

@property(nonatomic, readonly) NSString *directory;

/// Persist an accepted native RGB565 256x240 frame as a 320x240 PNG. Returns NO
/// when the directory is unusable, the source frame is malformed, or the atomic
/// rename fails.
- (BOOL)storeRgb565Frame:(NSData *)pixels
               canonicalId:(NSString *)canonicalId
                     width:(NSUInteger)width
                    height:(NSUInteger)height;

/// Cached cover for one game, or nil when no acceptable sample was captured yet.
- (nullable FlyNesCoverImage *)coverForCanonicalId:(NSString *)canonicalId;
- (BOOL)hasCoverForCanonicalId:(NSString *)canonicalId;
- (void)removeCoverForCanonicalId:(NSString *)canonicalId;

@end

NS_ASSUME_NONNULL_END

#endif
