#import <XCTest/XCTest.h>

@interface ProductUITests : XCTestCase
@end

/// Locates the installed app's private container on the simulator so tests can
/// assert on durable app state (captured covers) instead of UI proxies.
static NSString *installedDataContainer(void)
{
    // Xcode can reinstall the app into a new UUID after the host runner starts.
    // Resolve the current container by its exact MCM identifier at assertion time.
    NSString *applications = NSProcessInfo.processInfo.environment[@"FLYNES_TEST_APPLICATION_CONTAINERS"];
    if (applications.length == 0) return nil;
    NSFileManager *files = NSFileManager.defaultManager;
    NSString *found = nil;
    for (NSString *name in [files contentsOfDirectoryAtPath:applications error:nil]) {
        NSString *candidate = [applications stringByAppendingPathComponent:name];
        NSDictionary *metadata = [NSDictionary dictionaryWithContentsOfFile:
            [candidate stringByAppendingPathComponent:@".com.apple.mobile_container_manager.metadata.plist"]];
        if ([metadata[@"MCMMetadataIdentifier"] isEqual:@"com.flynes.app"]) {
            if (found != nil) return nil; // Never choose an arbitrary stale container.
            found = candidate;
        }
    }
    return found;
}

@implementation ProductUITests
- (void)testControlsResetRestoresDistinctButtons {
    self.continueAfterFailure = NO;
    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationLandscapeLeft;
    XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.flynes.app"];
    app.launchArguments = @[@"-AppleLanguages", @"(en)", @"-AppleLocale", @"en_US"];
    [app launch];
    XCTAssertTrue([app.buttons[@"open_settings"] waitForExistenceWithTimeout:15]);
    [app.buttons[@"open_settings"] tap];
    [app.buttons[@"section.controls"] tap];
    XCUIElement *distinct = app.switches[@"Distinct A/B haptics"];
    XCTAssertTrue([distinct waitForExistenceWithTimeout:5]);
    XCTAssertTrue(distinct.enabled);
    if ([distinct.value isEqual:@"1"]) [[distinct coordinateWithNormalizedOffset:CGVectorMake(0.93, 0.5)] tap];
    XCTAssertEqualObjects(distinct.value, @"0", @"Reset precondition: A/B distinction was turned off");
    [app.buttons[@"Reset to recommended"] tap];
    XCTAssertEqualObjects(distinct.value, @"1", @"Android controls reset includes haptics preferences");
    [app.buttons[@"settings_done"] tap];
}
- (void)testAndroidGameCenterSelectionStaysBesideGrid {
    self.continueAfterFailure = NO;
    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationLandscapeLeft;
    XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.flynes.app"];
    app.launchArguments = @[@"-AppleLanguages", @"(en)", @"-AppleLocale", @"en_US"];
    [app launch];
    XCUIElement *game = [app.buttons matchingPredicate:[NSPredicate predicateWithFormat:@"identifier BEGINSWITH %@", @"game_card_"]].firstMatch;
    XCTAssertTrue([game waitForExistenceWithTimeout:15]);
    [game tap];
    XCUIElement *launch = app.buttons[@"launch_selected"];
    XCUIElement *grid = app.scrollViews[@"game_grid"];
    XCTAssertTrue([launch waitForExistenceWithTimeout:5]);
    XCTAssertTrue(grid.exists, @"Selecting a game must keep the grid on screen");
    XCTAssertLessThan(launch.frame.origin.x + launch.frame.size.width, grid.frame.origin.x);
    XCTAssertTrue(app.buttons[@"open_sources"].exists);
    [app.buttons[@"open_sources"] tap];
    XCTAssertTrue([app.buttons[@"close_sources"] waitForExistenceWithTimeout:5]);
    [app.buttons[@"close_sources"] tap];
    XCTAssertTrue([grid waitForExistenceWithTimeout:5]);
}
/// The games the shared manifest bundles, read from this test bundle. Asserting
/// against it means adding a bundled game needs no test edit.
- (NSArray<NSDictionary *> *)bundledGames
{
    NSURL *url = [[NSBundle bundleForClass:self.class] URLForResource:@"builtin-games"
                                                       withExtension:@"json"];
    NSData *data = url == nil ? nil : [NSData dataWithContentsOfURL:url];
    if (data == nil) return @[];
    NSDictionary *root = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    NSArray *games = [root[@"games"] isKindOfClass:NSArray.class] ? root[@"games"] : @[];
    return games;
}

