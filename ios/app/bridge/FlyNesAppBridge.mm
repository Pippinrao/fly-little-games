#import "FlyNesAppBridge.h"

#include <flynes/flynes_app.h>
#include "flynes/product/game_center_item.hpp"
#include "flynes/product/game_center_state.hpp"

#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace {

NSError *flynes_error(NSInteger code, NSString *message)
{
    return [NSError errorWithDomain:@"com.flynes.app"
                               code:code
                           userInfo:@{NSLocalizedDescriptionKey : message}];
}

uint32_t uint32_value(NSDictionary<NSString *, id> *settings, NSString *key, uint32_t fallback)
{
    id value = settings[key];
    if ([value isKindOfClass:[NSNumber class]])
        return [value unsignedIntValue];
    return fallback;
}

float float_value(NSDictionary<NSString *, id> *settings, NSString *key, float fallback)
{
    id value = settings[key];
    if ([value isKindOfClass:[NSNumber class]])
        return [value floatValue];
    return fallback;
}

std::string basename_utf8(const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return path;
    return path.substr(slash + 1u);
}

NSDictionary<NSString *, id> *catalog_row_dictionary(const fly_catalog_entry& entry,
                                                     const fly_catalog_user_state& user)
{
    const std::string canonical = entry.canonical_id_utf8 == nullptr ? std::string{}
                                                                    : entry.canonical_id_utf8;
    const std::string display = entry.display_name_utf8 == nullptr ? std::string{}
                                                                  : entry.display_name_utf8;
    const std::string relative = entry.source_relative_path_utf8 == nullptr
                                     ? std::string{}
                                     : entry.source_relative_path_utf8;
    const std::string original_filename = !display.empty() ? display : basename_utf8(relative);
    const std::string title_en = !display.empty() ? display : original_filename;
    const std::string title_zh_hans;
    std::int64_t last_played = 0;
    if (user.last_played_sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        last_played = std::numeric_limits<std::int64_t>::max();
    else
        last_played = static_cast<std::int64_t>(user.last_played_sequence);

    return @{
        @"canonicalId" : @(canonical.c_str()),
        @"displayName" : @(title_en.c_str()),
        @"titleEn" : @(title_en.c_str()),
        @"titleZhHans" : @(title_zh_hans.c_str()),
        @"originalFilename" : @(original_filename.c_str()),
        @"compatibilityState" : @(entry.compatibility_state),
        @"freshness" : @(entry.freshness),
        @"sourceScope" : @(entry.source_scope),
        @"favorite" : @(user.favorite),
        @"lastPlayedSequence" : @(static_cast<std::uint64_t>(last_played)),
        @"builtin" : @(entry.source_scope == FLY_SOURCE_SCOPE_BUILTIN ? YES : NO),
    };
}

flynes::product::GameCenterItem game_center_item_from_row(NSDictionary<NSString *, id> *row)
{
    auto utf8 = [](id value) -> const char * {
        if (![value isKindOfClass:[NSString class]])
            return "";
        const char *bytes = [value UTF8String];
        return bytes != nullptr ? bytes : "";
    };
    std::int64_t last_played = 0;
    if ([row[@"lastPlayedSequence"] isKindOfClass:[NSNumber class]])
        last_played = [row[@"lastPlayedSequence"] longLongValue];
    const bool builtin = [row[@"builtin"] respondsToSelector:@selector(boolValue)]
                             ? [row[@"builtin"] boolValue]
                             : NO;
    const bool favorite = [row[@"favorite"] respondsToSelector:@selector(unsignedIntValue)]
                              ? [row[@"favorite"] unsignedIntValue] != 0
                              : NO;
    return flynes::product::GameCenterItem{utf8(row[@"canonicalId"]),
                                           utf8(row[@"titleEn"]),
                                           utf8(row[@"titleZhHans"]),
                                           builtin,
                                           favorite,
                                           last_played,
                                           utf8(row[@"originalFilename"])};
}

} // namespace

@implementation FlyNesAppBridge {
    std::mutex mutex_;
    fly_app_t *app_;
}

