#import "bridge/FlyNesAppBridge.h"
#include <iostream>
#include <stdexcept>

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    @autoreleasepool { try {
        check(argc == 3, "usage: catalog_bridge_test ROM scratch-root");
        NSURL *root = [NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        NSFileManager *files = NSFileManager.defaultManager;
        [files createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:nil];
        auto app = [[FlyNesAppBridge alloc] init];
        check([app createWithDataRoot:root.path cacheRoot:root.path error:nil], "create app");
        NSData *rom = [NSData dataWithContentsOfFile:@(argv[1])];
        check(rom.length > 16, "fixture missing");
        NSURL *first = [root URLByAppendingPathComponent:@"first.nes"];
        NSURL *second = [root URLByAppendingPathComponent:@"second.nes"];
        [rom writeToURL:first atomically:YES];
        NSMutableData *modified = [rom mutableCopy];
        static_cast<uint8_t *>(modified.mutableBytes)[modified.length - 1] ^= 1;
        [modified writeToURL:second atomically:YES];
        uuid_t uuid; [[[NSUUID alloc] initWithUUIDString:@"88A168F6-7F84-4C97-A43B-7550083E0123"] getUUIDBytes:uuid];
        NSData *source = [NSData dataWithBytes:uuid length:16];
        NSArray *records = @[
            @{@"url":first, @"relativePath":@"first.nes", @"displayName":@"first.nes"},
            @{@"url":second, @"relativePath":@"second.nes", @"displayName":@"second.nes"}];
        check([app scanFileRecords:records sourceUUID:source sourceScope:2 incomplete:NO error:nil], "batch source scan");
        NSArray *rows = [app catalogSnapshotGames];
        check(rows.count == 2, "complete directory scan retains both games");
        NSString *canonical = rows[0][@"canonicalId"];
        check([app setFavorite:YES canonicalID:canonical error:nil], "favorite mutation");
        check([app markPlayedCanonicalID:canonical error:nil], "played mutation");
        check([app scanFileRecords:@[] sourceUUID:source sourceScope:2 incomplete:YES error:nil], "partial scan publishes stale state");
        check([app catalogSnapshotGames].count == 2, "partial scan preserves old entries");
        app = nil;
        app = [[FlyNesAppBridge alloc] init];
        check([app createWithDataRoot:root.path cacheRoot:root.path error:nil], "reopen app");
        BOOL favorite = NO;
        for (NSDictionary *row in [app catalogSnapshotGames]) {
            if ([row[@"canonicalId"] isEqual:canonical]) {
                favorite = [row[@"favorite"] boolValue] && [row[@"lastPlayedSequence"] unsignedLongLongValue] > 0;
            }
        }
        check(favorite, "favorite and recent persist across restart");
        check([app removeSourceUUID:source scope:2 error:nil], "remove source");
        check([app catalogSnapshotGames].count == 0, "removed source disappears");
        check([app applySettings:@{@"button_scale":@1.25, @"joystick_scale":@1.1,
            @"dead_zone":@0.25, @"vertical_offset":@0.1} error:nil], "apply control settings");
        NSDictionary *settings = [app settingsGet];
        check(std::abs([settings[@"button_scale"] floatValue] - 1.25f) < .001f
            && std::abs([settings[@"joystick_scale"] floatValue] - 1.1f) < .001f
            && std::abs([settings[@"dead_zone"] floatValue] - .25f) < .001f,
            "control settings are applied, not silently ignored");
        NSURL *badZip = [root URLByAppendingPathComponent:@"broken.zip"];
        [@"PK\003\004broken" writeToURL:badZip atomically:YES encoding:NSUTF8StringEncoding error:nil];
        NSError *scanWarning = nil;
        check(![app scanFileRecords:@[@{@"url":badZip,@"relativePath":@"broken.zip",@"displayName":@"broken.zip"}]
            sourceUUID:source sourceScope:2 incomplete:NO error:&scanWarning], "invalid ZIP must be reported");
        check([scanWarning.userInfo[@"scanCommitted"] boolValue], "warning preserves completed scan transaction");
        [app removeSourceUUID:source scope:2 error:nil];
        std::cout << "ios_catalog_bridge: PASS\n";
    } catch (const std::exception& error) { std::cerr << "ios_catalog_bridge: FAIL: " << error.what() << '\n'; return 1; } }
}
