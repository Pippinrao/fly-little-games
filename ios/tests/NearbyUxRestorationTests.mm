#import <XCTest/XCTest.h>

/// TYPO-03.I: N00/N01 share Dynamic Type roles, wide 640×360 scroll, one header.
/// Simulator-only (FlyNESUITests). Do not treat host/static checks as screenshots.
@interface NearbyUxRestorationTests : XCTestCase
@end

@implementation NearbyUxRestorationTests

- (XCUIApplication *)launchApp {
    XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.flynes.app"];
    app.launchArguments = @[@"-AppleLanguages", @"(en)", @"-AppleLocale", @"en_US"];
    [app launch];
    return app;
}

- (XCUIElement *)elementWithIdentifier:(NSString *)identifier inApp:(XCUIApplication *)app {
    XCUIElement *element = app.buttons[identifier];
    if (element.exists) return element;
    element = app.otherElements[identifier];
    if (element.exists) return element;
    element = app.staticTexts[identifier];
    if (element.exists) return element;
    element = app.cells[identifier];
    if (element.exists) return element;
    return [[app descendantsMatchingType:XCUIElementTypeAny] matchingIdentifier:identifier].firstMatch;
}

- (void)openNearby:(XCUIApplication *)app {
    XCTAssertTrue([app.buttons[@"open_nearby"] waitForExistenceWithTimeout:15]);
    [app.buttons[@"open_nearby"] tap];
    XCUIElement *root = [[app descendantsMatchingType:XCUIElementTypeAny]
        matchingIdentifier:@"nearby_root"].firstMatch;
    XCTAssertTrue([root waitForExistenceWithTimeout:10]);
}

- (void)testTYPO_I_roles_and_scaling {
    // Run at two simctl ui content_size settings and compare TYPO_METRIC.
    // iOS 16 ignores -UIPreferredContentSizeCategoryName on app launch.
    self.continueAfterFailure = NO;
    XCUIApplication *app = [self launchApp];
    [self openNearby:app];
    XCUIElement *n00Title = [self elementWithIdentifier:@"nearby_entry_headline" inApp:app];
    XCUIElement *n00Muted = [self elementWithIdentifier:@"nearby_entry_subtitle" inApp:app];
    XCTAssertTrue([n00Title waitForExistenceWithTimeout:10]);
    XCTAssertTrue(n00Muted.exists);
    CGFloat n00TitleH = n00Title.frame.size.height;
    CGFloat n00MutedH = n00Muted.frame.size.height;
    XCTAssertGreaterThan(n00TitleH, n00MutedH, @"paneTitle 21/600 must read larger than muted 12");

    [[self elementWithIdentifier:@"nearby_action_create" inApp:app] tap];
    XCUIElement *n01Title = [self elementWithIdentifier:@"nearby_invite_headline" inApp:app];
    XCTAssertTrue([n01Title waitForExistenceWithTimeout:10]);
    CGFloat n01TitleH = n01Title.frame.size.height;
    XCTAssertGreaterThan(n01TitleH, n00MutedH,
        @"N01 paneTitle must read larger than N00 muted text; N00 title may wrap onto two lines");
    NSLog(@"TYPO_METRIC N00=%f N01=%f MUTED=%f", n00TitleH, n01TitleH, n00MutedH);
}

- (void)testTYPO_I_wide_scroll {
    self.continueAfterFailure = NO;
    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationLandscapeLeft;
    XCUIApplication *app = [self launchApp];
    [self openNearby:app];
    XCUIElement *scan = [self elementWithIdentifier:@"nearby_action_scan_qr" inApp:app];
    XCTAssertTrue([scan waitForExistenceWithTimeout:10]);
    XCUIElement *root = [[app descendantsMatchingType:XCUIElementTypeAny]
        matchingIdentifier:@"nearby_root"].firstMatch;
    XCUIElement *footer = [self elementWithIdentifier:@"nearby_entry_footer" inApp:app];
    for (NSUInteger attempt = 0; attempt < 5 &&
         (!scan.isHittable || (footer.exists &&
          CGRectIntersectsRect(CGRectInset(footer.frame, 2, 2), scan.frame))); attempt++) {
        XCUICoordinate *start = [root coordinateWithNormalizedOffset:CGVectorMake(0.5, 0.7)];
        XCUICoordinate *end = [root coordinateWithNormalizedOffset:CGVectorMake(0.5, 0.25)];
        [start pressForDuration:0.05 thenDragToCoordinate:end];
    }
    XCTAssertTrue(scan.isHittable,
        @"last action must be reachable at wide landscape (~640x360) large type English");
    if (footer.exists) {
        XCTAssertFalse(CGRectIntersectsRect(CGRectInset(footer.frame, 2, 2), scan.frame),
            @"footer must not cover the body / last action");
    }
}

- (void)testTYPO_I_single_header {
    self.continueAfterFailure = NO;
    XCUIApplication *app = [self launchApp];
    [self openNearby:app];
    XCTAssertEqual(app.navigationBars.count, 1, @"only one system navigation header");
    XCUIElement *bar = app.navigationBars.firstMatch;
    XCTAssertTrue([bar waitForExistenceWithTimeout:10]);
    XCTAssertTrue(bar.buttons.firstMatch.exists, @"keep system back semantics");
    NSUInteger titleHits = 0;
    for (XCUIElement *label in app.staticTexts.allElementsBoundByIndex) {
        if (!label.exists) continue;
        if ([label.label isEqualToString:@"Nearby Multiplayer"] &&
            CGRectIntersectsRect(label.frame, app.windows.firstMatch.frame)) {
            titleHits += 1;
        }
    }
    XCTAssertLessThanOrEqual(titleHits, 1,
        @"must not stack a custom HStack title on top of navigationTitle");
}

@end