+ (instancetype)sharedInstance
{
    static FlyNesAppBridge *instance;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      instance = [[FlyNesAppBridge alloc] init];
    });
    return instance;
}

- (instancetype)init
{
    self = [super init];
    if (self != nil)
        app_ = nullptr;
    return self;
}

- (void)dealloc
{
    std::lock_guard<std::mutex> lock(mutex_);
    fly_app_destroy(app_);
    app_ = nullptr;
}

- (BOOL)ensureApp:(NSError **)error
{
    if (app_ != nullptr)
        return YES;

    NSFileManager *files = NSFileManager.defaultManager;
    NSURL *documents =
        [files URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL *caches =
        [files URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask].firstObject;
    if (documents.path == nil || caches.path == nil)
    {
        if (error != nullptr)
            *error = flynes_error(-1, @"sandbox roots are unavailable");
        return NO;
    }
    return [self createWithDataRoot:documents.path cacheRoot:caches.path error:error];
}

- (BOOL)createWithDataRoot:(NSString *)dataRoot
                 cacheRoot:(NSString *)cacheRoot
                     error:(NSError **)error
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (app_ != nullptr)
    {
        fly_app_destroy(app_);
        app_ = nullptr;
    }

    const char *data_utf8 = dataRoot.UTF8String;
    const char *cache_utf8 = cacheRoot.UTF8String;
    if (data_utf8 == nullptr || cache_utf8 == nullptr)
    {
        if (error != nullptr)
            *error = flynes_error(FLY_RESULT_INVALID_ARGUMENT, @"root paths must be UTF-8");
        return NO;
    }

    fly_platform_capabilities capabilities{};
    capabilities.struct_size = FLY_PLATFORM_CAPABILITIES_V1_SIZE;
    capabilities.version = FLY_PLATFORM_CAPABILITIES_VERSION_1;
    capabilities.flags = 0;

    fly_app_config config{};
    config.struct_size = FLY_APP_CONFIG_V1_SIZE;
    config.version = FLY_APP_CONFIG_VERSION_1;
    config.data_root_utf8 = data_utf8;
    config.cache_root_utf8 = cache_utf8;
    config.platform_capabilities = &capabilities;
    config.data_root_utf8_length = static_cast<uint32_t>(std::strlen(data_utf8));
    config.cache_root_utf8_length = static_cast<uint32_t>(std::strlen(cache_utf8));

    const fly_result result = fly_app_create(&config, &app_);
    if (result != FLY_RESULT_OK || app_ == nullptr)
    {
        app_ = nullptr;
        if (error != nullptr)
            *error = flynes_error(result, @"fly_app_create failed");
        return NO;
    }
    return YES;
}

- (NSDictionary<NSString *, id> *)settingsGet
{
    std::lock_guard<std::mutex> lock(mutex_);
    NSError *create_error = nil;
    if (app_ == nullptr && ![self ensureAppLocked:&create_error])
        return @{};

    char locale[FLY_SETTINGS_LOCALE_MAX_UTF8_BYTES + 1] = {};
    char last_played[FLY_CANONICAL_ID_MAX_UTF8_BYTES + 1] = {};
    fly_settings_snapshot snapshot{};
    snapshot.struct_size = FLY_SETTINGS_SNAPSHOT_V1_SIZE;
    snapshot.version = FLY_SETTINGS_SNAPSHOT_VERSION_1;
    snapshot.locale_tag_utf8 = locale;
    snapshot.locale_tag_capacity = static_cast<uint32_t>(sizeof(locale));
    snapshot.last_played_id_utf8 = last_played;
    snapshot.last_played_id_capacity = static_cast<uint32_t>(sizeof(last_played));

    if (fly_settings_get(app_, &snapshot) != FLY_RESULT_OK)
        return @{};

    return @{
        @"aspect_mode" : @(snapshot.aspect_mode),
        @"video_quality_preset" : @(snapshot.video_quality_preset),
        @"custom_refresh_policy" : @(snapshot.custom_refresh_policy),
        @"custom_temporal_mode" : @(snapshot.custom_temporal_mode),
        @"custom_spatial_mode" : @(snapshot.custom_spatial_mode),
        @"custom_post_effect" : @(snapshot.custom_post_effect),
        @"adaptive_protection" : @(snapshot.adaptive_protection),
        @"layout_preset" : @(snapshot.layout_preset),
        @"direction_mode" : @(snapshot.direction_mode),
        @"button_scale" : @(snapshot.button_scale),
        @"vertical_offset" : @(snapshot.vertical_offset),
        @"control_opacity" : @(snapshot.control_opacity),
        @"joystick_scale" : @(snapshot.joystick_scale),
        @"dead_zone" : @(snapshot.dead_zone),
        @"haptic_level" : @(snapshot.haptic_level),
        @"distinct_ab_haptics" : @(snapshot.distinct_ab_haptics),
        @"audio_enabled" : @(snapshot.audio_enabled),
        @"audio_focus_policy" : @(snapshot.audio_focus_policy),
        @"autosave_enabled" : @(snapshot.autosave_enabled),
        @"locale_tag" : @(locale),
        @"last_played_id" : @(last_played),
    };
}

