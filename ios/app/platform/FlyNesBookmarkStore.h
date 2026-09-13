#ifndef FLYNES_BOOKMARK_STORE_H
#define FLYNES_BOOKMARK_STORE_H

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// UUID→bookmark map. Security-scoped bookmarks stay in platform code and are
/// never catalog rows.
@interface FlyNesBookmarkStore : NSObject
- (instancetype)initWithDefaults:(NSUserDefaults *)defaults;

- (void)setBookmark:(NSData *)bookmark forUUID:(NSUUID *)uuid;
- (nullable NSData *)bookmarkForUUID:(NSUUID *)uuid;
- (void)removeBookmarkForUUID:(NSUUID *)uuid;

- (nullable NSData *)bookmarkForURL:(NSURL *)url error:(NSError * _Nullable * _Nullable)error;
- (nullable NSURL *)resolveBookmark:(NSData *)bookmark
                    didStartAccess:(BOOL * _Nullable)didStartAccess
                             error:(NSError * _Nullable * _Nullable)error;
- (void)stopAccessing:(NSURL *)url;
- (nullable NSURL *)resolveUUID:(NSUUID *)uuid
                didStartAccess:(BOOL * _Nullable)didStartAccess
                         error:(NSError * _Nullable * _Nullable)error;

@end

NS_ASSUME_NONNULL_END

#endif
