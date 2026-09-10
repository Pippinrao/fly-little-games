#import "FlyNesBookmarkStore.h"
#import <TargetConditionals.h>

static NSString *const kBookmarkMapKey = @"flynes.source_uuid_bookmarks_v1";

@implementation FlyNesBookmarkStore {
    NSMutableDictionary<NSString *, NSData *> *bookmarksByUUID_;
    NSUserDefaults *defaults_;
}

- (instancetype)init
{
    return [self initWithDefaults:NSUserDefaults.standardUserDefaults];
}

- (instancetype)initWithDefaults:(NSUserDefaults *)defaults
{
    self = [super init];
    if (self != nil)
    {
        bookmarksByUUID_ = [NSMutableDictionary dictionary];
        defaults_ = defaults;
        NSDictionary *stored = [defaults_ dictionaryForKey:kBookmarkMapKey];
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
    [defaults_ setObject:bookmarksByUUID_ forKey:kBookmarkMapKey];
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
    NSURLBookmarkCreationOptions options = 0;
#if TARGET_OS_OSX
    // Unsandboxed Foundation hosts do not have an app-scope signing key.
    options = NSBundle.mainBundle.bundleIdentifier ? NSURLBookmarkCreationWithSecurityScope : 0;
#endif
    NSData *bookmark = [url bookmarkDataWithOptions:options
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
    if (didStartAccess) *didStartAccess = NO;
    if (bookmark.length == 0)
        return nil;
    BOOL stale = NO;
    NSURLBookmarkResolutionOptions options = 0;
#if TARGET_OS_OSX
    options = NSBundle.mainBundle.bundleIdentifier ? NSURLBookmarkResolutionWithSecurityScope : 0;
#endif
    NSURL *url = [NSURL URLByResolvingBookmarkData:bookmark
                                           options:options
                                     relativeToURL:nil
                               bookmarkDataIsStale:&stale
                                             error:error];
    if (url == nil)
        return nil;
    if (stale) {
        if (error) *error = [NSError errorWithDomain:@"com.flynes.bookmark" code:1 userInfo:@{NSLocalizedDescriptionKey:NSLocalizedString(@"library.source.reauthorize_required", nil)}];
        return nil;
    }
    const BOOL accessing = [url startAccessingSecurityScopedResource];
    if (didStartAccess != nullptr)
        *didStartAccess = accessing;
    return url;
}

- (NSURL *)resolveUUID:(NSUUID *)uuid didStartAccess:(BOOL *)didStartAccess error:(NSError **)error
{
    if (didStartAccess) *didStartAccess = NO;
    NSData *bookmark = [self bookmarkForUUID:uuid];
    if (bookmark.length == 0) {
        if (error) *error = [NSError errorWithDomain:@"com.flynes.bookmark" code:1 userInfo:@{NSLocalizedDescriptionKey:NSLocalizedString(@"library.source.reauthorize_required", nil)}];
        return nil;
    }
    BOOL stale = NO;
    NSURLBookmarkResolutionOptions options = 0;
#if TARGET_OS_OSX
    options = NSBundle.mainBundle.bundleIdentifier ? NSURLBookmarkResolutionWithSecurityScope : 0;
#endif
    NSURL *url = [NSURL URLByResolvingBookmarkData:bookmark options:options relativeToURL:nil bookmarkDataIsStale:&stale error:error];
    if (!url) return nil;
    BOOL access = [url startAccessingSecurityScopedResource];
    if (stale) {
        NSData *renewed = [self bookmarkForURL:url error:error];
        if (!renewed) { if (access) [url stopAccessingSecurityScopedResource]; return nil; }
        [self setBookmark:renewed forUUID:uuid];
    }
    if (didStartAccess) *didStartAccess = access;
    return url;
}

- (void)stopAccessing:(NSURL *)url
{
    [url stopAccessingSecurityScopedResource];
}

@end