- (BOOL)applySettings:(NSDictionary<NSString *, id> *)settings error:(NSError **)error
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (app_ == nullptr && ![self ensureAppLocked:error])
        return NO;

    char locale[FLY_SETTINGS_LOCALE_MAX_UTF8_BYTES + 1] = {};
    char last_played[FLY_CANONICAL_ID_MAX_UTF8_BYTES + 1] = {};
    fly_settings_snapshot current{};
    current.struct_size = FLY_SETTINGS_SNAPSHOT_V1_SIZE;
    current.version = FLY_SETTINGS_SNAPSHOT_VERSION_1;
    current.locale_tag_utf8 = locale;
    current.locale_tag_capacity = static_cast<uint32_t>(sizeof(locale));
    current.last_played_id_utf8 = last_played;
    current.last_played_id_capacity = static_cast<uint32_t>(sizeof(last_played));
    if (fly_settings_get(app_, &current) != FLY_RESULT_OK)
    {
        if (error != nullptr)
            *error = flynes_error(FLY_RESULT_INTERNAL_ERROR, @"fly_settings_get failed");
        return NO;
    }

    current.aspect_mode = uint32_value(settings, @"aspect_mode", current.aspect_mode);
    current.video_quality_preset =
        uint32_value(settings, @"video_quality_preset", current.video_quality_preset);
    current.custom_refresh_policy =
        uint32_value(settings, @"custom_refresh_policy", current.custom_refresh_policy);
    current.custom_temporal_mode =
        uint32_value(settings, @"custom_temporal_mode", current.custom_temporal_mode);
    current.custom_spatial_mode =
        uint32_value(settings, @"custom_spatial_mode", current.custom_spatial_mode);
    current.custom_post_effect =
        uint32_value(settings, @"custom_post_effect", current.custom_post_effect);
    current.direction_mode = uint32_value(settings, @"direction_mode", current.direction_mode);
    current.control_opacity = float_value(settings, @"control_opacity", current.control_opacity);
    current.haptic_level = uint32_value(settings, @"haptic_level", current.haptic_level);
    current.distinct_ab_haptics =
        uint32_value(settings, @"distinct_ab_haptics", current.distinct_ab_haptics);
    current.adaptive_protection =
        uint32_value(settings, @"adaptive_protection", current.adaptive_protection);
    current.audio_enabled = uint32_value(settings, @"audio_enabled", current.audio_enabled);
    current.audio_focus_policy =
        uint32_value(settings, @"audio_focus_policy", current.audio_focus_policy);
    current.autosave_enabled = uint32_value(settings, @"autosave_enabled", current.autosave_enabled);
    id locale_value = settings[@"locale_tag"];
    if ([locale_value isKindOfClass:[NSString class]])
    {
        const char *locale_utf8 = [locale_value UTF8String];
        if (locale_utf8 != nullptr)
        {
            std::strncpy(locale, locale_utf8, sizeof(locale) - 1);
            locale[sizeof(locale) - 1] = '\0';
        }
    }
    current.locale_tag_utf8_length = static_cast<uint32_t>(std::strlen(locale));
    current.last_played_id_utf8_length = static_cast<uint32_t>(std::strlen(last_played));

    const fly_result result = fly_settings_apply(app_, &current);
    if (result != FLY_RESULT_OK)
    {
        if (error != nullptr)
            *error = flynes_error(result, @"fly_settings_apply failed");
        return NO;
    }
    return YES;
}

