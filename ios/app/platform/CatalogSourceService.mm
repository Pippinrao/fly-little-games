#import "CatalogSourceService.h"
#import "FlyNesBookmarkStore.h"
#import "FlyNesAppBridge.h"
#include "RomPackage.hpp"
#include "catalog/bounded_zip_archive.hpp"
#include <exception>

static NSString *const SourceMetadataKey = @"flynes.source_metadata_v1";
static NSString *const BuiltinUUID = @"6FC22AA9-81CC-4CBB-A5F3-018A480B0001";
static NSError *SourceError(NSString *key) {
    return [NSError errorWithDomain:@"com.flynes.sources" code:1 userInfo:@{NSLocalizedDescriptionKey:NSLocalizedString(key, nil)}];
}
static NSData *UUIDBytes(NSString *value) {
    NSUUID *uuid = [[NSUUID alloc] initWithUUIDString:value];
    if (!uuid) return nil;
    uuid_t bytes; [uuid getUUIDBytes:bytes];
    return [NSData dataWithBytes:bytes length:16];
}
static std::string Hex(NSData *bytes) {
    std::string result;
    for (NSUInteger i = 0; i < bytes.length; ++i) {
        auto byte = static_cast<const uint8_t *>(bytes.bytes)[i];
        result += "0123456789ABCDEF"[byte >> 4]; result += "0123456789ABCDEF"[byte & 15];
    }
    return result;
}
static BOOL SupportedURL(NSURL *url) {
    return [@[@"nes", @"zip", @"fds", @"unf", @"unif"] containsObject:url.pathExtension.lowercaseString];
}
static BOOL IsWithin(NSURL *url, NSURL *root) {
    NSString *path = url.URLByResolvingSymlinksInPath.URLByStandardizingPath.path;
    NSString *base = root.URLByResolvingSymlinksInPath.URLByStandardizingPath.path;
    return [path hasPrefix:[base stringByAppendingString:@"/"]];
}

