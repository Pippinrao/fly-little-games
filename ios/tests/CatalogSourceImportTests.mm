// Drives the real source-import pipeline: a directory source, a ZIP package with a
// Chinese entry name, duplicate suppression, cancellation, persistence across a
// restart, rescan, and removal. Only the system Files picker's tap is out of scope
// here; everything the picker hands to the app is exercised.
#import <XCTest/XCTest.h>
#import "../app/platform/CatalogSourceService.h"
#import "../app/bridge/FlyNesAppBridge.h"
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

/// The simulator sandbox has no NSTask, so the fixture ZIP is produced by spawning
/// `/usr/bin/zip` directly.
static int run_zip(NSString *workingDirectory, NSArray<NSString *> *arguments)
{
    NSMutableArray<NSString *> *values = [NSMutableArray arrayWithObject:@"/usr/bin/zip"];
    [values addObjectsFromArray:arguments];
    char *argv[8] = {};
    for (NSUInteger index = 0; index < values.count && index < 7u; ++index)
        argv[index] = const_cast<char *>(values[index].UTF8String);
    NSString *previous = NSFileManager.defaultManager.currentDirectoryPath;
    [NSFileManager.defaultManager changeCurrentDirectoryPath:workingDirectory];
    pid_t child = 0;
    const int spawned = posix_spawn(&child, "/usr/bin/zip", nullptr, nullptr, argv, environ);
    [NSFileManager.defaultManager changeCurrentDirectoryPath:previous];
    if (spawned != 0)
        return spawned;
    int status = 0;
    if (waitpid(child, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

@interface CatalogSourceImportTests : XCTestCase
@end

@implementation CatalogSourceImportTests {
    NSURL *root_;
    NSURL *sources_;
    FlyNesAppBridge *bridge_;
    CatalogSourceService *service_;
    NSUserDefaults *defaults_;
}

- (NSData *)romFixture
{
    NSURL *url = [NSBundle.mainBundle URLForResource:@"from_below" withExtension:@"nes"];
    NSData *rom = url == nil ? nil : [NSData dataWithContentsOfURL:url];
    XCTAssertNotNil(rom, @"the repository ROM fixture must be bundled for this test");
    return rom;
}

- (void)setUp
{
    [super setUp];
    root_ = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]
                       isDirectory:YES];
    sources_ = [root_ URLByAppendingPathComponent:@"sources" isDirectory:YES];
    [NSFileManager.defaultManager createDirectoryAtURL:sources_ withIntermediateDirectories:YES
                                            attributes:nil error:nil];
    defaults_ = [NSUserDefaults standardUserDefaults];
    for (NSString *key in @[@"flynes.source_metadata_v1", @"flynes.source_uuid_bookmarks_v1"])
        [defaults_ removeObjectForKey:key];
    bridge_ = [[FlyNesAppBridge alloc] init];
    XCTAssertTrue([bridge_ createWithDataRoot:root_.path cacheRoot:root_.path error:nil]);
    service_ = [[CatalogSourceService alloc] initWithBridge:bridge_ defaults:defaults_
                                                 builtinURL:nil];
}

- (void)tearDown
{
    for (NSString *key in @[@"flynes.source_metadata_v1", @"flynes.source_uuid_bookmarks_v1"])
        [defaults_ removeObjectForKey:key];
    [NSFileManager.defaultManager removeItemAtURL:root_ error:nil];
    [super tearDown];
}

/// A ZIP whose entry name must supply the Chinese title while the outer package name
/// supplies the English one, matching Android CanonicalGame candidate ranking.
- (NSURL *)writeZipNamed:(NSString *)packageName entry:(NSString *)entryName rom:(NSData *)rom
{
    NSURL *staging = [root_ URLByAppendingPathComponent:@"staging" isDirectory:YES];
    NSURL *inner = [staging URLByAppendingPathComponent:@"folder" isDirectory:YES];
    [NSFileManager.defaultManager createDirectoryAtURL:inner withIntermediateDirectories:YES
                                            attributes:nil error:nil];
    NSURL *entry = [inner URLByAppendingPathComponent:entryName];
    XCTAssertTrue([rom writeToURL:entry atomically:YES]);
    NSURL *package = [sources_ URLByAppendingPathComponent:packageName];
    const int status = run_zip(staging.path, @[@"-q", @"-r", package.path, @"folder"]);
    XCTAssertEqual(status, 0, @"zip exited with %d", status);
    [NSFileManager.defaultManager removeItemAtURL:staging error:nil];
    return package;
}

- (NSDictionary *)rowForCanonicalID:(NSString *)canonicalId
{
    for (NSDictionary *row in [bridge_ catalogSnapshotGames])
        if ([row[@"canonicalId"] isEqual:canonicalId]) return row;
    return nil;
}

- (void)testDirectoryImportIndexesZipAndChineseFilenamesAndSurvivesRestart
{
    NSData *rom = [self romFixture];
    XCTAssertTrue([rom writeToURL:[sources_ URLByAppendingPathComponent:@"超级游戏.nes"] atomically:YES]);
    [self writeZipNamed:@"Collection.zip" entry:@"来自下方 (USA).nes" rom:rom];
    // Unsupported payloads must be ignored rather than failing the whole scan.
    XCTAssertTrue([@"not a rom" writeToURL:[sources_ URLByAppendingPathComponent:@"notes.txt"]
                                atomically:YES encoding:NSUTF8StringEncoding error:nil]);

    NSError *failure = nil;
    NSString *uuid = [service_ addURL:sources_ directory:YES error:&failure];
    XCTAssertNotNil(uuid, @"directory import failed: %@", failure);
    XCTAssertEqual(service_.sources.count, 1u);
    XCTAssertEqualObjects(service_.sources.firstObject[@"name"], @"sources");

    NSArray<NSDictionary *> *rows = [bridge_ catalogSnapshotGames];
    XCTAssertEqual(rows.count, 2u, @"one raw ROM and one ZIP entry expected: %@", rows);
    // Identical payloads share a canonical id, so the Game Center shows one card whose
    // presentation is merged across both source variants, exactly like Android
    // GameCatalog.Group.mergeCanonicalMetadata.
    NSArray<NSDictionary *> *cards = [bridge_ gameCenterFilteredGamesForCategory:@"ALL" query:@""];
    XCTAssertEqual(cards.count, 1u, @"the Game Center must show a canonical game once");
    NSDictionary *card = cards.firstObject;
    NSArray<NSString *> *english = @[@"Collection", @"超级游戏"];
    NSArray<NSString *> *chinese = @[@"来自下方 (USA)", @"超级游戏"];
    XCTAssertTrue([english containsObject:card[@"titleEn"]],
                  @"English title must come from a real filename candidate: %@", card[@"titleEn"]);
    XCTAssertTrue([chinese containsObject:card[@"titleZhHans"]],
                  @"Chinese title must come from a real filename candidate: %@", card[@"titleZhHans"]);
    XCTAssertTrue([card[@"searchAliases"] containsString:@"超级游戏"],
                  @"the other variant's filename must stay searchable: %@", card[@"searchAliases"]);
    XCTAssertTrue([card[@"searchAliases"] containsString:@"Collection.zip"]);
    XCTAssertTrue([card[@"searchAliases"] containsString:@"来自下方 (USA)"]);

    for (NSDictionary *row in rows) {
        XCTAssertEqual([row[@"freshness"] unsignedIntValue], 1u);
        XCTAssertEqual([row[@"compatibilityState"] unsignedIntValue], 1u);
    }

    // Both games must resolve back to real ROM bytes through the persisted source.
    for (NSDictionary *row in rows) {
        NSData *payload = [service_ romDataForCanonicalID:row[@"canonicalId"] error:&failure];
        XCTAssertNotNil(payload, @"cannot reopen %@: %@", row[@"titleEn"], failure);
        XCTAssertEqual(payload.length, rom.length, @"resolved payload differs for %@", row[@"titleEn"]);
    }

    // A favorite and play marker must survive a restart with the catalog.
    NSString *canonical = rows.firstObject[@"canonicalId"];
    XCTAssertTrue([bridge_ setFavorite:YES canonicalID:canonical error:nil]);
    XCTAssertTrue([bridge_ markPlayedCanonicalID:canonical error:nil]);

    bridge_ = [[FlyNesAppBridge alloc] init];
    XCTAssertTrue([bridge_ createWithDataRoot:root_.path cacheRoot:root_.path error:nil]);
    CatalogSourceService *reopened = [[CatalogSourceService alloc] initWithBridge:bridge_
                                                                        defaults:defaults_
                                                                      builtinURL:nil];
    XCTAssertEqual(reopened.sources.count, 1u, @"source metadata must persist");
    NSArray<NSDictionary *> *restored = [bridge_ catalogSnapshotGames];
    XCTAssertEqual(restored.count, 2u, @"catalog must persist across a restart");
    NSDictionary *restoredRow = [self rowForCanonicalID:canonical];
    XCTAssertNotNil(restoredRow);
    XCTAssertEqual([restoredRow[@"favorite"] unsignedIntValue], 1u);
    XCTAssertGreaterThan([restoredRow[@"lastPlayedSequence"] unsignedLongLongValue], 0ull);

    // Rescan keeps the same rows; removing the source clears them.
    NSError *rescanFailure = nil;
    XCTAssertTrue([reopened rescanUUID:uuid error:&rescanFailure], @"%@", rescanFailure);
    XCTAssertEqual([bridge_ catalogSnapshotGames].count, 2u);
    XCTAssertTrue([reopened removeUUID:uuid error:&rescanFailure], @"%@", rescanFailure);
    XCTAssertEqual([bridge_ catalogSnapshotGames].count, 0u);
    XCTAssertEqual(reopened.sources.count, 0u);
}

- (void)testZipPackageUsesOuterNameForEnglishAndEntryNameForChinese
{
    NSData *rom = [self romFixture];
    NSURL *package = [self writeZipNamed:@"Collection.zip" entry:@"来往下方的冒险 (USA).nes" rom:rom];
    NSError *failure = nil;
    XCTAssertNotNil([service_ addURL:package directory:NO error:&failure], @"%@", failure);
    NSArray<NSDictionary *> *cards = [bridge_ gameCenterFilteredGamesForCategory:@"ALL" query:@""];
    XCTAssertEqual(cards.count, 1u);
    // Android RomPackageScanner ranks the outer filename above the ZIP entry name for
    // the same language, and the entry name is the only Chinese candidate.
    XCTAssertEqualObjects(cards.firstObject[@"titleEn"], @"Collection");
    XCTAssertEqualObjects(cards.firstObject[@"titleZhHans"], @"来往下方的冒险 (USA)");
}

- (void)testDuplicateGamesAreCanonicalizedToOneRow
{
    NSData *rom = [self romFixture];
    XCTAssertTrue([rom writeToURL:[sources_ URLByAppendingPathComponent:@"first.nes"] atomically:YES]);
    XCTAssertTrue([rom writeToURL:[sources_ URLByAppendingPathComponent:@"second.nes"] atomically:YES]);
    NSError *failure = nil;
    XCTAssertNotNil([service_ addURL:sources_ directory:YES error:&failure], @"%@", failure);
    // Identical payloads share a canonical id but remain independent variants, so the
    // snapshot keeps both and the Game Center collapses them to one selectable card.
    NSArray<NSDictionary *> *rows = [bridge_ catalogSnapshotGames];
    XCTAssertEqual(rows.count, 2u);
    XCTAssertEqualObjects(rows[0][@"canonicalId"], rows[1][@"canonicalId"]);
    XCTAssertNotEqualObjects(rows[0][@"variantId"], rows[1][@"variantId"]);
    NSArray<NSDictionary *> *cards = [bridge_ gameCenterFilteredGamesForCategory:@"ALL" query:@""];
    XCTAssertEqual(cards.count, 1u, @"the Game Center must show a canonical game once");
}

- (void)testCancelledAndUnsupportedImportsAreRefused
{
    NSData *rom = [self romFixture];
    NSURL *text = [sources_ URLByAppendingPathComponent:@"notes.txt"];
    XCTAssertTrue([@"not a rom" writeToURL:text atomically:YES encoding:NSUTF8StringEncoding error:nil]);
    NSError *failure = nil;
    XCTAssertNil([service_ addURL:text directory:NO error:&failure],
                 @"an unsupported extension must be refused");
    XCTAssertNotNil(failure);
    XCTAssertEqual(service_.sources.count, 0u);

    // A directory offered as a single file is the wrong type.
    failure = nil;
    XCTAssertNil([service_ addURL:sources_ directory:NO error:&failure]);
    XCTAssertNotNil(failure);
    XCTAssertEqual(service_.sources.count, 0u);

    // A single supported file is imported as its own source.
    NSURL *single = [sources_ URLByAppendingPathComponent:@"rom.nes"];
    XCTAssertTrue([rom writeToURL:single atomically:YES]);
    failure = nil;
    NSString *uuid = [service_ addURL:single directory:NO error:&failure];
    XCTAssertNotNil(uuid, @"%@", failure);
    XCTAssertEqual(service_.sources.count, 1u);
    XCTAssertEqual([bridge_ catalogSnapshotGames].count, 1u);

    // Removing a source that was never added reports instead of silently succeeding.
    failure = nil;
    XCTAssertFalse([service_ removeUUID:NSUUID.UUID.UUIDString error:&failure]);
    XCTAssertNotNil(failure);
}

@end