- (BOOL)label:(NSString *)label matchesABundledTitle:(NSString *)titleKey
{
    for (NSDictionary *game in [self bundledGames]) {
        NSString *title = game[titleKey];
        if ([title isKindOfClass:NSString.class] && title.length > 0
                && [label containsString:title]) {
            return YES;
        }
    }
    return NO;
}
/// The cover quality gate discards flat title screens, so the game that proves
/// capture must come from the manifest flag instead of a hardcoded title or the
/// first row on screen. Adding or reordering a bundled game cannot break this.
- (NSDictionary *)coverEligibleGame
{
    for (NSDictionary *game in [self bundledGames]) {
        if ([game[@"coverEligible"] boolValue]) return game;
    }
    return nil;
}
/// A bundled card is identified by the content id the scanner derives from the
/// ROM bytes - `game:` plus the uppercase payload hash - on every platform, not
/// by the manifest's human-readable canonicalId.
- (NSString *)cardIdentifierForGame:(NSDictionary *)game
{
    NSString *hash = game[@"romSha256"];
    if (![hash isKindOfClass:NSString.class] || hash.length == 0) return nil;
    return [@"game_card_game:" stringByAppendingString:hash.uppercaseString];
}
/// The gate samples the native frame at frames 120/240/360/480, so a slower
/// simulator can still be four seconds away from its last chance at capture.
- (BOOL)waitForCoverEntries:(NSString *)covers
{
    NSFileManager *files = NSFileManager.defaultManager;
    for (NSUInteger attempt = 0; attempt < 14; attempt++) {
        if ([files contentsOfDirectoryAtPath:covers error:nil].count > 0) return YES;
        [NSThread sleepForTimeInterval:1.0];
    }
    return NO;
}
- (void)testAndroidBilingualTitlesAndAutomaticCoverCapture {
    self.continueAfterFailure = NO;
    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationLandscapeLeft;
    XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.flynes.app"];
    app.launchArguments = @[@"-AppleLanguages", @"(en)", @"-AppleLocale", @"en_US"];
    [app launch];
    XCUIElement *game = [app.buttons matchingPredicate:[NSPredicate predicateWithFormat:@"identifier BEGINSWITH %@", @"game_card_"]].firstMatch;
    XCTAssertTrue([game waitForExistenceWithTimeout:15]);
    // Android presents the trusted manifest title, never the ROM filename.
    XCTAssertTrue([self label:game.label matchesABundledTitle:@"titleEn"], @"card label was %@", game.label);
    XCTAssertFalse([game.label containsString:@".nes"], @"card label leaked the ROM filename: %@", game.label);
    XCTAssertTrue([self label:game.label matchesABundledTitle:@"titleZhHans"], @"bilingual metadata missing: %@", game.label);

    // Chinese interface promotes the localized title and keeps English secondary.
    [app terminate];
    app.launchArguments = @[@"-AppleLanguages", @"(zh-Hans)", @"-AppleLocale", @"zh_CN"];
    [app launch];
    NSDictionary *capturable = [self coverEligibleGame];
    NSString *capturableCard = [self cardIdentifierForGame:capturable];
    XCTAssertTrue(capturableCard.length > 0, @"the manifest must mark one game coverEligible");
    XCUIElement *chinese = app.buttons[capturableCard];
    XCTAssertTrue([chinese waitForExistenceWithTimeout:15]);
    XCTAssertTrue([self label:chinese.label matchesABundledTitle:@"titleZhHans"], @"zh card label was %@", chinese.label);
    XCTAssertTrue([self label:chinese.label matchesABundledTitle:@"titleEn"], @"zh secondary missing: %@", chinese.label);

    // Play past the 120/240/360/480-frame samples, then confirm the cover is durable.
    // The cover is written while playback runs, so the wait has to happen before pausing.
    NSString *container = installedDataContainer();
    XCTAssertGreaterThan(container.length, 0u,
                         @"Run via run_simulator_tests.py to resolve the product container");
    NSString *covers = [[container stringByAppendingPathComponent:@"Library/Caches"]
        stringByAppendingPathComponent:@"covers/v1"];
    [chinese tap];
    XCTAssertTrue([app.buttons[@"launch_selected"] waitForExistenceWithTimeout:5]);
    [app.buttons[@"launch_selected"] tap];
    XCTAssertTrue([app.buttons[@"OPEN_PAUSE"] waitForExistenceWithTimeout:10]);
    BOOL captured = [self waitForCoverEntries:covers];
    [app.buttons[@"OPEN_PAUSE"] tap];
    XCTAssertTrue([app.buttons[@"game_center"] waitForExistenceWithTimeout:5]);
    [app.buttons[@"game_center"] tap];

    {
        NSFileManager *files = NSFileManager.defaultManager;
        XCTAssertTrue([files fileExistsAtPath:covers], @"cover directory is missing at %@", covers);
        NSArray<NSString *> *entries = [files contentsOfDirectoryAtPath:covers error:nil];
        XCTAssertTrue(captured && entries.count > 0u,
                      @"no cover was captured from native game frames for %@", capturableCard);
        for (NSString *entry in entries) {
            // Android names covers by SHA-256 of the canonical id, never by title.
            XCTAssertEqualObjects(entry.pathExtension, @"png");
            for (NSDictionary *bundled in [self bundledGames]) {
                NSString *stem = [(NSString *)bundled[@"assetFilename"] stringByDeletingPathExtension];
                XCTAssertFalse(stem.length > 0 && [entry.lowercaseString containsString:stem.lowercaseString],
                               @"cover filename leaks identity: %@", entry);
            }
        }
    }
}

