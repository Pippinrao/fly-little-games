#import "FlyNesBookmarkStore.h"

static NSString *const kBookmarkMapKey = @"flynes.source_uuid_bookmarks_v1";

@implementation FlyNesBookmarkStore {
    NSMutableDictionary<NSString *, NSData *> *bookmarksByUUID_;
}

- (instancetype)init
{
    self = [super init];
    if (self != nil)
    {
        bookmarksByUUID_ = [NSMutableDictionary dictionary];
        NSDictionary *stored = [[NSUserDefaults standardUserDefaults] dictionaryForKey:kBookmarkMapKey];
        for (NSString *key in stored)
        {
            id value = stored[key];
            if (key.length > 0 && [value isKindOfClass:[NSData class]])
                bookmarksByUUID_[key] = value;
        }
    }
    return self;
}

- (void)persist
{
    [[NSUserDefaults standardUserDefaults] setObject:bookmarksByUUID_ forKey:kBookmarkMapKey];
}

- (void)setBookmark:(NSData *)bookmark forUUID:(NSUUID *)uuid
{
    if (uuid == nil || bookmark.length == 0)
        return;
    bookmarksByUUID_[uuid.UUIDString] = bookmark;
    [self persist];
}

- (NSData *)bookmarkForUUID:(NSUUID *)uuid
{
    if (uuid == nil)
        return nil;
    return bookmarksByUUID_[uuid.UUIDString];
}

- (void)removeBookmarkForUUID:(NSUUID *)uuid
{
    if (uuid == nil)
        return;
    [bookmarksByUUID_ removeObjectForKey:uuid.UUIDString];
    [self persist];
}

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
