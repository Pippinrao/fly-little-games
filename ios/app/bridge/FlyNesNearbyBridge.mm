#import "FlyNesNearbyBridge.h"

#import "BuiltinGames.h"

#include <flynes/flynes_nearby_mvp.h>

#import <Security/Security.h>

#include <arpa/inet.h>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>

namespace {
NSError *nearby_error(NSInteger code, NSString *message)
{
    return [NSError errorWithDomain:@"FlyNesNearby" code:code
                           userInfo:@{NSLocalizedDescriptionKey: message}];
}

NSString *local_ipv4()
{
    struct ifaddrs *interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) return nil;
    NSString *fallback = nil;
    for (struct ifaddrs *item = interfaces; item != nullptr; item = item->ifa_next) {
        if (item->ifa_addr == nullptr || item->ifa_addr->sa_family != AF_INET) continue;
        if ((item->ifa_flags & IFF_UP) == 0 || (item->ifa_flags & IFF_LOOPBACK) != 0) continue;
        char address[INET_ADDRSTRLEN]{};
        const auto *ipv4 = reinterpret_cast<const struct sockaddr_in *>(item->ifa_addr);
        if (inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address)) == nullptr) continue;
        NSString *value = [NSString stringWithUTF8String:address];
        if (strcmp(item->ifa_name, "en0") == 0) { fallback = value; break; }
        if (fallback == nil) fallback = value;
    }
    freeifaddrs(interfaces);
    return fallback;
}

NSData *bundled_rom(FlyNesBuiltinGame *game)
{
    if (game == nil) return nil;
    NSString *resource = [FlyNesBuiltinGames resourceNameForAssetFilename:game.assetFilename];
    NSString *path = [NSBundle.mainBundle pathForResource:resource ofType:@"nes"];
    return path == nil ? nil : [NSData dataWithContentsOfFile:path];
}
}

@implementation FlyNesNearbyBridge {
    fly_lan_mvp_session *session_;
    NSString *_gameTitle;
    NSString *_canonicalId;
}

+ (instancetype)sharedInstance
{
    static FlyNesNearbyBridge *instance;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ instance = [[FlyNesNearbyBridge alloc] init]; });
    return instance;
}

- (instancetype)init
{
    if ((self = [super init])) {
        _gameTitle = @"";
        _canonicalId = @"";
    }
    return self;
}

- (void)dealloc
{
    if (session_ != nullptr) fly_lan_mvp_destroy(session_);
}

- (NSString *)gameTitle { return _gameTitle; }
- (NSString *)canonicalId { return _canonicalId; }

- (BOOL)replaceSession:(NSError **)error
{
    if (session_ != nullptr) fly_lan_mvp_destroy(session_);
    session_ = fly_lan_mvp_create();
    _gameTitle = @"";
    _canonicalId = @"";
    if (session_ != nullptr) return YES;
    if (error) *error = nearby_error(1, @"Nearby transport is unavailable");
    return NO;
}

- (BOOL)startHost:(NSError **)error
{
    NSString *address = local_ipv4();
    if (address.length == 0) {
        if (error) *error = nearby_error(2, @"No reachable IPv4 interface");
        return NO;
    }
    if (![self replaceSession:error]) return NO;
    uint8_t token[16]{};
    if (SecRandomCopyBytes(kSecRandomDefault, sizeof(token), token) != errSecSuccess) {
        if (error) *error = nearby_error(3, @"Secure random generation failed");
        [self cancel];
        return NO;
    }
    const BOOL started = fly_lan_mvp_host(session_, address.UTF8String, token) == 1;
    memset(token, 0, sizeof(token));
    if (!started) {
        if (error) *error = nearby_error(4, @"Nearby host could not start");
        [self cancel];
    }
    return started;
}

- (BOOL)joinInvite:(NSString *)invite error:(NSError **)error
{
    NSString *address = local_ipv4();
    NSData *utf8 = [invite dataUsingEncoding:NSUTF8StringEncoding];
    if (address.length == 0 || utf8.length == 0 || ![self replaceSession:error]) return NO;
    const BOOL started = fly_lan_mvp_join(session_, address.UTF8String,
        static_cast<const char *>(utf8.bytes), utf8.length) == 1;
    if (!started) {
        if (error) *error = nearby_error(5, @"Nearby invitation is invalid or unreachable");
        [self cancel];
    }
    return started;
}

- (NSString *)inviteText
{
    if (session_ == nullptr) return nil;
    const size_t size = fly_lan_mvp_copy_invite(session_, nullptr, 0);
    if (size == 0 || size > 256) return nil;
    char bytes[256]{};
    if (fly_lan_mvp_copy_invite(session_, bytes, sizeof(bytes)) != size) return nil;
    return [NSString stringWithUTF8String:bytes];
}