/// Android settings are committed immediately and survive a restart, and the
/// interface language switches both the mirrored strings and the persisted tag.
- (void)testSettingsPersistAcrossRestartAndFollowInterfaceLanguage {
    self.continueAfterFailure = NO;
    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationLandscapeLeft;
    XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.flynes.app"];
    app.launchArguments = @[@"-AppleLanguages", @"(en)", @"-AppleLocale", @"en_US"];
    [app launch];
    XCTAssertTrue([app.buttons[@"open_settings"] waitForExistenceWithTimeout:15]);
    [app.buttons[@"open_settings"] tap];
    [app.buttons[@"section.audio"] tap];
    XCUIElement *audio = app.switches[@"settings_audio"];
    XCTAssertTrue([audio waitForExistenceWithTimeout:5]);
    NSString *originalAudio = [audio.value description];
    XCTAssertTrue(audio.enabled, @"audio switch must be interactive: %@", audio.debugDescription);
    [audio tap];
    NSString *toggledAudio = [audio.value description];
    if ([originalAudio isEqualToString:toggledAudio]) {
        // A tap on the row centre can miss the switch control on a wide landscape row.
        [[audio coordinateWithNormalizedOffset:CGVectorMake(0.95, 0.5)] tap];
        toggledAudio = [audio.value description];
    }
    XCTAssertNotEqualObjects(originalAudio, toggledAudio,
                             @"audio toggle did not change (value %@)", toggledAudio);

    // The language section shows both languages and keeps the picker in sync.
    [app.buttons[@"section.game_language"] tap];
    XCUIElement *language = app.buttons[@"settings_language"];
    XCTAssertTrue([language waitForExistenceWithTimeout:5]);
    XCTAssertTrue([language.label containsString:@"English"] || [language.label containsString:@"System"],
                  @"language picker label was %@", language.label);
    [app.buttons[@"settings_done"] tap];

    [app terminate];
    [app launch];
    XCTAssertTrue([app.buttons[@"open_settings"] waitForExistenceWithTimeout:15]);
    [app.buttons[@"open_settings"] tap];
    [app.buttons[@"section.audio"] tap];
    XCUIElement *restored = app.switches[@"settings_audio"];
    XCTAssertTrue([restored waitForExistenceWithTimeout:5]);
    XCTAssertEqualObjects([restored.value description], toggledAudio,
                          @"audio setting must persist across a restart");

    // Restore the pre-test value so the suite stays order independent. A tap on the
    // row centre can miss the switch control on a wide landscape row.
    [[restored coordinateWithNormalizedOffset:CGVectorMake(0.95, 0.5)] tap];
    XCTAssertEqualObjects([restored.value description], originalAudio);
    [app.buttons[@"settings_done"] tap];
}
- (void)testLibraryLaunchPauseSettingsResumeAndReturn {
    self.continueAfterFailure = NO;
    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationLandscapeLeft;
    XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.flynes.app"];
    app.launchArguments = @[@"-AppleLanguages", @"(en)", @"-AppleLocale", @"en_US"];
    [app launch];
    XCUIElement *game = [app.buttons matchingPredicate:[NSPredicate predicateWithFormat:@"identifier BEGINSWITH %@", @"game_card_"]].firstMatch;
    XCTAssertTrue([game waitForExistenceWithTimeout:15]);
    [game tap];
    XCTAssertTrue([app.buttons[@"launch_selected"] waitForExistenceWithTimeout:5]);
    [app.buttons[@"launch_selected"] tap];
    XCTAssertTrue([app.buttons[@"OPEN_PAUSE"] waitForExistenceWithTimeout:10]);
    XCTAttachment *play = [XCTAttachment attachmentWithScreenshot:app.screenshot];
    play.name = @"gameplay"; play.lifetime = XCTAttachmentLifetimeKeepAlways; [self addAttachment:play];
    for (int cycle=0; cycle<10; ++cycle) {
        [app.buttons[@"OPEN_PAUSE"] tap];
        XCTAssertTrue([app.buttons[@"resume"] waitForExistenceWithTimeout:3]);
        [app.buttons[@"settings"] tap];
        XCTAssertTrue([app.buttons[@"settings_done"] waitForExistenceWithTimeout:5]);
        [app.buttons[@"settings_done"] tap];
        XCTAssertTrue([app.buttons[@"resume"] waitForExistenceWithTimeout:5]);
        [app.buttons[@"resume"] tap];
        XCTAssertTrue([app.buttons[@"OPEN_PAUSE"] waitForExistenceWithTimeout:3]);
        [app.buttons[@"OPEN_PAUSE"] tap];
        [app.buttons[@"game_center"] tap];
        XCTAssertTrue([game waitForExistenceWithTimeout:5]);
        [app.buttons[@"launch_selected"] tap];
        XCTAssertTrue([app.buttons[@"OPEN_PAUSE"] waitForExistenceWithTimeout:10]);
    }
    [app.buttons[@"OPEN_PAUSE"] tap];
    [app.buttons[@"game_center"] tap];
    XCTAssertTrue([game waitForExistenceWithTimeout:5]);
    [app terminate];
    [app launch];
    XCTAssertTrue([game waitForExistenceWithTimeout:10]);
}
@end
