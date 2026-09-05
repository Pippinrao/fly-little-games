#import "FlyNesBookmarkStore.h"

@implementation FlyNesBookmarkStore

- (NSData *)bookmarkForURL:(NSURL *)url error:(NSError **)error
{
    if (url == nil)
        return nil;
    BOOL accessing = [url startAccessingSecurityScopedResource];
    NSData *bookmark = [url bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope
                     includingResourceValuesForKeys:nil
                                      relativeToURL:nil
                                              error:error];
    if (accessing)
        [url stopAccessingSecurityScopedResource];
    return bookmark;
}

- (NSURL *)resolveBookmark:(NSData *)bookmark
            didStartAccess:(BOOL *)didStartAccess
                     error:(NSError **)error
{
    if (bookmark.length == 0)
        return nil;
    BOOL stale = NO;
    NSURL *url = [NSURL URLByResolvingBookmarkData:bookmark
                                           options:NSURLBookmarkResolutionWithSecurityScope
                                     relativeToURL:nil
                               bookmarkDataIsStale:&stale
                                             error:error];
    if (url == nil)
        return nil;
    const BOOL accessing = [url startAccessingSecurityScopedResource];
    if (didStartAccess != nullptr)
        *didStartAccess = accessing;
    return url;
}

- (void)stopAccessing:(NSURL *)url
{
    [url stopAccessingSecurityScopedResource];
}

@end
