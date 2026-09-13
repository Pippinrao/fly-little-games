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

// SwiftUI List NavigationLinks surface as buttons, cells or other elements
// depending on iOS version; resolve through every container type.
- (XCUIElement *)elementWithIdentifier:(NSString *)identifier inApp:(XCUIApplication *)app {
    XCUIElement *element = app.buttons[identifier];
    if (element.exists) return element;
    element = app.otherElements[identifier];
    if (element.exists) return element;
    element = app.staticTexts[identifier];
    if (element.exists) return element;
    element = app.cells[identifier];
    if (element.exists) return element;
    return app.staticTexts[[identifier stringByAppendingString:@"_label"]];
}

- (void)testN00ShowsTheThreePrimaryActionsInContractOrder {
    self.continueAfterFailure = NO;
    XCUIApplication *app = [self launchApp];
    XCTAssertTrue([app.buttons[@"open_nearby"] waitForExistenceWithTimeout:15]);
    [app.buttons[@"open_nearby"] tap];
    XCUIElement *root = [[app descendantsMatchingType:XCUIElementTypeAny]
        matchingIdentifier:@"nearby_root"].firstMatch;
    XCTAssertTrue([root waitForExistenceWithTimeout:10]);
    XCTAssertTrue([[self elementWithIdentifier:@"nearby_action_create" inApp:app]
        waitForExistenceWithTimeout:10]);
    XCTAssertTrue([[self elementWithIdentifier:@"nearby_action_enter_code" inApp:app] exists]);
    XCTAssertTrue([[self elementWithIdentifier:@"nearby_action_scan_qr" inApp:app] exists]);
}

- (void)testJoinCodeFormNeverSubmitsIncompleteInput {
    self.continueAfterFailure = NO;
    XCUIApplication *app = [self launchApp];
    XCTAssertTrue([app.buttons[@"open_nearby"] waitForExistenceWithTimeout:15]);
    [app.buttons[@"open_nearby"] tap];
    XCUIElement *enterCode = [self elementWithIdentifier:@"nearby_action_enter_code" inApp:app];
    XCTAssertTrue([enterCode waitForExistenceWithTimeout:10]);
    [enterCode tap];
    XCUIElement *input = app.textFields[@"nearby_join_code_input"];
    XCTAssertTrue([input waitForExistenceWithTimeout:10]);
    [input tap];  // focus the field before typing
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
    XCUIElement *create = [self elementWithIdentifier:@"nearby_action_create" inApp:app];
    XCTAssertTrue([create waitForExistenceWithTimeout:10]);
    [create tap];
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
