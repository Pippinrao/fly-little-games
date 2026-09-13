#import <XCTest/XCTest.h>
#import "../app/platform/FlyNesCoverStore.h"
#import "../app/platform/CatalogPresentation.h"
#include "../app/platform/CoverCapturePolicy.hpp"

namespace {

/// A 256x240 RGB565 frame with an exact, recognizable pattern so the stored PNG
/// conversion can be verified pixel by pixel.
NSData *pattern_frame()
{
    NSMutableData *pixels = [NSMutableData dataWithLength:256u * 240u * 2u];
    uint8_t *bytes = static_cast<uint8_t *>(pixels.mutableBytes);
    for (NSUInteger y = 0; y < 240; ++y)
    {
        for (NSUInteger x = 0; x < 256; ++x)
        {
            const unsigned red = (x / 8u) % 32u;
            const unsigned green = (y / 8u) % 64u;
            const unsigned blue = (x + y) % 32u;
            const unsigned value = (red << 11) | (green << 5) | blue;
            const size_t index = (y * 256u + x) * 2u;
            bytes[index] = static_cast<uint8_t>(value & 0xFFu);
            bytes[index + 1u] = static_cast<uint8_t>(value >> 8);
        }
    }
    return pixels;
}

NSData *flat_frame(unsigned value)
{
    NSMutableData *pixels = [NSMutableData dataWithLength:256u * 240u * 2u];
    uint8_t *bytes = static_cast<uint8_t *>(pixels.mutableBytes);
    for (NSUInteger index = 0; index < 256u * 240u; ++index)
    {
        bytes[index * 2u] = static_cast<uint8_t>(value & 0xFFu);
        bytes[index * 2u + 1u] = static_cast<uint8_t>(value >> 8);
    }
    return pixels;
}

} // namespace

@interface CatalogTitleCoverTests : XCTestCase
@end

@implementation CatalogTitleCoverTests

#pragma mark - Android title presentation

- (void)testIndexedMetadataSurvivesRenameAndProjectsBothLocales
{
    NSDictionary *fields = [FlyNesCatalogPresentation fieldsForFilename:@"renamed.nes"
        entryPath:@"" trustedBuiltin:NO indexedTitleEn:@"Contra" indexedTitleZhHans:@"魂斗罗"
        aliases:@"Gryzor\n魂斗羅"];
    XCTAssertEqualObjects([FlyNesCatalogPresentation titleForFields:fields locale:@"zh-Hans"][@"primary"], @"魂斗罗");
    XCTAssertEqualObjects([FlyNesCatalogPresentation titleForFields:fields locale:@"en"][@"primary"], @"Contra");
    XCTAssertEqualObjects([FlyNesCatalogPresentation titleForFields:fields locale:@"fr"][@"primary"], @"Contra");
    XCTAssertTrue([FlyNesCatalogPresentation fields:fields matchQuery:@"renamed.nes"]);
    XCTAssertTrue([FlyNesCatalogPresentation fields:fields matchQuery:@"Gryzor"]);
    XCTAssertTrue([FlyNesCatalogPresentation fields:fields matchQuery:@"魂斗羅"]);
}

- (void)testTrustedBuiltinPresentsAndroidBilingualTitle
{
    NSDictionary *fields = [FlyNesCatalogPresentation fieldsForFilename:@"thwaite.nes"
                                                              entryPath:@""
                                                         trustedBuiltin:YES];
    XCTAssertEqualObjects(fields[@"titleEn"], @"Thwaite");
    XCTAssertEqualObjects(fields[@"titleZhHans"], @"护村记");
    XCTAssertEqualObjects([FlyNesCatalogPresentation titleForFields:fields locale:@"en-US"][@"primary"],
                          @"Thwaite");
    XCTAssertEqualObjects([FlyNesCatalogPresentation titleForFields:fields locale:@"en-US"][@"secondary"],
                          @"护村记");
    XCTAssertEqualObjects([FlyNesCatalogPresentation titleForFields:fields locale:@"zh-Hans-CN"][@"primary"],
                          @"护村记");
    XCTAssertEqualObjects([FlyNesCatalogPresentation titleForFields:fields locale:@"zh-Hans-CN"][@"secondary"],
                          @"Thwaite");
}