- (NSArray<NSDictionary<NSString *, id> *> *)catalogSnapshotGames
{
    std::lock_guard<std::mutex> lock(mutex_);
    NSError *create_error = nil;
    if (app_ == nullptr && ![self ensureAppLocked:&create_error])
        return @[];

    fly_catalog_snapshot_t *snapshot = nullptr;
    if (fly_catalog_snapshot(app_, &snapshot) != FLY_RESULT_OK || snapshot == nullptr)
        return @[];

    uint64_t count = 0;
    fly_catalog_snapshot_count(snapshot, &count);
    NSMutableArray<NSDictionary<NSString *, id> *> *games =
        [NSMutableArray arrayWithCapacity:static_cast<NSUInteger>(count)];
    for (uint64_t index = 0; index < count; ++index)
    {
        char canonical[FLY_CANONICAL_ID_MAX_UTF8_BYTES + 1] = {};
        char variant[FLY_CANONICAL_ID_MAX_UTF8_BYTES + 1] = {};
        char display[FLY_SCAN_DISPLAY_NAME_MAX_UTF8_BYTES + 1] = {};
        char relative[FLY_SCAN_RELATIVE_PATH_MAX_UTF8_BYTES + 1] = {};
        fly_catalog_entry entry{};
        entry.struct_size = FLY_CATALOG_ENTRY_V1_SIZE;
        entry.version = FLY_CATALOG_ENTRY_VERSION_1;
        entry.canonical_id_utf8 = canonical;
        entry.canonical_id_capacity = static_cast<uint32_t>(sizeof(canonical));
        entry.variant_id_utf8 = variant;
        entry.variant_id_capacity = static_cast<uint32_t>(sizeof(variant));
        entry.display_name_utf8 = display;
        entry.display_name_capacity = static_cast<uint32_t>(sizeof(display));
        entry.source_relative_path_utf8 = relative;
        entry.source_relative_path_capacity = static_cast<uint32_t>(sizeof(relative));
        if (fly_catalog_snapshot_get(snapshot, index, &entry) != FLY_RESULT_OK)
            continue;
        fly_catalog_user_state user{};
        user.struct_size = FLY_CATALOG_USER_STATE_V1_SIZE;
        user.version = FLY_CATALOG_USER_STATE_VERSION_1;
        fly_catalog_user_state_get(app_, canonical,
                                   static_cast<uint32_t>(std::strlen(canonical)), &user);
        [games addObject:catalog_row_dictionary(entry, user)];
    }
    fly_catalog_snapshot_release(snapshot);
    return games;
}

