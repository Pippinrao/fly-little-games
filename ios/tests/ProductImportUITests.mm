// Real Files-picker coverage. Stage ONLY the repository's licensed fixture using
// ios/scripts/stage_import_fixtures.py on a dedicated iOS 16.4 simulator first.
// This suite never seeds app state, deletes app data, or imports private ROMs.
#import <XCTest/XCTest.h>

static NSString *const FixtureRoot = @"FlyNES-Import-E2E-v1";
static NSString *const SingleSource = @"FlyNES-E2E-Single.nes";
static NSString *const HundredSource = @"FlyNES-E2E-Hundred";

@interface ProductImportUITests : XCTestCase
@property(nonatomic, strong) XCUIApplication *app;
@property(nonatomic, strong) NSMutableArray<NSString *> *ownedSources;
@end

@implementation ProductImportUITests

- (void)waitFor:(BOOL (^)(void))condition reason:(NSString *)reason timeout:(NSTimeInterval)timeout
{
    NSPredicate *predicate = [NSPredicate predicateWithBlock:^BOOL(id object, NSDictionary *bindings) {
        return condition();
    }];
    XCTNSPredicateExpectation *expectation = [[XCTNSPredicateExpectation alloc] initWithPredicate:predicate object:nil];
    XCTAssertEqual([XCTWaiter waitForExpectations:@[expectation] timeout:timeout], XCTWaiterResultCompleted,
                   @"%@: %@", reason, self.app.debugDescription);
}

