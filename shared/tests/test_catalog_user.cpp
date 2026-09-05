#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <flynes/flynes_app.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

int failures = 0;
std::atomic<unsigned int> temp_sequence{0};

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

class TempRoot final
{
public:
    TempRoot()
    {
        for (unsigned int attempt = 0u; attempt < 100u; ++attempt)
        {
            path_ = std::filesystem::temp_directory_path() /
                    ("flynes-user-" + std::to_string(temp_sequence.fetch_add(1u)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error))
            {
                break;
            }
            path_.clear();
        }
        check(!path_.empty(), "user-state test creates an isolated data root");
    }

    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;

    ~TempRoot()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    std::string utf8() const { return path_.u8string(); }

private:
    std::filesystem::path path_;
};

const fly_platform_capabilities* capabilities()
{
    static fly_platform_capabilities value{};
    value.struct_size = FLY_PLATFORM_CAPABILITIES_V1_SIZE;
    value.version = FLY_PLATFORM_CAPABILITIES_VERSION_1;
    return &value;
}

fly_app_t* make_app(const std::string& data_root)
{
    fly_app_config config{};
    config.struct_size = FLY_APP_CONFIG_V1_SIZE;
    config.version = FLY_APP_CONFIG_VERSION_1;
    config.data_root_utf8 = data_root.data();
    config.cache_root_utf8 = data_root.data();
    config.platform_capabilities = capabilities();
    config.data_root_utf8_length = static_cast<std::uint32_t>(data_root.size());
    config.cache_root_utf8_length = static_cast<std::uint32_t>(data_root.size());
    fly_app_t* app = nullptr;
    check(fly_app_create(&config, &app) == FLY_RESULT_OK, "user-state test creates app");
    return app;
}

fly_scan_t* begin_scan(fly_app_t* app, std::uint32_t scope = FLY_SOURCE_SCOPE_USER_DIRECTORY)
{
    fly_scan_config config{};
    config.struct_size = FLY_SCAN_CONFIG_V1_SIZE;
    config.version = FLY_SCAN_CONFIG_VERSION_1;
    config.source_uuid[15] = 1u;
    config.source_scope = scope;
    fly_scan_t* scan = nullptr;
    check(fly_scan_begin(app, &config, &scan) == FLY_RESULT_OK, "user-state test begins scan");
    return scan;
}

fly_catalog_user_state empty_user_state()
{
    fly_catalog_user_state value{};
    value.struct_size = FLY_CATALOG_USER_STATE_V1_SIZE;
    value.version = FLY_CATALOG_USER_STATE_VERSION_1;
    return value;
}

fly_catalog_user_state get_user(const fly_app_t* app, const char* canonical_id)
{
    fly_catalog_user_state value = empty_user_state();
    check(fly_catalog_user_state_get(app,
                                     canonical_id,
                                     static_cast<std::uint32_t>(std::strlen(canonical_id)),
                                     &value) == FLY_RESULT_OK,
          "user-state get succeeds");
    return value;
}

void test_abi_layout()
{
    static_assert(FLY_CATALOG_USER_STATE_VERSION_1 == 1u, "user-state version changed");
    static_assert(FLY_SOURCE_STATUS_VERSION_1 == 1u, "source-status version changed");
    static_assert(offsetof(fly_catalog_user_state, struct_size) == 0u,
                  "user-state prefix changed");
    static_assert(offsetof(fly_catalog_user_state, favorite) == 2u * sizeof(std::uint32_t),
                  "user-state favorite offset changed");
    static_assert(FLY_CATALOG_USER_STATE_V1_SIZE == sizeof(fly_catalog_user_state),
                  "user-state v1 prefix size changed");
    static_assert(offsetof(fly_source_status, source_uuid) == 2u * sizeof(std::uint32_t),
                  "source-status UUID offset changed");
    static_assert(FLY_SOURCE_STATUS_V1_SIZE == sizeof(fly_source_status),
                  "source-status v1 prefix size changed");
    static_assert(FLY_CANONICAL_ID_MAX_UTF8_BYTES == 4096u, "canonical-id byte limit changed");
}

void test_unknown_id_reads_empty_and_rejects_invalid_args()
{
    TempRoot root;
    fly_app_t* app = make_app(root.utf8());
    const char* id = "game:UNKNOWNCANONICAL";
    fly_catalog_user_state state = get_user(app, id);
    check(state.favorite == 0u && state.play_count == 0u && state.favorite_revision == 0u &&
              state.last_played_sequence == 0u,
          "unknown canonical ID reads as the empty user record");

    fly_catalog_user_state before = empty_user_state();
    before.favorite = 7u;
    fly_catalog_user_state short_state = before;
    short_state.struct_size = FLY_CATALOG_USER_STATE_V1_SIZE - 1u;
    check(fly_catalog_user_state_get(app, id, static_cast<std::uint32_t>(std::strlen(id)),
                                     &short_state) == FLY_RESULT_STRUCT_TOO_SMALL,
          "user-state get rejects a short output");
    check(short_state.favorite == 7u, "short user-state rejection does not write output");

    check(fly_catalog_user_state_get(nullptr, id, 4u, &state) == FLY_RESULT_INVALID_ARGUMENT,
          "user-state get rejects a null app");
    check(fly_catalog_favorite_set(app, id, static_cast<std::uint32_t>(std::strlen(id)), 2u) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "favorite set rejects values other than 0 or 1");
    check(get_user(app, id).favorite == 0u, "invalid favorite set does not create a row");
    fly_app_destroy(app);
}