- (NSDictionary<NSString *, id> *)snapshot
{
    fly_lan_mvp_snapshot value{};
    if (session_ == nullptr || fly_lan_mvp_snapshot_read(session_, &value) != 1)
        return @{ @"state": @0, @"role": @0 };
    return @{ @"state": @(value.state), @"reason": @(value.reason), @"role": @(value.role),
              @"localConfigured": @(value.local_configured), @"peerConfigured": @(value.peer_configured),
              @"localReady": @(value.local_ready), @"peerReady": @(value.peer_ready),
              @"paused": @(value.paused), @"completedFrames": @(value.completed_frames),
              @"peerGameKey": [NSString stringWithUTF8String:value.peer_game_key] ?: @"" };
}

- (NSString *)configureLocalGameIfNeeded:(NSError **)error
{
    fly_lan_mvp_snapshot value{};
    if (session_ == nullptr || fly_lan_mvp_snapshot_read(session_, &value) != 1) return @"";
    if (value.local_configured != 0) return _gameTitle;
    FlyNesBuiltinGame *game = nil;
    BOOL host = value.role == FLY_LAN_MVP_ROLE_HOST_P1;
    if (host && value.state == FLY_LAN_MVP_LOBBY) {
        for (FlyNesBuiltinGame *candidate in FlyNesBuiltinGames.shared.all) {
            if ([candidate.multiplayerEligibility isEqualToString:@"SUPPORTED"] &&
                candidate.multiplayerMaxPlayers == 2) { game = candidate; break; }
        }
    } else if (!host && value.role == FLY_LAN_MVP_ROLE_GUEST_P2 &&
               value.state == FLY_LAN_MVP_CONFIGURING && value.peer_game_key[0] != '\0') {
        game = [FlyNesBuiltinGames.shared byCanonicalId:
            [NSString stringWithUTF8String:value.peer_game_key]];
    }
    NSData *rom = bundled_rom(game);
    if (game == nil || rom.length == 0) return @"";
    const int selected = host
        ? fly_lan_mvp_select_game(session_, static_cast<const uint8_t *>(rom.bytes), rom.length,
                                  game.canonicalId.UTF8String)
        : fly_lan_mvp_select_rom(session_, static_cast<const uint8_t *>(rom.bytes), rom.length);
    if (selected != 1) {
        if (error) *error = nearby_error(6, @"The matching local game could not be selected");
        return @"";
    }
    _canonicalId = [game.canonicalId copy];
    _gameTitle = [[NSLocale.preferredLanguages.firstObject ?: @"en" lowercaseString]
        hasPrefix:@"zh"] ? [game.titleZhHans copy] : [game.titleEn copy];
    return _gameTitle;
}

- (BOOL)confirm { return session_ != nullptr && fly_lan_mvp_confirm(session_) == 1; }
- (BOOL)setPaused:(BOOL)paused { return session_ != nullptr && fly_lan_mvp_set_paused(session_, paused) == 1; }
- (BOOL)returnLobby { return session_ != nullptr && fly_lan_mvp_return_lobby(session_) == 1; }
- (BOOL)stepWithButtons:(uint32_t)buttons { return session_ != nullptr && fly_lan_mvp_submit_input(session_, buttons) == 1; }

- (NSData *)copyLatestRgb565Frame
{
    if (session_ == nullptr) return nil;
    NSMutableData *data = [NSMutableData dataWithLength:FLY_RUNTIME_RGB565_BYTES];
    fly_latest_frame_v1 meta{};
    meta.struct_size = FLY_LATEST_FRAME_V1_SIZE;
    meta.version = FLY_LATEST_FRAME_VERSION_1;
    return fly_lan_mvp_copy_latest_frame(session_, data.mutableBytes, data.length, &meta) == 1 ? data : nil;
}

- (NSData *)pullPCM
{
    if (session_ == nullptr) return [NSData data];
    int16_t samples[4096]{};
    fly_pcm_block_v1 block{};
    block.struct_size = FLY_PCM_BLOCK_V1_SIZE;
    block.version = FLY_PCM_BLOCK_VERSION_1;
    if (fly_lan_mvp_pull_pcm(session_, samples, 4096, &block) != 1) return [NSData data];
    return [NSData dataWithBytes:samples length:block.sample_count * sizeof(int16_t)];
}

- (void)cancel
{
    if (session_ != nullptr) { fly_lan_mvp_destroy(session_); session_ = nullptr; }
    _gameTitle = @"";
    _canonicalId = @"";
}

@end