- (void)testExternalFilenamesNeverBecomeTranslations
{
    NSDictionary *fields = [FlyNesCatalogPresentation fieldsForFilename:@"thwaite.nes"
                                                              entryPath:@""
                                                         trustedBuiltin:NO];
    XCTAssertEqualObjects(fields[@"titleEn"], @"thwaite");
    XCTAssertEqualObjects(fields[@"titleZhHans"], @"");
    // Underscores and region tags survive; only the final extension is dropped.
    NSDictionary *tagged = [FlyNesCatalogPresentation fieldsForFilename:@"Super_Game (USA).nes"
                                                              entryPath:@""
                                                         trustedBuiltin:NO];
    XCTAssertEqualObjects(tagged[@"titleEn"], @"Super_Game (USA)");
}

- (void)testZipOuterFilenameOutranksSameLanguageEntryName
{
    NSDictionary *fields = [FlyNesCatalogPresentation fieldsForFilename:@"Collection.zip"
                                                              entryPath:@"folder/超级游戏 (USA).nes"
                                                         trustedBuiltin:NO];
    XCTAssertEqualObjects(fields[@"titleEn"], @"Collection");
    XCTAssertEqualObjects(fields[@"titleZhHans"], @"超级游戏 (USA)");
}

- (void)testAliasSearchCoversFilenameEntryAndBothLanguages
{
    NSDictionary *fields = [FlyNesCatalogPresentation fieldsForFilename:@"Collection.zip"
                                                              entryPath:@"folder/超级游戏 (USA).nes"
                                                         trustedBuiltin:NO];
    XCTAssertTrue([FlyNesCatalogPresentation fields:fields matchQuery:@" collection.zip "]);
    XCTAssertTrue([FlyNesCatalogPresentation fields:fields matchQuery:@"超级游戏"]);
    XCTAssertTrue([FlyNesCatalogPresentation fields:fields matchQuery:@"folder"]);
    XCTAssertFalse([FlyNesCatalogPresentation fields:fields matchQuery:@"SUPER"]);
}

- (void)testUnclassifiedScriptStillHasAPlaceholderTitle
{
    NSDictionary *fields = [FlyNesCatalogPresentation fieldsForFilename:@"パズル.nes"
                                                              entryPath:@""
                                                         trustedBuiltin:NO];
    XCTAssertEqual([fields[@"titleEn"] length], 0u);
    XCTAssertEqualObjects([FlyNesCatalogPresentation titleForFields:fields locale:@"en"][@"primary"],
                          @"パズル");
}

#pragma mark - Cover sampling policy

- (void)testFirstObservedFrameOnlyStartsTheSessionClock
{
    flynes::ios::CoverCaptureSession session;
    XCTAssertFalse(session.note_frame(0u));
    XCTAssertTrue(session.started());
    XCTAssertEqual(session.sampled(), 0u);
    for (uint64_t sequence = 1; sequence < 120u; ++sequence)
        XCTAssertFalse(session.note_frame(sequence), @"sequence %llu must not sample", sequence);
    XCTAssertTrue(session.note_frame(120u));
    XCTAssertFalse(session.note_frame(121u));
    XCTAssertTrue(session.note_frame(240u));
    XCTAssertTrue(session.note_frame(360u));
    XCTAssertTrue(session.note_frame(480u));
    XCTAssertFalse(session.note_frame(600u));
    XCTAssertEqual(session.sampled(), 4u);
}

- (void)testQualityGateRejectsBlackAndRequiresARealImprovement
{
    flynes::ios::CoverCaptureSession session;
    XCTAssertFalse(session.consider(flynes::ios::game_cover_score(
        static_cast<const uint8_t *>(flat_frame(0x0000).bytes), flat_frame(0x0000).length, 256u, 240u)),
        @"a black boot frame must be rejected");
    NSData *pattern = pattern_frame();
    const double good = flynes::ios::game_cover_score(
        static_cast<const uint8_t *>(pattern.bytes), pattern.length, 256u, 240u);
    XCTAssertGreaterThanOrEqual(good, 18.0);
    XCTAssertTrue(session.consider(good));
    // Android restarts the gate per play session, so an equal score is not enough.
    XCTAssertFalse(session.consider(good));
}

- (void)testExposurePenaltyRejectsWhiteAndBlackFrames
{
    NSData *white = flat_frame(0xFFFFu);
    const double white_score = flynes::ios::game_cover_score(
        static_cast<const uint8_t *>(white.bytes), white.length, 256u, 240u);
    XCTAssertLessThan(white_score, 18.0);
    NSData *black = flat_frame(0u);
    const double black_score = flynes::ios::game_cover_score(
        static_cast<const uint8_t *>(black.bytes), black.length, 256u, 240u);
    XCTAssertLessThan(black_score, 18.0);
}

