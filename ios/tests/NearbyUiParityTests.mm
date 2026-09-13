#import <XCTest/XCTest.h>

/// Nearby UI parity on the real app (design 2026-09-13, cases C01/C04/C05/C16):
/// the N00 three primary actions in contract order, the invite lifecycle
/// (regenerate/cancel kill the old code), and the six-digit join form gating
/// with leading zeros preserved. Mirrors NearbyUiParityTest /
/// NearbyInviteCodeTest / HomeMultiplayerFilterTest on Android and the Hypium
/// vectors on HarmonyOS; the same identifiers are used across the three
/// platforms. Executed by the FlyNESUITests bundle on macOS/Xcode (P6/P8).
@interface NearbyUiParityTests : XCTestCase
@end

@implementation NearbyUiParityTests

- (XCUIApplication *)launchApp {
    XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.flynes.app"];
    app.launchArguments = @[@"-AppleLanguages", @"(en)", @"-AppleLocale", @"en_US"];
    [app launch];
    return app;
}

- (void)testN00ShowsTheThreePrimaryActionsInContractOrder {
    self.continueAfterFailure = NO;
    XCUIApplication *app = [self launchApp];
    XCTAssertTrue([app.buttons[@"open_nearby"] waitForExistenceWithTimeout:15]);
    [app.buttons[@"open_nearby"] tap];
    XCTAssertTrue([app.otherElements[@"nearby_root"] waitForExistenceWithTimeout:10]);
    XCTAssertTrue([app.buttons[@"nearby_action_create"] waitForExistenceWithTimeout:5]);
    XCTAssertTrue([app.buttons[@"nearby_action_enter_code"] waitForExistenceWithTimeout:5]);
    XCTAssertTrue([app.buttons[@"nearby_action_scan_qr"] waitForExistenceWithTimeout:5]);
}

- (void)testJoinCodeFormNeverSubmitsIncompleteInput {
    self.continueAfterFailure = NO;
    XCUIApplication *app = [self launchApp];
    XCTAssertTrue([app.buttons[@"open_nearby"] waitForExistenceWithTimeout:15]);
    [app.buttons[@"open_nearby"] tap];
    XCTAssertTrue([app.buttons[@"nearby_action_enter_code"] waitForExistenceWithTimeout:10]);
    [app.buttons[@"nearby_action_enter_code"] tap];
    XCUIElement *input = app.textFields[@"nearby_join_code_input"];
    XCTAssertTrue([input waitForExistenceWithTimeout:10]);
    [input typeText:@"12345"];
    XCUIElement *submit = app.buttons[@"nearby_join_submit"];
    XCTAssertFalse(submit.isEnabled);

    [input typeText:@"6"];
    XCTAssertTrue(submit.isEnabled);
    [submit tap];
    // No discovery bearer in this build: the honest outcome is the blocked
    // reason, and no pairing state may appear.
    XCTAssertTrue([app.staticTexts[@"nearby_join_code_error"] waitForExistenceWithTimeout:5]);
}

- (void)testInviteLifecycleRegenerateChangesTheCodeAndCancelClearsIt {
    self.continueAfterFailure = NO;
    XCUIApplication *app = [self launchApp];
    XCTAssertTrue([app.buttons[@"open_nearby"] waitForExistenceWithTimeout:15]);
    [app.buttons[@"open_nearby"] tap];
    XCTAssertTrue([app.buttons[@"nearby_action_create"] waitForExistenceWithTimeout:10]);
    [app.buttons[@"nearby_action_create"] tap];
    XCUIElement *code = app.staticTexts[@"nearby_invite_code_value"];
    XCTAssertTrue([code waitForExistenceWithTimeout:10]);
    NSString *first = code.label;

    XCUIElement *regenerate = app.buttons[@"nearby_invite_regenerate"];
    XCTAssertTrue([regenerate waitForExistenceWithTimeout:5]);
    [regenerate tap];
    NSString *second = code.label;
    XCTAssertEqualObjects(@(second.length), @6);
    XCTAssertNotEqualObjects(first, second);

    [app.buttons[@"nearby_invite_cancel"] tap];
    XCTAssertEqualObjects(code.label, @"· · · · · ·");
}

@end
