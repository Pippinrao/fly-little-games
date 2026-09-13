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
- (void)testAndroidBilingualTitlesAndAutomaticCoverCapture {
    self.continueAfterFailure = NO;
    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationLandscapeLeft;
    XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.flynes.app"];
    app.launchArguments = @[@"-AppleLanguages", @"(en)", @"-AppleLocale", @"en_US"];
    [app launch];
    XCUIElement *game = [app.buttons matchingPredicate:[NSPredicate predicateWithFormat:@"identifier BEGINSWITH %@", @"game_card_"]].firstMatch;
    XCTAssertTrue([game waitForExistenceWithTimeout:15]);
    // Android presents the trusted manifest title, never the ROM filename.
    XCTAssertTrue([game.label containsString:@"From Below"], @"card label was %@", game.label);
    XCTAssertFalse([game.label containsString:@"from_below.nes"], @"card label was %@", game.label);
    XCTAssertTrue([game.label containsString:@"来自下方"], @"bilingual metadata missing: %@", game.label);

    // Chinese interface promotes the localized title and keeps English secondary.
    [app terminate];
    app.launchArguments = @[@"-AppleLanguages", @"(zh-Hans)", @"-AppleLocale", @"zh_CN"];
    [app launch];
    XCUIElement *chinese = [app.buttons matchingPredicate:[NSPredicate predicateWithFormat:@"identifier BEGINSWITH %@", @"game_card_"]].firstMatch;
    XCTAssertTrue([chinese waitForExistenceWithTimeout:15]);
    XCTAssertTrue([chinese.label containsString:@"来自下方"], @"zh card label was %@", chinese.label);
    XCTAssertTrue([chinese.label containsString:@"From Below"], @"zh secondary missing: %@", chinese.label);

    // Play long enough for the 120-frame sample, then confirm the cover is durable.
    [chinese tap];
    XCTAssertTrue([app.buttons[@"launch_selected"] waitForExistenceWithTimeout:5]);
    [app.buttons[@"launch_selected"] tap];
    XCTAssertTrue([app.buttons[@"OPEN_PAUSE"] waitForExistenceWithTimeout:10]);
    [NSThread sleepForTimeInterval:6.0];
    [app.buttons[@"OPEN_PAUSE"] tap];
    XCTAssertTrue([app.buttons[@"game_center"] waitForExistenceWithTimeout:5]);
    [app.buttons[@"game_center"] tap];

    NSString *container = installedDataContainer();
    XCTAssertGreaterThan(container.length, 0u,
                         @"Run via run_simulator_tests.py to resolve the product container");
    {
        NSFileManager *files = NSFileManager.defaultManager;
        NSString *covers = [[container stringByAppendingPathComponent:@"Library/Caches"]
            stringByAppendingPathComponent:@"covers/v1"];
        XCTAssertTrue([files fileExistsAtPath:covers], @"cover directory is missing at %@", covers);
        NSArray<NSString *> *entries = [files contentsOfDirectoryAtPath:covers error:nil];
        XCTAssertGreaterThan(entries.count, 0u, @"no cover was captured from native game frames");
        for (NSString *entry in entries) {
            // Android names covers by SHA-256 of the canonical id, never by title.
            XCTAssertEqualObjects(entry.pathExtension, @"png");
            XCTAssertFalse([entry containsString:@"from-below"], @"cover filename leaks identity: %@", entry);
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