@implementation CatalogSourceService {
    FlyNesAppBridge *bridge_;
    NSUserDefaults *defaults_;
    FlyNesBookmarkStore *bookmarks_;
    NSMutableDictionary<NSString *, NSMutableDictionary *> *metadata_;
    NSURL *builtinURL_;
    BOOL builtinPrepared_;
}
+ (instancetype)sharedInstance {
    static CatalogSourceService *instance; static dispatch_once_t once;
    dispatch_once(&once, ^{
        instance = [[self alloc] initWithBridge:FlyNesAppBridge.sharedInstance defaults:NSUserDefaults.standardUserDefaults builtinURL:[NSBundle.mainBundle URLForResource:@"from_below" withExtension:@"nes"]];
    });
    return instance;
}
- (instancetype)initWithBridge:(FlyNesAppBridge *)bridge defaults:(NSUserDefaults *)defaults builtinURL:(NSURL *)builtinURL {
    if ((self = [super init])) {
        bridge_ = bridge; defaults_ = defaults; builtinURL_ = builtinURL;
        bookmarks_ = [[FlyNesBookmarkStore alloc] initWithDefaults:defaults];
        metadata_ = [NSMutableDictionary dictionary];
        for (NSDictionary *row in [defaults arrayForKey:SourceMetadataKey]) {
            if (![row isKindOfClass:NSDictionary.class] || ![row[@"uuid"] isKindOfClass:NSString.class] || !UUIDBytes(row[@"uuid"])) continue;
            if (![row[@"scope"] isKindOfClass:NSNumber.class] || ![row[@"name"] isKindOfClass:NSString.class]) continue;
            uint32_t scope = [row[@"scope"] unsignedIntValue];
            if (scope != 2 && scope != 3) continue;
            metadata_[row[@"uuid"]] = [row mutableCopy];
        }
    }
    return self;
}
- (void)persist { [defaults_ setObject:metadata_.allValues forKey:SourceMetadataKey]; }
- (NSArray *)sources { @synchronized(self) {
    NSMutableArray *result = [NSMutableArray array];
    for (NSDictionary *item in metadata_.allValues) [result addObject:[item copy]];
    return [result sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *a, NSDictionary *b) { return [a[@"name"] localizedStandardCompare:b[@"name"]]; }];
} }
- (BOOL)validateURL:(NSURL *)url directory:(BOOL)directory error:(NSError **)error {
    NSNumber *isDirectory = nil, *symlink = nil;
    if (!url.isFileURL || ![url getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:error] ||
        ![url getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:error]) return NO;
    if (symlink.boolValue || isDirectory.boolValue != directory) {
        if (error) *error = SourceError(@"library.source.wrong_type"); return NO;
    }
    if (!directory && !SupportedURL(url)) { if (error) *error = SourceError(@"library.source.unsupported_file"); return NO; }
    return YES;
}
- (BOOL)scanURL:(NSURL *)url uuid:(NSString *)uuid scope:(uint32_t)scope error:(NSError **)error {
    __block BOOL success = NO;
    __block NSError *failure = nil;
    NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    NSError *coordinationError = nil;
    [coordinator coordinateReadingItemAtURL:url options:0 error:&coordinationError byAccessor:^(NSURL *root) {
        BOOL directory = scope == 2;
        if (![self validateURL:root directory:directory error:&failure]) return;
        NSMutableArray *records = [NSMutableArray array];
        if (directory) {
            NSDirectoryEnumerator *enumerator = [NSFileManager.defaultManager enumeratorAtURL:root includingPropertiesForKeys:@[NSURLIsRegularFileKey, NSURLIsSymbolicLinkKey] options:NSDirectoryEnumerationSkipsHiddenFiles errorHandler:^BOOL(NSURL *, NSError *problem) { failure = problem; return YES; }];
            if (!enumerator) { failure = SourceError(@"library.source.scan_failed"); return; }
            for (NSURL *file in enumerator) {
                NSNumber *symlink = nil, *regular = nil;
                NSError *readError = nil;
                if (![file getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:&readError] || ![file getResourceValue:&regular forKey:NSURLIsRegularFileKey error:&readError]) { failure = readError; continue; }
                if (symlink.boolValue || !IsWithin(file, root)) { [enumerator skipDescendants]; continue; }
                if (!regular.boolValue || !SupportedURL(file)) continue;
                NSString *relative = [file.URLByResolvingSymlinksInPath.URLByStandardizingPath.path substringFromIndex:root.URLByResolvingSymlinksInPath.URLByStandardizingPath.path.length + 1];
                [records addObject:@{@"url":file, @"relativePath":relative, @"displayName":file.lastPathComponent}];
            }
        } else {
            [records addObject:@{@"url":root, @"relativePath":root.lastPathComponent, @"displayName":root.lastPathComponent}];
        }
        NSError *scanError = nil;
        success = [bridge_ scanFileRecords:records sourceUUID:UUIDBytes(uuid) sourceScope:scope incomplete:failure != nil error:&scanError];
        if (scanError) failure = scanError;
    }];
    if (coordinationError) failure = coordinationError;
    if (!success || failure) {
        // A failed root access never supplies an empty complete transaction.
        // Mark old rows stale while preserving them for reauthorization.
        if (!success) [bridge_ scanFileRecords:@[] sourceUUID:UUIDBytes(uuid) sourceScope:scope incomplete:YES error:nil];
        if (error) *error = failure ?: SourceError(@"library.source.scan_failed");
        return NO;
    }
    return YES;
}
- (BOOL)prepareBuiltin:(NSError **)error { @synchronized(self) {
    if (builtinPrepared_) return YES;
    if (!builtinURL_) { if (error) *error = SourceError(@"library.source.builtin_missing"); return NO; }
    builtinPrepared_ = [self scanURL:builtinURL_ uuid:BuiltinUUID scope:1 error:error];
    return builtinPrepared_;
} }
- (NSString *)addURL:(NSURL *)url directory:(BOOL)directory error:(NSError **)error { @synchronized(self) {
    BOOL accessing = [url startAccessingSecurityScopedResource];
    NSString *uuid = nil;
    @try {
        if (![self validateURL:url directory:directory error:error]) return nil;
        NSData *bookmark = [bookmarks_ bookmarkForURL:url error:error];
        if (!bookmark) return nil;
        uuid = NSUUID.UUID.UUIDString;
        [bookmarks_ setBookmark:bookmark forUUID:[[NSUUID alloc] initWithUUIDString:uuid]];
        metadata_[uuid] = [@{@"uuid":uuid, @"scope":@(directory ? 2 : 3), @"name":url.lastPathComponent, @"error":@""} mutableCopy];
        [self persist];
        NSError *scanError = nil;
        if (![self scanURL:url uuid:uuid scope:directory ? 2 : 3 error:&scanError]) {
            metadata_[uuid][@"error"] = scanError.localizedDescription;
            [self persist];
            if (error) *error = scanError;
            return nil;
        }
        return uuid;
    } @finally { if (accessing) [url stopAccessingSecurityScopedResource]; }
} }
- (BOOL)rescanUUID:(NSString *)uuid error:(NSError **)error { @synchronized(self) {
    NSMutableDictionary *source = metadata_[uuid];
    if (!source) { if (error) *error = SourceError(@"library.source.not_found"); return NO; }
    BOOL access = NO; NSError *failure = nil;
    NSURL *url = [bookmarks_ resolveUUID:[[NSUUID alloc] initWithUUIDString:uuid] didStartAccess:&access error:&failure];
    BOOL success = NO;
    @try {
        if (url) success = [self scanURL:url uuid:uuid scope:[source[@"scope"] unsignedIntValue] error:&failure];
        else [bridge_ scanFileRecords:@[] sourceUUID:UUIDBytes(uuid) sourceScope:[source[@"scope"] unsignedIntValue] incomplete:YES error:nil];
    } @finally { if (access) [bookmarks_ stopAccessing:url]; }
    source[@"error"] = success ? @"" : (failure.localizedDescription ?: NSLocalizedString(@"library.source.reauthorize_required", nil));
    [self persist];
    if (!success && error) *error = failure ?: SourceError(@"library.source.reauthorize_required");
    return success;
} }
- (BOOL)reauthorizeUUID:(NSString *)uuid URL:(NSURL *)url error:(NSError **)error { @synchronized(self) {
    NSMutableDictionary *source = metadata_[uuid];
    if (!source) { if (error) *error = SourceError(@"library.source.not_found"); return NO; }
    BOOL access = [url startAccessingSecurityScopedResource];
    @try {
        if (![self validateURL:url directory:[source[@"scope"] unsignedIntValue] == 2 error:error]) return NO;
        NSData *bookmark = [bookmarks_ bookmarkForURL:url error:error];
        if (!bookmark) return NO;
        [bookmarks_ setBookmark:bookmark forUUID:[[NSUUID alloc] initWithUUIDString:uuid]];
        source[@"name"] = url.lastPathComponent;
        [self persist];
        return [self rescanUUID:uuid error:error];
    } @finally { if (access) [url stopAccessingSecurityScopedResource]; }
} }
- (BOOL)removeUUID:(NSString *)uuid error:(NSError **)error { @synchronized(self) {
    NSDictionary *source = metadata_[uuid];
    if (!source) { if (error) *error = SourceError(@"library.source.not_found"); return NO; }
    if (![bridge_ removeSourceUUID:UUIDBytes(uuid) scope:[source[@"scope"] unsignedIntValue] error:error]) return NO;
    [metadata_ removeObjectForKey:uuid];
    [bookmarks_ removeBookmarkForUUID:[[NSUUID alloc] initWithUUIDString:uuid]];
    [self persist]; return YES;
} }
- (NSData *)romDataForCanonicalID:(NSString *)canonicalID error:(NSError **)error { @synchronized(self) {
    NSDictionary *selected = nil;
    for (NSDictionary *row in bridge_.catalogSnapshotGames) if ([row[@"canonicalId"] isEqual:canonicalID]) { selected = row; break; }
    if (!selected) { if (error) *error = SourceError(@"library.source.game_missing"); return nil; }
    if ([selected[@"compatibilityState"] unsignedIntValue] != 1) { if (error) *error = SourceError(@"library.source.game_unsupported"); return nil; }
    if ([selected[@"freshness"] unsignedIntValue] != 1) { if (error) *error = SourceError(@"library.source.game_stale"); return nil; }
    NSString *uuid = selected[@"sourceUUID"];
    uint32_t scope = [selected[@"sourceScope"] unsignedIntValue];
    BOOL access = NO;
    NSURL *root = nil;
    if (scope == 1 && [uuid isEqual:BuiltinUUID]) root = builtinURL_;
    else if ([metadata_[uuid][@"scope"] unsignedIntValue] == scope) root = [bookmarks_ resolveUUID:[[NSUUID alloc] initWithUUIDString:uuid] didStartAccess:&access error:error];
    if (!root) { if (error && !*error) *error = SourceError(@"library.source.reauthorize_required"); return nil; }
    @try {
        NSString *relative = selected[@"relativePath"];
        if (relative.length == 0 || relative.isAbsolutePath || [relative.pathComponents containsObject:@".."]) { if (error) *error = SourceError(@"library.source.game_stale"); return nil; }
        NSURL *file = scope == 2 ? [root URLByAppendingPathComponent:relative] : root;
        if ((scope == 2 && !IsWithin(file, root)) || (scope != 2 && ![file.lastPathComponent isEqual:relative])) { if (error) *error = SourceError(@"library.source.game_stale"); return nil; }
        __block NSData *result = nil;
        __block NSError *failure = nil;
        NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
        NSError *coordinationError = nil;
        [coordinator coordinateReadingItemAtURL:file options:0 error:&coordinationError byAccessor:^(NSURL *coordinated) {
            if (scope == 2 && !IsWithin(coordinated, root)) { failure = SourceError(@"library.source.game_stale"); return; }
            NSNumber *size = nil, *regular = nil, *link = nil;
            if (![coordinated getResourceValue:&size forKey:NSURLFileSizeKey error:&failure] ||
                ![coordinated getResourceValue:&regular forKey:NSURLIsRegularFileKey error:&failure] ||
                ![coordinated getResourceValue:&link forKey:NSURLIsSymbolicLinkKey error:&failure]) return;
            const auto limit = flynes::catalog::BoundedZipLimits::defaults().max_package_bytes();
            if (!regular.boolValue || link.boolValue || size.unsignedLongLongValue == 0 || size.unsignedLongLongValue > limit) { failure = SourceError(@"library.source.game_unsupported"); return; }
            // Read at most the shared package budget even if an uncoordinated writer grows the file.
            NSInputStream *stream = [NSInputStream inputStreamWithURL:coordinated];
            [stream open];
            NSMutableData *package = [NSMutableData data];
            uint8_t buffer[65536];
            while (true) {
                NSInteger count = [stream read:buffer maxLength:sizeof(buffer)];
                if (count < 0) { failure = stream.streamError ?: SourceError(@"library.rom_open_failed"); break; }
                if (count == 0) break;
                if (package.length + count > limit) { failure = SourceError(@"library.source.game_unsupported"); break; }
                [package appendBytes:buffer length:count];
            }
            [stream close];
            if (failure) return;
            NSString *sourceID = [NSString stringWithFormat:@"source:%u:%@", scope, [[uuid stringByReplacingOccurrencesOfString:@"-" withString:@""] uppercaseString]];
            try {
                auto bytes = flynes::ios::resolve_rom_package(static_cast<const uint8_t *>(package.bytes), package.length, [selected[@"packageFormat"] unsignedIntValue], sourceID.UTF8String, relative.UTF8String, [selected[@"variantId"] UTF8String], Hex(selected[@"payloadSHA256"]), Hex(selected[@"physicalSHA256"]));
                result = [NSData dataWithBytes:bytes.data() length:bytes.size()];
            } catch (const std::exception&) { failure = SourceError(@"library.source.game_stale"); }
        }];
        if (!result && error) *error = failure ?: coordinationError ?: SourceError(@"library.rom_open_failed");
        return result;
    } @finally { if (access) [bookmarks_ stopAccessing:root]; }
} }
@end
