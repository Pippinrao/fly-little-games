#import "platform/CatalogSourceService.h"
#import "bridge/FlyNesAppBridge.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <zlib.h>
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static NSData *DuplicateZIP(NSData *first, NSData *second) {
    std::vector<uint8_t> bytes;
    auto u16 = [&](uint16_t value) { bytes.push_back(value & 255); bytes.push_back(value >> 8); };
    auto u32 = [&](uint32_t value) { u16(value & 65535); u16(value >> 16); };
    const std::string name = "game.nes";
    uint32_t offsets[2], crcs[2], sizes[2];
    NSArray *payloads = @[first, second];
    for (NSUInteger i = 0; i < 2; ++i) {
        NSData *data = payloads[i];
        offsets[i] = (uint32_t)bytes.size(); sizes[i] = (uint32_t)data.length;
        crcs[i] = (uint32_t)crc32(0, static_cast<const Bytef *>(data.bytes), sizes[i]);
        u32(0x04034b50); u16(20); u16(0); u16(0); u16(0); u16(0); u32(crcs[i]);
        u32(sizes[i]); u32(sizes[i]); u16(name.size()); u16(0);
        bytes.insert(bytes.end(), name.begin(), name.end());
        const auto *start = static_cast<const uint8_t *>(data.bytes);
        bytes.insert(bytes.end(), start, start + data.length);
    }
    uint32_t centralOffset = (uint32_t)bytes.size();
    for (NSUInteger i = 0; i < 2; ++i) {
        u32(0x02014b50); u16(20); u16(20); u16(0); u16(0); u16(0); u16(0); u32(crcs[i]);
        u32(sizes[i]); u32(sizes[i]); u16(name.size()); u16(0); u16(0); u16(0); u16(0); u32(0); u32(offsets[i]);
        bytes.insert(bytes.end(), name.begin(), name.end());
    }
    uint32_t centralSize = (uint32_t)bytes.size() - centralOffset;
    u32(0x06054b50); u16(0); u16(0); u16(2); u16(2); u32(centralSize); u32(centralOffset); u16(0);
    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}
