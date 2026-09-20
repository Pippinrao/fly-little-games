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

- (void)testGameCenterExposesVisibleNearbyStatusAndIndependentMultiplayerFilter {
    self.continueAfterFailure = NO;
    XCUIApplication *app = [self launchApp];
    XCUIElement *entryText = app.staticTexts[@"nearby_entry_text"];
    XCTAssertTrue([entryText waitForExistenceWithTimeout:15]);
    XCTAssertEqualObjects(entryText.label, @"Nearby Multiplayer");

    XCUIElement *filter = app.switches[@"nearby_multiplayer_filter"];
    XCTAssertTrue([filter waitForExistenceWithTimeout:5]);
    XCTAssertTrue(filter.isEnabled);
    NSString *before = [filter.value description];
    [filter tap];
    XCTAssertNotEqualObjects([filter.value description], before);
    // AppStorage persists GameCenterMultiplayerOnly. Restore so later suites
    // still see single-player bundled cards (UX03: builtins are not SUPPORTED).
    [filter tap];
    XCTAssertEqualObjects([filter.value description], before);
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
    XCTAssertTrue([[self elementWithIdentifier:@"nearby_entry_headline" inApp:app]
        waitForExistenceWithTimeout:10], @"cancel returns to N00");
    XCTAssertFalse(code.exists, @"cancel must remove the old invite code from the screen");
}

- (void)testJoinSubmitFailureAllowsModifyRetryAndCancel {
    self.continueAfterFailure = NO;
    XCUIApplication *app = [self launchApp];
    XCTAssertTrue([app.buttons[@"open_nearby"] waitForExistenceWithTimeout:15]);
    [app.buttons[@"open_nearby"] tap];
    XCUIElement *enterCode = [self elementWithIdentifier:@"nearby_action_enter_code" inApp:app];
    XCTAssertTrue([enterCode waitForExistenceWithTimeout:10]);
    [enterCode tap];
    XCUIElement *input = app.textFields[@"nearby_join_code_input"];
    XCTAssertTrue([input waitForExistenceWithTimeout:10]);
    [input tap];
    [input typeText:@"123456"];
    XCUIElement *submit = app.buttons[@"nearby_join_submit"];
    XCTAssertTrue(submit.isEnabled);
    [submit tap];
    [submit tap];
    XCUIElement *cancel = app.buttons[@"nearby_join_cancel"];
    XCTAssertTrue([cancel waitForExistenceWithTimeout:5]);
    XCTAssertTrue(submit.isEnabled, @"failure must restore submit on the same page");
    XCTAssertTrue(input.isEnabled, @"failure must unlock the code field for modify/retry");
    [input tap];
    NSString *current = [input.value description];
    for (NSUInteger i = 0; i < current.length; i++) {
        [input typeText:XCUIKeyboardKeyDelete];
    }
    [input typeText:@"654321"];
    [submit tap];
    XCTAssertTrue([cancel waitForExistenceWithTimeout:5]);
    [cancel tap];
    XCTAssertTrue([[self elementWithIdentifier:@"nearby_entry_headline" inApp:app]
        waitForExistenceWithTimeout:10]);
}

- (void)testLobbyFirstScreenHidesDiagnosticsUntilOpened {
    self.continueAfterFailure = NO;
    XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.flynes.app"];
    app.launchArguments = @[@"-AppleLanguages", @"(en)", @"-AppleLocale", @"en_US",
                            @"-flynes.test.nearby_lobby"];
    [app launch];
    XCUIElement *game = [self elementWithIdentifier:@"nearby_lobby_row_rom_identity" inApp:app];
    XCTAssertTrue([game waitForExistenceWithTimeout:10]);
    XCTAssertTrue([[self elementWithIdentifier:@"nearby_lobby_row_network_owner" inApp:app] exists]);
    XCTAssertTrue([[self elementWithIdentifier:@"nearby_lobby_row_seat" inApp:app] exists]);
    XCTAssertFalse([[self elementWithIdentifier:@"nearby_lobby_row_identity_fingerprint" inApp:app] exists]);
    XCUIElement *confirm = app.buttons[@"nearby_lobby_confirm"];
    XCTAssertTrue([confirm waitForExistenceWithTimeout:5]);
    XCTAssertFalse(confirm.isEnabled, @"confirm stays disabled until pending-config exists");
    XCUIElement *details = [self elementWithIdentifier:@"nearby_lobby_details" inApp:app];
    XCTAssertTrue([details waitForExistenceWithTimeout:5]);
    XCUIElement *scroll = app.scrollViews.firstMatch;
    XCUICoordinate *revealStart = [scroll coordinateWithNormalizedOffset:CGVectorMake(0.2, 0.32)];
    XCUICoordinate *revealEnd = [scroll coordinateWithNormalizedOffset:CGVectorMake(0.2, 0.10)];
    [revealStart pressForDuration:0.3 thenDragToCoordinate:revealEnd];
    XCTAssertTrue(details.isHittable, @"details control must be reachable above the fixed confirm section");
    [details tap];
    XCUIElement *fingerprint = [self elementWithIdentifier:@"nearby_lobby_row_identity_fingerprint" inApp:app];
    for (NSUInteger attempt = 0; attempt < 12 && !fingerprint.exists; attempt++) {
        XCUICoordinate *start = [scroll coordinateWithNormalizedOffset:CGVectorMake(0.2, 0.31)];
        XCUICoordinate *end = [scroll coordinateWithNormalizedOffset:CGVectorMake(0.2, 0.18)];
        [start pressForDuration:0.5 thenDragToCoordinate:end];
        fingerprint = [self elementWithIdentifier:@"nearby_lobby_row_identity_fingerprint" inApp:app];
    }
    XCTAssertTrue(fingerprint.exists, @"expanded diagnostics must reveal the identity row after scrolling");
    XCTAssertFalse(confirm.isEnabled);
}

@end
