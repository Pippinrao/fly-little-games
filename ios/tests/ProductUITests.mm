#import <XCTest/XCTest.h>
@interface ProductUITests : XCTestCase
@end
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
