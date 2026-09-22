#import <XCTest/XCTest.h>

#import "FlyNesNearbyBridge.h"

@interface NearbyMvpBridgeTests : XCTestCase
@end

@implementation NearbyMvpBridgeTests

- (void)testHostPublishesSharedInviteAsPlayerOne
{
    FlyNesNearbyBridge *bridge = FlyNesNearbyBridge.sharedInstance;
    [bridge cancel];
    NSError *error = nil;
    XCTAssertTrue([bridge startHost:&error], @"%@", error);
    NSString *invite = nil;
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:8.0];
    do {
        invite = bridge.inviteText;
        if (invite.length == 0) [NSThread sleepForTimeInterval:0.02];
    } while (invite.length == 0 && [deadline timeIntervalSinceNow] > 0);
    XCTAssertTrue([invite hasPrefix:@"flynes-lan-v1:"]);
    XCTAssertEqual([bridge.snapshot[@"role"] unsignedIntegerValue], 1u);
    [bridge cancel];
}

@end
