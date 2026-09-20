#import <XCTest/XCTest.h>
#import "../app/bridge/FlyNesAppBridge.h"

#include <flynes/flynes_session.h>

// Declare the intended public bridge seam before its implementation exists so
// the RED test fails on the missing behavior, not on an undeclared selector.
@interface FlyNesAppBridge (NearbySessionV2BridgeTests)
- (NSDictionary<NSString *, id> *)nearbySessionSnapshotV2;
@end

@interface NearbySessionV2BridgeTests : XCTestCase
@end

@implementation NearbySessionV2BridgeTests

- (void)testMissingDiscoveryKeepsTheProductionV2OwnerFailClosed
{
    self.continueAfterFailure = NO;
    FlyNesAppBridge *bridge = [[FlyNesAppBridge alloc] init];
    XCTAssertTrue([bridge respondsToSelector:@selector(nearbySessionSnapshotV2)]);

    NSDictionary<NSString *, id> *snapshot = @{};
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:3.0];
    do {
        snapshot = [bridge nearbySessionSnapshotV2];
        if ([snapshot[@"engineState"] unsignedIntValue] == FLY_SESSION_ENGINE_READY_V2)
            break;
        [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode
                              beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    } while ([deadline timeIntervalSinceNow] > 0);

    XCTAssertEqual([snapshot[@"engineState"] unsignedIntValue], FLY_SESSION_ENGINE_READY_V2);
    XCTAssertEqual([snapshot[@"linkState"] unsignedIntValue], FLY_SESSION_LINK_IDLE_V2);
    XCTAssertEqualObjects(snapshot[@"primaryReasonKey"], @"nearby.reason.discovery.unavailable");
    XCTAssertEqual([snapshot[@"actionCount"] unsignedIntValue], 0u);
}

@end