int main(int argc, char **argv) { @autoreleasepool { try {
    check(argc == 3, "usage: catalog_source_service_test ROM scratch-root");
    NSFileManager *fm = NSFileManager.defaultManager;
    NSURL *root = [NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
    [fm createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:nil];
    NSURL *folder = [root URLByAppendingPathComponent:@"games" isDirectory:YES];
    [fm createDirectoryAtURL:folder withIntermediateDirectories:YES attributes:nil error:nil];
    NSData *rom = [NSData dataWithContentsOfFile:@(argv[1])];
    check(rom.length > 16, "fixture available");
    NSURL *first = [folder URLByAppendingPathComponent:@"first.nes"];
    NSURL *second = [folder URLByAppendingPathComponent:@"second.NES"];
    [rom writeToURL:first atomically:YES];
    NSMutableData *changed = [rom mutableCopy];
    static_cast<uint8_t *>(changed.mutableBytes)[changed.length - 1] ^= 1;
    [changed writeToURL:second atomically:YES];
    [rom writeToURL:[root URLByAppendingPathComponent:@"outside.nes"] atomically:YES];
    [fm createSymbolicLinkAtURL:[folder URLByAppendingPathComponent:@"escape.nes"] withDestinationURL:[root URLByAppendingPathComponent:@"outside.nes"] error:nil];
    NSString *suite = [@"com.flynes.test.sources." stringByAppendingString:NSUUID.UUID.UUIDString];
    NSUserDefaults *defaults = [[NSUserDefaults alloc] initWithSuiteName:suite];
    FlyNesAppBridge *bridge = [[FlyNesAppBridge alloc] init];
    check([bridge createWithDataRoot:root.path cacheRoot:root.path error:nil], "create bridge");
    CatalogSourceService *service = [[CatalogSourceService alloc] initWithBridge:bridge defaults:defaults builtinURL:[NSURL fileURLWithPath:@(argv[1])]];
    NSError *error = nil;
    NSString *uuid = [service addURL:folder directory:YES error:&error];
    if (!uuid) NSLog(@"import error: %@", error);
    check(uuid != nil, "directory import succeeds");
    check([service sources].count == 1, "source metadata stored");
    NSArray *rows = [bridge catalogSnapshotGames];
    check(rows.count == 2, "batch scan preserves two games and skips escaping symlink");
    for (NSDictionary *row in rows) {
        NSData *opened = [service romDataForCanonicalID:row[@"canonicalId"] error:&error];
        if (!opened) NSLog(@"resolve error: %@; row: %@", error, row);
        check(opened != nil && ([opened isEqual:rom] || [opened isEqual:changed]), "exact canonical ROM resolves");
    }
    NSString *firstID = nil;
    for (NSDictionary *row in rows) if ([row[@"relativePath"] isEqual:@"first.nes"]) firstID = row[@"canonicalId"];
    [changed writeToURL:first atomically:YES];
    check([service romDataForCanonicalID:firstID error:&error] == nil && error != nil, "changed package rejected with actionable error");
    service = [[CatalogSourceService alloc] initWithBridge:bridge defaults:defaults builtinURL:[NSURL fileURLWithPath:@(argv[1])]];
    check([service sources].count == 1, "source metadata survives restart");
    check([service rescanUUID:uuid error:&error], "persisted bookmark resolves and rescans");
    [fm removeItemAtURL:folder error:nil];
    NSUInteger before = [bridge catalogSnapshotGames].count;
    check(![service rescanUUID:uuid error:&error], "missing source is visible failure");
    check([bridge catalogSnapshotGames].count == before, "failed scan does not delete previous rows");
    check([[service sources][0][@"error"] length] > 0, "source records reauthorization error");
    NSURL *replacement = [root URLByAppendingPathComponent:@"replacement" isDirectory:YES];
    [fm createDirectoryAtURL:replacement withIntermediateDirectories:YES attributes:nil error:nil];
    [rom writeToURL:[replacement URLByAppendingPathComponent:@"restored.nes"] atomically:YES];
    check([service reauthorizeUUID:uuid URL:replacement error:&error], "source can be reauthorized");
    check([[service sources][0][@"uuid"] isEqual:uuid], "reauthorization preserves UUID");
    check([service removeUUID:uuid error:&error], "source removed");
    check([service sources].count == 0 && [bridge catalogSnapshotGames].count == 0, "source removal clears metadata and rows");
    NSURL *zip = [root URLByAppendingPathComponent:@"duplicate.zip"];
    [DuplicateZIP(rom, changed) writeToURL:zip atomically:YES];
    NSString *zipUUID = [service addURL:zip directory:NO error:&error];
    check(zipUUID != nil, "standalone ZIP imports through bookmark");
    rows = bridge.catalogSnapshotGames;
    check(rows.count == 2, "both duplicate-name ZIP variants are cataloged");
    NSMutableSet *payloads = [NSMutableSet set];
    for (NSDictionary *row in rows) {
        NSData *opened = [service romDataForCanonicalID:row[@"canonicalId"] error:&error];
        check(opened != nil, "real scanned ZIP identity resolves");
        [payloads addObject:opened];
    }
    check([payloads containsObject:rom] && [payloads containsObject:changed], "duplicate entries launch their exact selected payload");
    check([service removeUUID:zipUUID error:&error], "ZIP source removal");
    NSURL *unsupportedFolder = [root URLByAppendingPathComponent:@"unsupported" isDirectory:YES];
    [fm createDirectoryAtURL:unsupportedFolder withIntermediateDirectories:YES attributes:nil error:nil];
    NSMutableData *unif = [NSMutableData dataWithLength:41];
    memcpy(unif.mutableBytes, "UNIF", 4);
    memcpy(static_cast<uint8_t *>(unif.mutableBytes) + 32, "PRG0\1\0\0\0", 8);
    [unif writeToURL:[unsupportedFolder URLByAppendingPathComponent:@"board.UNIF"] atomically:YES];
    NSMutableData *fds = [NSMutableData dataWithLength:16 + 65500];
    memcpy(fds.mutableBytes, "FDS\x1a\1", 5);
    memcpy(static_cast<uint8_t *>(fds.mutableBytes) + 16, "\x01*NINTENDO-HVC*", 15);
    [fds writeToURL:[unsupportedFolder URLByAppendingPathComponent:@"disk.fds"] atomically:YES];
    NSString *unsupportedUUID = [service addURL:unsupportedFolder directory:YES error:&error];
    check(unsupportedUUID != nil, "FDS and UNIF directory scans");
    rows = bridge.catalogSnapshotGames;
    check(rows.count == 2, "supported source extensions include FDS and UNIF");
    for (NSDictionary *row in rows) {
        error = nil;
        check([row[@"compatibilityState"] unsignedIntValue] != 1, "unsupported formats remain visible");
        check([service romDataForCanonicalID:row[@"canonicalId"] error:&error] == nil && error != nil, "unsupported games explain why launch is blocked");
    }
    check([service removeUUID:unsupportedUUID error:&error], "unsupported source removal");
    check([service prepareBuiltin:&error], "builtin is actually scanned");
    rows = [bridge catalogSnapshotGames];
    check(rows.count == 1, "one real builtin game");
    check([rows[0][@"sourceUUID"] isEqual:@"6FC22AA9-81CC-4CBB-A5F3-018A480B0001"], "builtin UUID is stable");
    check([[service romDataForCanonicalID:rows[0][@"canonicalId"] error:&error] isEqual:rom], "builtin resolves through catalog hashes");
    [defaults removePersistentDomainForName:suite];
    std::cout << "ios_catalog_source_service: PASS\n";
} catch (const std::exception& e) { std::cerr << "ios_catalog_source_service: FAIL: " << e.what() << '\n'; return 1; } } }