void test_favorite_and_played_persist()
{
    TempRoot root;
    const std::string data_root = root.utf8();
    const char* id = "game:1E095A30CCAEC2D5D6C438538C1106DC7B8FD8D5631C5FAB156E00F579E5E06B";
    const std::uint32_t id_len = static_cast<std::uint32_t>(std::strlen(id));

    fly_app_t* app = make_app(data_root);
    check(fly_catalog_favorite_set(app, id, id_len, 1u) == FLY_RESULT_OK, "favorite set succeeds");
    fly_catalog_user_state first = get_user(app, id);
    check(first.favorite == 1u && first.favorite_revision == 1u,
          "first favorite assignment uses revision 1");
    check(fly_catalog_favorite_set(app, id, id_len, 1u) == FLY_RESULT_OK,
          "repeat favorite set still advances revision");
    fly_catalog_user_state second = get_user(app, id);
    check(second.favorite == 1u && second.favorite_revision == 2u,
          "favorite revision is monotonic even when the boolean is unchanged");
    check(fly_catalog_mark_played(app, id, id_len) == FLY_RESULT_OK, "mark played succeeds");
    check(fly_catalog_mark_played(app, id, id_len) == FLY_RESULT_OK, "second mark played succeeds");
    fly_catalog_user_state played = get_user(app, id);
    check(played.play_count == 2u && played.last_played_sequence == 2u,
          "each play increments count and last-played sequence");
    check(played.favorite == 1u && played.favorite_revision == 2u,
          "mark played does not clear favorite state");
    fly_app_destroy(app);

    app = make_app(data_root);
    fly_catalog_user_state restored = get_user(app, id);
    check(restored.favorite == 1u && restored.favorite_revision == 2u &&
              restored.play_count == 2u && restored.last_played_sequence == 2u,
          "favorites and recents round-trip through FLYCAT01");
    check(fly_catalog_favorite_set(app, id, id_len, 0u) == FLY_RESULT_OK, "favorite clear succeeds");
    check(get_user(app, id).favorite == 0u && get_user(app, id).favorite_revision == 3u,
          "clearing favorite still advances revision");
    fly_app_destroy(app);

    app = make_app(data_root);
    fly_catalog_user_state cleared = get_user(app, id);
    check(cleared.favorite == 0u && cleared.favorite_revision == 3u && cleared.play_count == 2u,
          "cleared favorite persists without dropping play history");
    fly_app_destroy(app);
}

void test_source_status_has_no_locator_and_survives_reload()
{
    TempRoot root;
    const std::string data_root = root.utf8();
    fly_app_t* app = make_app(data_root);
    std::uint64_t count = 99u;
    check(fly_source_status_count(app, &count) == FLY_RESULT_OK && count == 0u,
          "new app has an empty source registry");

    fly_scan_t* empty = begin_scan(app);
    check(fly_scan_commit(empty, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "empty FULL scan commits a pinned source");
    fly_scan_abort(empty);

    check(fly_source_status_count(app, &count) == FLY_RESULT_OK && count == 1u,
          "empty FULL scan publishes one source-status row");
    fly_source_status status{};
    status.struct_size = FLY_SOURCE_STATUS_V1_SIZE;
    status.version = FLY_SOURCE_STATUS_VERSION_1;
    check(fly_source_status_get(app, 0u, &status) == FLY_RESULT_OK, "source status get succeeds");
    check(status.source_uuid[15] == 1u, "source status exposes UUID bytes");
    check(status.source_scope == FLY_SOURCE_SCOPE_USER_DIRECTORY,
          "source status exposes portable scope");
    check(status.last_completeness == FLY_SCAN_COMPLETENESS_FULL,
          "source status records last completeness");
    check(status.freshness == FLY_CATALOG_FRESHNESS_FRESH,
          "source with no stale rows is fresh");
    fly_source_status before = status;
    check(fly_source_status_get(app, 1u, &status) == FLY_RESULT_OUT_OF_RANGE,
          "source status get rejects an out-of-range index");
    check(std::memcmp(&status, &before, sizeof(status)) == 0,
          "out-of-range source status does not write output");
    fly_app_destroy(app);

    app = make_app(data_root);
    check(fly_source_status_count(app, &count) == FLY_RESULT_OK && count == 1u,
          "source registry round-trips through FLYCAT01");
    status = {};
    status.struct_size = FLY_SOURCE_STATUS_V1_SIZE;
    status.version = FLY_SOURCE_STATUS_VERSION_1;
    check(fly_source_status_get(app, 0u, &status) == FLY_RESULT_OK &&
              status.source_scope == FLY_SOURCE_SCOPE_USER_DIRECTORY &&
              status.last_completeness == FLY_SCAN_COMPLETENESS_FULL,
          "reloaded source status keeps UUID scope and completeness without a URI");
    fly_app_destroy(app);
}

} // namespace

int main()
{
    test_abi_layout();
    test_unknown_id_reads_empty_and_rejects_invalid_args();
    test_favorite_and_played_persist();
    test_source_status_has_no_locator_and_survives_reload();
    if (failures == 0)
    {
        std::puts("flynes_catalog_user_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
