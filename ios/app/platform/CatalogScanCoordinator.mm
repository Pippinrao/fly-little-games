#import "CatalogScanCoordinator.h"

#import "FlyNesAppBridge.h"
#import "FlyNesBookmarkStore.h"

#include <flynes/flynes_app.h>

#include <fcntl.h>
#include <unistd.h>

/*
 * Bookmark bytes must never be written into FLYCAT01. After resolving a
 * security-scoped bookmark, this coordinator opens a read-only descriptor and
 * passes it as borrowed_fd to fly_scan_add_file. The catalog row stores only
 * the portable relative path and identity hashes.
 */

@implementation CatalogScanCoordinator {
    FlyNesBookmarkStore *bookmarks_;
}

- (instancetype)init
{
    self = [super init];
    if (self != nil)
        bookmarks_ = [[FlyNesBookmarkStore alloc] init];
    return self;
}

- (BOOL)scanBookmark:(NSData *)bookmark
          sourceUUID:(NSData *)sourceUUID
         sourceScope:(uint32_t)sourceScope
               error:(NSError **)error
{
    if (bookmark.length == 0 || sourceUUID.length != 16)
    {
        if (error != nullptr)
        {
            *error = [NSError errorWithDomain:@"com.flynes.scan"
                                         code:FLY_RESULT_INVALID_ARGUMENT
                                     userInfo:@{NSLocalizedDescriptionKey : @"bookmark or source UUID missing"}];
        }
        return NO;
    }

    BOOL accessing = NO;
    NSURL *url = [bookmarks_ resolveBookmark:bookmark didStartAccess:&accessing error:error];
    if (url.path == nil)
        return NO;

    const char *path_utf8 = url.path.UTF8String;
    int borrowed_fd = path_utf8 != nullptr ? open(path_utf8, O_RDONLY) : -1;
    if (borrowed_fd < 0)
    {
        if (accessing)
            [bookmarks_ stopAccessing:url];
        if (error != nullptr)
        {
            *error = [NSError errorWithDomain:@"com.flynes.scan"
                                         code:FLY_RESULT_INVALID_ARGUMENT
                                     userInfo:@{NSLocalizedDescriptionKey : @"failed to open borrowed_fd"}];
        }
        return NO;
    }

    NSString *displayName = url.lastPathComponent ?: @"rom.nes";
    const BOOL ok = [FlyNesAppBridge.sharedInstance scanBorrowedFd:borrowed_fd
                                                    relativePath:displayName
                                                     displayName:displayName
                                                      sourceUUID:sourceUUID
                                                     sourceScope:sourceScope
                                                           error:error];
    close(borrowed_fd);
    if (accessing)
        [bookmarks_ stopAccessing:url];
    return ok;
}

@end
