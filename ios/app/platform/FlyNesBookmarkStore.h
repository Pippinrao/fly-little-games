#ifndef FLYNES_BOOKMARK_STORE_H
#define FLYNES_BOOKMARK_STORE_H

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Security-scoped bookmarks stay in platform code. They are never catalog rows.
@interface FlyNesBookmarkStore : NSObject

- (nullable NSData *)bookmarkForURL:(NSURL *)url error:(NSError * _Nullable * _Nullable)error;
- (nullable NSURL *)resolveBookmark:(NSData *)bookmark
                    didStartAccess:(BOOL * _Nullable)didStartAccess
                             error:(NSError * _Nullable * _Nullable)error;
- (void)stopAccessing:(NSURL *)url;

@end

NS_ASSUME_NONNULL_END

#endif