- (XCUIElementQuery *)cards
{
    return [self.app.buttons matchingPredicate:[NSPredicate predicateWithFormat:
        @"identifier BEGINSWITH %@", @"game_card_"]];
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

- (NSUInteger)bundledCount
{
    return [self bundledGames].count;
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

- (void)closeSearch
{
    if (self.app.buttons[@"close_search"].exists) [self.app.buttons[@"close_search"] tap];
}

- (void)openSources
{
    if (!self.app.buttons[@"close_sources"].exists) [self.app.buttons[@"open_sources"] tap];
    XCTAssertTrue([self.app.buttons[@"add_source_files"] waitForExistenceWithTimeout:15]);
    [self waitFor:^BOOL { return self.app.buttons[@"add_source_files"].enabled; }
            reason:@"source operation completed" timeout:30];
}

- (void)setUp
{
    [super setUp];
    self.continueAfterFailure = NO;
    self.ownedSources = [NSMutableArray array];
    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationLandscapeLeft;
    self.app = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.flynes.app"];
    self.app.launchArguments = @[@"-AppleLanguages", @"(en)", @"-AppleLocale", @"en_US"];
    [self.app launch];
    XCTAssertTrue([self.app.buttons[@"open_sources"] waitForExistenceWithTimeout:15]);
    XCUIElement *filter = self.app.switches[@"nearby_multiplayer_filter"];
    if ([filter waitForExistenceWithTimeout:5] &&
        ![[filter.value description] isEqual:@"0"]) {
        [filter tap];
    }
    [self closeSearch];
    [self.app.buttons[@"category_all"] tap];
    [self openSources];
    // A dedicated simulator is an explicit precondition. Never remove someone
    // else's sources to make the expected game counts pass.
    XCTAssertEqual([self.app.scrollViews[@"source_list"].buttons matchingIdentifier:@"Remove"].count, 0u,
                   @"Use a dedicated simulator with no imported sources; existing sources are preserved.");
    [self.app.buttons[@"close_sources"] tap];
    XCTAssertTrue([self.cards.firstMatch waitForExistenceWithTimeout:15]);
    XCTAssertEqual(self.cards.count, self.bundledCount, @"every bundled game must already exist");
    XCTAssertTrue([self label:self.cards.firstMatch.label matchesABundledTitle:@"titleEn"], @"the first builtin card must carry a manifest title, was %@", self.cards.firstMatch.label);
}

// Source rows currently lack stable identifiers. Resolve a row by its exact
// fixture name, then require one action in that row. On SwiftUI versions that
// flatten VStack accessibility, the dedicated-simulator precondition and exactly
// one named imported source still permit an unambiguous action; never use an index.
- (XCUIElement *)sourceAction:(NSString *)action name:(NSString *)name
{
    XCUIElement *list = self.app.scrollViews[@"source_list"];
    XCUIElementQuery *ancestors = [list.otherElements containingType:XCUIElementTypeStaticText identifier:name];
    for (XCUIElement *ancestor in ancestors.allElementsBoundByIndex.reverseObjectEnumerator) {
        if ([ancestor.buttons matchingIdentifier:action].count == 1 && ancestor.staticTexts[name].exists)
            return ancestor.buttons[action];
    }
    if (self.ownedSources.count == 1 && [self.ownedSources.firstObject isEqual:name]
        && list.staticTexts[name].exists
        && [list.buttons matchingIdentifier:@"Remove"].count == 1
        && [list.buttons matchingIdentifier:action].count == 1)
        return list.buttons[action];
    XCTFail(@"Cannot unambiguously locate %@ for fixture source %@: %@", action, name, list.debugDescription);
    return nil;
}

- (void)removeOwnedSource:(NSString *)name
{
    XCTAssertTrue([self.ownedSources containsObject:name]);
    [self openSources];
    XCUIElement *label = self.app.scrollViews[@"source_list"].staticTexts[name];
    XCTAssertTrue([label waitForExistenceWithTimeout:15]);
    XCUIElement *remove = [self sourceAction:@"Remove" name:name];
    if (remove == nil) return;
    XCTAssertTrue(remove.hittable);
    [remove tap];
    [self waitFor:^BOOL { return !label.exists && self.app.buttons[@"add_source_files"].enabled; }
            reason:@"only the fixture source was removed" timeout:30];
    [self.ownedSources removeObject:name];
    [self.app.buttons[@"close_sources"] tap];
}

- (void)tearDown
{
    if (self.testRun.failureCount > 0) {
        XCTAttachment *attachment = [XCTAttachment attachmentWithScreenshot:self.app.screenshot];
        attachment.name = @"import-failure";
        attachment.lifetime = XCTAttachmentLifetimeKeepAlways;
        [self addAttachment:attachment];
    }
    // Preserve source/fixture evidence on failure; cleanup can be performed in
    // the visible app after diagnosis. Successful tests remove their own sources.
    [self.app terminate];
    [super tearDown];
}

- (XCUIElement *)pickerItem:(NSString *)name
{
    NSString *stem = name.stringByDeletingPathExtension;
    return [self.app.cells matchingPredicate:[NSPredicate predicateWithFormat:
        @"identifier == %@ OR identifier == %@ OR label == %@ OR label == %@ OR label BEGINSWITH %@ OR label BEGINSWITH %@",
        name, stem, name, stem, [name stringByAppendingString:@","], [stem stringByAppendingString:@","]]].firstMatch;
}

- (void)openFixtureRootInPicker
{
    // Files may remember the previous import's folder. Navigate using visible
    // system labels, with a bounded number of parent/location transitions.
    for (NSUInteger attempt = 0; attempt < 8; ++attempt) {
        XCUIElement *root = [self pickerItem:FixtureRoot];
        if (root.exists && root.hittable) { [root tap]; return; }
        XCUIElement *local = self.app.cells[@"On My iPhone"];
        if (!local.exists) local = self.app.staticTexts[@"On My iPhone"];
        if (!local.exists) local = self.app.cells[@"On My iPad"];
        if (!local.exists) local = self.app.staticTexts[@"On My iPad"];
        if (local.exists && local.hittable) { [local tap]; continue; }
        XCUIElement *browse = self.app.tabBars.buttons[@"Browse"];
        if (browse.exists && browse.hittable && !browse.selected) { [browse tap]; continue; }
        // The first navigation-bar button is a UIKit Back button only if it
        // carries one of these observed destination names. Never tap blindly.
        XCUIElement *back = [self.app.navigationBars.buttons matchingPredicate:[NSPredicate predicateWithFormat:
            @"label IN %@", @[@"Browse", @"On My iPhone", @"On My iPad", FixtureRoot, @"Single"]]].firstMatch;
        if ([back waitForExistenceWithTimeout:3] && back.hittable) { [back tap]; continue; }
        (void)[root waitForExistenceWithTimeout:3];
    }
    XCTFail(@"Files cannot find %@. Stage fixtures and verify the local provider root first. %@",
            FixtureRoot, self.app.debugDescription);
}

- (void)importSource:(NSString *)name directory:(BOOL)directory
{
    [self openSources];
    [self.app.buttons[directory ? @"add_source" : @"add_source_files"] tap];
    [self openFixtureRootInPicker];
    if (!directory) {
        XCUIElement *singleFolder = [self pickerItem:@"Single"];
        XCTAssertTrue([singleFolder waitForExistenceWithTimeout:10]);
        [singleFolder tap];
    }
    XCUIElement *item = [self pickerItem:name];
    XCTAssertTrue([item waitForExistenceWithTimeout:10], @"missing staged item %@", name);
    [item tap];
    XCUIElement *open = self.app.buttons[@"Open"];
    XCTAssertTrue([open waitForExistenceWithTimeout:5]);
    XCTAssertTrue(open.enabled);
    if (![self.ownedSources containsObject:name]) [self.ownedSources addObject:name];
    [open tap];
    XCTAssertTrue([self.app.scrollViews[@"source_list"].staticTexts[name] waitForExistenceWithTimeout:30]);
    [self waitFor:^BOOL { return self.app.buttons[@"close_sources"].enabled; }
            reason:@"picker import completed" timeout:45];
    [self.app.buttons[@"close_sources"] tap];
}

- (void)expectGameCount:(NSUInteger)count
{
    // The LazyHGrid only instantiates visible cards. The status is rendered from
    // the full filtered snapshot, and therefore measures the actual library.
    NSString *label = [NSString stringWithFormat:@"%lu games · Swipe to browse", (unsigned long)count];
    XCTAssertTrue([self.app.staticTexts[label] waitForExistenceWithTimeout:30],
                  @"expected full library count %@: %@", label, self.app.debugDescription);
}

- (void)search:(NSString *)query
{
    [self closeSearch];
    [self.app.buttons[@"open_search"] tap];
    XCUIElement *input = self.app.textFields[@"search_input"];
    XCTAssertTrue([input waitForExistenceWithTimeout:5]);
    [input tap];
    [input typeText:query];
    XCTAssertEqualObjects(input.value, query);
    // Dismiss the landscape keyboard without clearing the query.
    if (self.app.keyboards.count > 0) {
        [input typeText:@"\n"];
        [self waitFor:^BOOL { return self.app.keyboards.count == 0; }
                reason:@"search keyboard dismissed" timeout:5];
    }
}

- (void)testSingleFilesImportWithBuiltinFavoriteRestartAndRemove
{
    NSString *builtinID = self.cards.firstMatch.identifier;
    [self importSource:SingleSource directory:NO];
    [self expectGameCount:(1 + self.bundledCount)];
    [self importSource:SingleSource directory:NO];
    [self expectGameCount:(1 + self.bundledCount)];
    [self openSources];
    XCTAssertEqual([self.app.scrollViews[@"source_list"].buttons matchingIdentifier:@"Remove"].count, 1u,
                   @"picking the same file twice must leave one removable source");
    [self.app.buttons[@"close_sources"] tap];
    XCTAssertTrue(self.app.buttons[builtinID].exists, @"single import must preserve the pre-existing builtin");
    [self search:@"FlyNES-E2E-Single"];
    [self expectGameCount:1];
    NSString *importID = self.cards.firstMatch.identifier;
    XCTAssertNotEqualObjects(importID, builtinID);
    [self.cards.firstMatch tap];
    XCTAssertEqualObjects(self.app.buttons[@"favorite_toggle"].label, @"Add to Favorites");
    [self.app.buttons[@"favorite_toggle"] tap];
    [self waitFor:^BOOL { return [self.app.buttons[@"favorite_toggle"].label isEqual:@"Remove from Favorites"]; }
            reason:@"favorite committed" timeout:10];
    [self.app.buttons[@"category_favorites"] tap];
    [self expectGameCount:1];
    [self.app terminate];
    [self.app launch];
    XCTAssertTrue([self.app.textFields[@"search_input"] waitForExistenceWithTimeout:15]);
    XCTAssertEqualObjects(self.app.textFields[@"search_input"].value, @"FlyNES-E2E-Single");
    XCTAssertTrue(self.app.buttons[@"category_favorites"].selected);
    [self expectGameCount:1];
    XCTAssertEqualObjects(self.cards.firstMatch.identifier, importID);
    XCTAssertEqualObjects(self.app.buttons[@"favorite_toggle"].label, @"Remove from Favorites");
    // Resolve and run the imported payload through the normal runtime route.
    XCTAssertTrue(self.app.buttons[@"launch_selected"].enabled);
    [self.app.buttons[@"launch_selected"] tap];
    XCTAssertTrue([self.app.buttons[@"OPEN_PAUSE"] waitForExistenceWithTimeout:15]);
    [self.app.buttons[@"OPEN_PAUSE"] tap];
    XCTAssertTrue([self.app.buttons[@"game_center"] waitForExistenceWithTimeout:5]);
    [self.app.buttons[@"game_center"] tap];
    XCTAssertTrue([self.app.buttons[@"category_recent"] waitForExistenceWithTimeout:10]);
    [self.app.buttons[@"category_recent"] tap];
    [self expectGameCount:1];
    XCTAssertEqualObjects(self.cards.firstMatch.identifier, importID);
    [self.app.buttons[@"category_favorites"] tap];
    [self expectGameCount:1];
    // Restore this fixture's favorite before removing its source.
    [self.app.buttons[@"favorite_toggle"] tap];
    XCTAssertTrue([self.app.staticTexts[@"No matching games"] waitForExistenceWithTimeout:10]);
    [self closeSearch];
    [self.app.buttons[@"category_all"] tap];
    [self removeOwnedSource:SingleSource];
    [self waitFor:^BOOL { return !self.app.buttons[importID].exists && self.app.buttons[builtinID].exists; }
            reason:@"removing the imported source preserves the builtin" timeout:15];
    XCTAssertEqual(self.cards.count, self.bundledCount);
    [self.app terminate];
    [self.app launch];
    XCTAssertTrue([self.app.buttons[builtinID] waitForExistenceWithTimeout:15]);
    XCTAssertFalse(self.app.buttons[importID].exists);
    XCTAssertEqual(self.cards.count, self.bundledCount, @"repeated import removal persists across restart");
}

- (void)testHundredGameDirectoryDuplicateAliasesSearchRescanRestartAndRemove
{
    NSString *builtinID = self.cards.firstMatch.identifier;
    [self importSource:HundredSource directory:YES];
    [self expectGameCount:(100 + self.bundledCount)]; // 200 directory files => 100 canonical games + every bundled game.
    [self search:@"FlyNES-E2E-Game"];
    [self expectGameCount:100];
    [self search:@"FlyNES-E2E-Game-099"];
    [self expectGameCount:1];
    NSString *canonicalID = self.cards.firstMatch.identifier;
    [self search:@"FlyNES-E2E-Alias-099"];
    [self expectGameCount:1];
    XCTAssertEqualObjects(self.cards.firstMatch.identifier, canonicalID, @"both filenames refer to one canonical card");
    [self.cards.firstMatch tap];
    [self.app.buttons[@"favorite_toggle"] tap];
    [self waitFor:^BOOL { return [self.app.buttons[@"favorite_toggle"].label isEqual:@"Remove from Favorites"]; }
            reason:@"directory favorite committed" timeout:10];
    [self.app terminate];
    [self.app launch];
    [self expectGameCount:1];
    XCTAssertEqualObjects(self.cards.firstMatch.identifier, canonicalID);
    XCTAssertEqualObjects(self.app.buttons[@"favorite_toggle"].label, @"Remove from Favorites");
    [self search:@"FlyNES-E2E-No-Match"];
    XCTAssertTrue([self.app.staticTexts[@"No matching games"] waitForExistenceWithTimeout:10]);
    XCTAssertEqual(self.cards.count, 0u);
    XCTAssertFalse(self.app.buttons[@"launch_selected"].enabled);
    [self closeSearch];
    [self openSources];
    [[self sourceAction:@"Rescan" name:HundredSource] tap];
    [self waitFor:^BOOL { return self.app.buttons[@"close_sources"].enabled; }
            reason:@"directory rescan finished" timeout:45];
    [self.app.buttons[@"close_sources"] tap];
    [self expectGameCount:(100 + self.bundledCount)];
    [self search:@"FlyNES-E2E-Alias-099"];
    [self expectGameCount:1];
    XCTAssertEqualObjects(self.cards.firstMatch.identifier, canonicalID);
    [self.app.buttons[@"favorite_toggle"] tap];
    [self waitFor:^BOOL { return [self.app.buttons[@"favorite_toggle"].label isEqual:@"Add to Favorites"]; }
            reason:@"fixture favorite restored" timeout:10];
    [self closeSearch];
    [self removeOwnedSource:HundredSource];
    [self search:@"FlyNES-E2E"];
    XCTAssertTrue([self.app.staticTexts[@"No matching games"] waitForExistenceWithTimeout:10]);
    XCTAssertEqual(self.cards.count, 0u);
    [self closeSearch];
    XCTAssertTrue([self.app.buttons[builtinID] waitForExistenceWithTimeout:10]);
    XCTAssertEqual(self.cards.count, self.bundledCount);
    [self.app terminate];
    [self.app launch];
    XCTAssertTrue([self.app.buttons[builtinID] waitForExistenceWithTimeout:15]);
    XCTAssertEqual(self.cards.count, self.bundledCount, @"source removal persists across restart");
}

- (void)testCancelFilesPickerPreservesExistingLibrary
{
    NSString *builtinID = self.cards.firstMatch.identifier;
    [self openSources];
    [self.app.buttons[@"add_source_files"] tap];
    XCUIElement *cancel = self.app.buttons[@"Cancel"];
    XCTAssertTrue([cancel waitForExistenceWithTimeout:10]);
    [cancel tap];
    XCTAssertTrue([self.app.buttons[@"close_sources"] waitForExistenceWithTimeout:10]);
    XCTAssertEqual([self.app.scrollViews[@"source_list"].buttons matchingIdentifier:@"Remove"].count, 0u);
    [self.app.buttons[@"close_sources"] tap];
    XCTAssertTrue([self.app.buttons[builtinID] waitForExistenceWithTimeout:10]);
    XCTAssertEqual(self.cards.count, self.bundledCount);
}
@end