- (NSArray<NSDictionary<NSString *, id> *> *)gameCenterFilteredGamesForCategory:(NSString *)category
                                                                         query:(NSString *)query
{
    NSArray<NSDictionary<NSString *, id> *> *snapshot = [self catalogSnapshotGames];
    NSMutableArray<NSDictionary<NSString *, id> *> *rows =
        [NSMutableArray arrayWithCapacity:snapshot.count + 1u];
    BOOL has_builtin = NO;
    for (NSDictionary<NSString *, id> *row in snapshot)
    {
        [rows addObject:row];
        if ([row[@"canonicalId"] isEqualToString:@"builtin"])
            has_builtin = YES;
    }
    if (!has_builtin)
    {
        [rows insertObject:@{
            @"canonicalId" : @"builtin",
            @"displayName" : @"From Below",
            @"titleEn" : @"From Below",
            @"titleZhHans" : @"来自下方",
            @"originalFilename" : @"from_below.nes",
            @"compatibilityState" : @(FLY_COMPATIBILITY_PLAYABLE),
            @"freshness" : @(0),
            @"sourceScope" : @(FLY_SOURCE_SCOPE_BUILTIN),
            @"favorite" : @(0),
            @"lastPlayedSequence" : @(0ULL),
            @"builtin" : @YES,
        } atIndex:0];
    }

    using flynes::product::GameCenterItem;
    using flynes::product::GameCenterState;

    std::vector<GameCenterItem> items;
    items.reserve(static_cast<std::size_t>(rows.count));
    NSMutableDictionary<NSString *, NSDictionary<NSString *, id> *> *by_id =
        [NSMutableDictionary dictionaryWithCapacity:rows.count];
    for (NSDictionary<NSString *, id> *row in rows)
    {
        id canonical_value = row[@"canonicalId"];
        if (![canonical_value isKindOfClass:[NSString class]] || [canonical_value length] == 0)
            continue;
        try
        {
            items.push_back(game_center_item_from_row(row));
            by_id[canonical_value] = row;
        }
        catch (const std::exception&)
        {
            continue;
        }
    }

    const char *category_utf8 = category.UTF8String;
    const char *query_utf8 = query.UTF8String;
    GameCenterState state = GameCenterState::restore(category_utf8 != nullptr ? category_utf8 : "",
                                                     query_utf8 != nullptr ? query_utf8 : "",
                                                     {});
    const std::vector<GameCenterItem> filtered = state.filtered(items);

    NSMutableArray<NSDictionary<NSString *, id> *> *result =
        [NSMutableArray arrayWithCapacity:static_cast<NSUInteger>(filtered.size())];
    for (const GameCenterItem& item : filtered)
    {
        NSString *canonical = @(item.canonical_id.c_str());
        NSDictionary<NSString *, id> *row = by_id[canonical];
        if (row != nil)
            [result addObject:row];
    }
    return result;
}

- (BOOL)scanBorrowedFd:(int)borrowedFd
          relativePath:(NSString *)relativePath
           displayName:(NSString *)displayName
            sourceUUID:(NSData *)sourceUUID
           sourceScope:(uint32_t)sourceScope
                 error:(NSError **)error
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (app_ == nullptr && ![self ensureAppLocked:error])
        return NO;
    if (borrowedFd < 0 || relativePath.UTF8String == nullptr || displayName.UTF8String == nullptr
        || sourceUUID.length != 16)
    {
        if (error != nullptr)
            *error = flynes_error(FLY_RESULT_INVALID_ARGUMENT, @"scan arguments invalid");
        return NO;
    }

    fly_scan_config config{};
    config.struct_size = FLY_SCAN_CONFIG_V1_SIZE;
    config.version = FLY_SCAN_CONFIG_VERSION_1;
    std::memcpy(config.source_uuid, sourceUUID.bytes, 16);
    config.source_scope = sourceScope;

    fly_scan_t *scan = nullptr;
    fly_result result = fly_scan_begin(app_, &config, &scan);
    if (result != FLY_RESULT_OK || scan == nullptr)
    {
        if (error != nullptr)
            *error = flynes_error(result, @"fly_scan_begin failed");
        return NO;
    }

    const char *relative_utf8 = relativePath.UTF8String;
    const char *display_utf8 = displayName.UTF8String;
    fly_scan_file file{};
    file.struct_size = FLY_SCAN_FILE_V1_SIZE;
    file.version = FLY_SCAN_FILE_VERSION_1;
    file.source_relative_path_utf8 = relative_utf8;
    file.display_name_utf8 = display_utf8;
    file.source_relative_path_utf8_length = static_cast<uint32_t>(std::strlen(relative_utf8));
    file.display_name_utf8_length = static_cast<uint32_t>(std::strlen(display_utf8));
    file.borrowed_fd = borrowedFd;

    fly_scan_file_result file_result{};
    file_result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE;
    file_result.version = FLY_SCAN_FILE_RESULT_VERSION_1;
    result = fly_scan_add_file(scan, &file, &file_result);
    if (result != FLY_RESULT_OK)
    {
        fly_scan_abort(scan);
        if (error != nullptr)
            *error = flynes_error(result, @"fly_scan_add_file failed");
        return NO;
    }
    result = fly_scan_commit(scan, FLY_SCAN_COMPLETENESS_FULL);
    if (result != FLY_RESULT_OK)
    {
        if (error != nullptr)
            *error = flynes_error(result, @"fly_scan_commit failed");
        return NO;
    }
    return YES;
}