#pragma mark - Cover repository

- (void)testStoredCoverIsPersistedResizedAndReloadable
{
    NSString *root = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    FlyNesCoverStore *store = [[FlyNesCoverStore alloc] init];
    [store configureWithCacheRoot:root];
    NSData *pixels = pattern_frame();
    XCTAssertTrue([store storeRgb565Frame:pixels canonicalId:@"builtin:thwaite"
                                    width:256u height:240u]);

    NSString *directory = store.directory;
    XCTAssertTrue([directory hasSuffix:@"covers/v1"]);
    NSArray<NSString *> *entries = [NSFileManager.defaultManager contentsOfDirectoryAtPath:directory error:nil];
    XCTAssertEqual(entries.count, 1u);
    // The filename must not reveal the canonical id.
    XCTAssertFalse([entries.firstObject containsString:@"thwaite"]);
    XCTAssertEqualObjects(entries.firstObject.pathExtension, @"png");

    // A fresh store instance must read the same durable file, not a memory cache.
    FlyNesCoverStore *reopened = [[FlyNesCoverStore alloc] init];
    [reopened configureWithCacheRoot:root];
    UIImage *cover = [reopened coverForCanonicalId:@"builtin:thwaite"];
    XCTAssertNotNil(cover);
    XCTAssertEqualWithAccuracy(cover.size.width, 320.0, 0.001);
    XCTAssertEqualWithAccuracy(cover.size.height, 240.0, 0.001);

    // Nearest-neighbour conversion must preserve the source pattern.
    CGImageRef image = cover.CGImage;
    XCTAssertNotNil((__bridge id)image);
    CGDataProviderRef provider = CGImageGetDataProvider(image);
    CFDataRef raw = CGDataProviderCopyData(provider);
    const uint8_t *bytes = CFDataGetBytePtr(raw);
    const size_t row_bytes = CGImageGetBytesPerRow(image);
    XCTAssertEqual(CGImageGetWidth(image), 320u);
    XCTAssertEqual(CGImageGetHeight(image), 240u);
    // Target (200, 120) maps back to source (160, 120) in the generated pattern.
    const unsigned value = (((160u / 8u) % 32u) << 11) | (((120u / 8u) % 64u) << 5) | ((160u + 120u) % 32u);
    const uint8_t expected_red = static_cast<uint8_t>(((value >> 11) & 0x1Fu) * 255u / 31u);
    const uint8_t expected_green = static_cast<uint8_t>(((value >> 5) & 0x3Fu) * 255u / 63u);
    const uint8_t expected_blue = static_cast<uint8_t>((value & 0x1Fu) * 255u / 31u);
    const uint8_t *read = bytes + 120u * row_bytes + 200u * 4u;
    XCTAssertEqualWithAccuracy(read[0], expected_red, 1);
    XCTAssertEqualWithAccuracy(read[1], expected_green, 1);
    XCTAssertEqualWithAccuracy(read[2], expected_blue, 1);
    CFRelease(raw);

    XCTAssertTrue([reopened hasCoverForCanonicalId:@"builtin:thwaite"]);
    [reopened removeCoverForCanonicalId:@"builtin:thwaite"];
    XCTAssertFalse([reopened hasCoverForCanonicalId:@"builtin:thwaite"]);
    [NSFileManager.defaultManager removeItemAtPath:root error:nil];
}

- (void)testMalformedFramesAndIdentifiersAreRefused
{
    NSString *root = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    FlyNesCoverStore *store = [[FlyNesCoverStore alloc] init];
    [store configureWithCacheRoot:root];
    XCTAssertFalse([store storeRgb565Frame:[NSData data] canonicalId:@"game" width:256u height:240u]);
    XCTAssertFalse([store storeRgb565Frame:pattern_frame() canonicalId:@"" width:256u height:240u]);
    NSData *short_frame = [NSMutableData dataWithLength:16];
    XCTAssertFalse([store storeRgb565Frame:short_frame canonicalId:@"game" width:256u height:240u]);
    XCTAssertNil([store coverForCanonicalId:@"never-stored"]);
    [NSFileManager.defaultManager removeItemAtPath:root error:nil];
}

@end