- (NSString *)controlLayoutGet
{
    std::lock_guard<std::mutex> lock(mutex_);
    NSError *create_error = nil;
    if (app_ == nullptr && ![self ensureAppLocked:&create_error])
        return @"";

    std::uint32_t required = 0;
    const fly_result sized = fly_control_layout_get(app_, nullptr, 0, &required);
    if (sized != FLY_RESULT_OK && sized != FLY_RESULT_BUFFER_TOO_SMALL)
        return @"";

    std::vector<char> bytes(required == 0 ? 1u : required, '\0');
    if (fly_control_layout_get(app_, bytes.data(), static_cast<std::uint32_t>(bytes.size()),
                               &required)
        != FLY_RESULT_OK)
        return @"";
    if (required == 0)
        return @"";
    const NSUInteger length = static_cast<NSUInteger>(required - 1u);
    NSString *utf8 = [[NSString alloc] initWithBytes:bytes.data()
                                              length:length
                                            encoding:NSUTF8StringEncoding];
    return utf8 != nil ? utf8 : @"";
}

- (BOOL)controlLayoutApply:(NSString *)utf8 error:(NSError **)error
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (app_ == nullptr && ![self ensureAppLocked:error])
        return NO;

    const char *bytes = utf8.UTF8String;
    if (bytes == nullptr)
        bytes = "";
    const fly_result result =
        fly_control_layout_apply(app_, bytes, static_cast<std::uint32_t>(std::strlen(bytes)));
    if (result != FLY_RESULT_OK)
    {
        if (error != nullptr)
            *error = flynes_error(result, @"fly_control_layout_apply failed");
        return NO;
    }
    return YES;
}

- (BOOL)ensureAppLocked:(NSError **)error
{
    // Caller holds mutex_. Duplicate the unlocked create path without re-locking.
    if (app_ != nullptr)
        return YES;
    NSFileManager *files = NSFileManager.defaultManager;
    NSURL *documents =
        [files URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL *caches =
        [files URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask].firstObject;
    if (documents.path == nil || caches.path == nil)
    {
        if (error != nullptr)
            *error = flynes_error(-1, @"sandbox roots are unavailable");
        return NO;
    }

    const char *data_utf8 = documents.path.UTF8String;
    const char *cache_utf8 = caches.path.UTF8String;
    if (data_utf8 == nullptr || cache_utf8 == nullptr)
    {
        if (error != nullptr)
            *error = flynes_error(FLY_RESULT_INVALID_ARGUMENT, @"root paths must be UTF-8");
        return NO;
    }

    fly_platform_capabilities capabilities{};
    capabilities.struct_size = FLY_PLATFORM_CAPABILITIES_V1_SIZE;
    capabilities.version = FLY_PLATFORM_CAPABILITIES_VERSION_1;

    fly_app_config config{};
    config.struct_size = FLY_APP_CONFIG_V1_SIZE;
    config.version = FLY_APP_CONFIG_VERSION_1;
    config.data_root_utf8 = data_utf8;
    config.cache_root_utf8 = cache_utf8;
    config.platform_capabilities = &capabilities;
    config.data_root_utf8_length = static_cast<uint32_t>(std::strlen(data_utf8));
    config.cache_root_utf8_length = static_cast<uint32_t>(std::strlen(cache_utf8));

    const fly_result result = fly_app_create(&config, &app_);
    if (result != FLY_RESULT_OK || app_ == nullptr)
    {
        app_ = nullptr;
        if (error != nullptr)
            *error = flynes_error(result, @"fly_app_create failed");
        return NO;
    }
    return YES;
}

@end
