#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <flynes/flynes_app.h>

#include <algorithm>
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
                    ("flynes-source-remove-" + std::to_string(temp_sequence.fetch_add(1u)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error))
            {
                break;
            }
            path_.clear();
        }
        check(!path_.empty(), "source-remove test creates an isolated data root");
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

class TempFile final
{
public:
    explicit TempFile(const std::vector<std::uint8_t>& bytes)
    {
        for (unsigned int attempt = 0u; attempt < 100u; ++attempt)
        {
            path_ = std::filesystem::temp_directory_path() /
                    ("flynes-source-remove-" + std::to_string(temp_sequence.fetch_add(1u)) +
                     ".bin");
#if defined(_WIN32)
            fd_ = _open(path_.string().c_str(),
                        _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                        _S_IREAD | _S_IWRITE);
#else
            fd_ = open(path_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
#endif
            if (fd_ >= 0)
            {
                break;
            }
        }
        check(fd_ >= 0, "source-remove test creates a temporary descriptor");
        if (fd_ >= 0)
        {
            std::size_t written = 0u;
            while (written < bytes.size())
            {
#if defined(_WIN32)
                const int amount = _write(
                    fd_, bytes.data() + written,
                    static_cast<unsigned int>(
                        std::min<std::size_t>(bytes.size() - written, 1u << 20)));
#else
                const ssize_t amount =
                    write(fd_, bytes.data() + written, bytes.size() - written);
#endif
                check(amount > 0, "source-remove test writes temporary bytes");
                if (amount <= 0)
                {
                    break;
                }
                written += static_cast<std::size_t>(amount);
            }
        }
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    ~TempFile()
    {
        if (fd_ >= 0)
        {
#if defined(_WIN32)
            static_cast<void>(_close(fd_));
#else
            static_cast<void>(close(fd_));
#endif
            fd_ = -1;
        }
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    int fd() const noexcept { return fd_; }

private:
    std::filesystem::path path_;
    int fd_ = -1;
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
    check(fly_app_create(&config, &app) == FLY_RESULT_OK, "source-remove test creates app");
    return app;
}

std::array<std::uint8_t, 16> source_uuid(std::uint8_t last)
{
    std::array<std::uint8_t, 16> value{};
    value[15] = last;
    return value;
}

fly_scan_t* begin_scan(fly_app_t* app, std::uint8_t uuid_last, std::uint32_t scope)
{
    fly_scan_config config{};
    config.struct_size = FLY_SCAN_CONFIG_V1_SIZE;
    config.version = FLY_SCAN_CONFIG_VERSION_1;
    const auto uuid = source_uuid(uuid_last);
    std::copy(uuid.begin(), uuid.end(), config.source_uuid);
    config.source_scope = scope;
    fly_scan_t* scan = nullptr;
    check(fly_scan_begin(app, &config, &scan) == FLY_RESULT_OK, "source-remove test begins scan");
    return scan;
}

std::vector<std::uint8_t> make_nes(std::uint8_t seed)
{
    std::vector<std::uint8_t> bytes(16u + 16u * 1024u, 0u);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1Au;
    bytes[4] = 1u;
    for (std::size_t index = 0; index < 16u * 1024u; ++index)
    {
        bytes[16u + index] = static_cast<std::uint8_t>((index * 31u + seed) & 0xFFu);
    }
    return bytes;
}

fly_scan_file make_scan_file(const char* path, const char* display, int fd)
{
    fly_scan_file value{};
    value.struct_size = FLY_SCAN_FILE_V1_SIZE;
    value.version = FLY_SCAN_FILE_VERSION_1;
    value.source_relative_path_utf8 = path;
    value.display_name_utf8 = display;
    value.source_relative_path_utf8_length = static_cast<std::uint32_t>(std::strlen(path));
    value.display_name_utf8_length = static_cast<std::uint32_t>(std::strlen(display));
    value.borrowed_fd = fd;
    value.declared_size = 0u;
    value.modified_time_hint_ns = INT64_C(123456789);
    return value;
}

void add_indexed(fly_scan_t* scan, const fly_scan_file& file)
{
    fly_scan_file_result result{};
    result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE;
    result.version = FLY_SCAN_FILE_RESULT_VERSION_1;
    check(fly_scan_add_file(scan, &file, &result) == FLY_RESULT_OK, "scan add succeeds");
    check(result.outcome == FLY_SCAN_FILE_OUTCOME_INDEXED, "scan add indexes ROM");
}

std::uint64_t source_count(const fly_app_t* app)
{
    std::uint64_t count = UINT64_MAX;
    check(fly_source_status_count(app, &count) == FLY_RESULT_OK, "source status count succeeds");
    return count;
}

std::uint64_t catalog_generation(const fly_app_t* app)
{
    fly_catalog_snapshot_t* snapshot = nullptr;
    check(fly_catalog_snapshot(app, &snapshot) == FLY_RESULT_OK, "snapshot acquired");
    std::uint64_t generation = UINT64_MAX;
    check(fly_catalog_snapshot_generation(snapshot, &generation) == FLY_RESULT_OK,
          "snapshot generation queried");
    fly_catalog_snapshot_release(snapshot);
    return generation;
}

std::uint64_t catalog_count(const fly_app_t* app)
{
    fly_catalog_snapshot_t* snapshot = nullptr;
    check(fly_catalog_snapshot(app, &snapshot) == FLY_RESULT_OK, "snapshot acquired");
    std::uint64_t count = UINT64_MAX;
    check(fly_catalog_snapshot_count(snapshot, &count) == FLY_RESULT_OK, "snapshot count queried");
    fly_catalog_snapshot_release(snapshot);
    return count;
}

std::string first_canonical_id(const fly_app_t* app)
{
    fly_catalog_snapshot_t* snapshot = nullptr;
    check(fly_catalog_snapshot(app, &snapshot) == FLY_RESULT_OK, "snapshot acquired");
    std::uint64_t count = 0u;
    check(fly_catalog_snapshot_count(snapshot, &count) == FLY_RESULT_OK, "snapshot count queried");
    check(count >= 1u, "catalog has at least one entry");
    fly_catalog_entry query{};
    query.struct_size = FLY_CATALOG_ENTRY_V1_SIZE;
    query.version = FLY_CATALOG_ENTRY_VERSION_1;
    check(fly_catalog_snapshot_get(snapshot, 0u, &query) == FLY_RESULT_BUFFER_TOO_SMALL,
          "entry size query succeeds");
    std::string canonical(query.canonical_id_required, '\0');
    std::string variant(query.variant_id_required, '\0');
    std::string display(query.display_name_required, '\0');
    std::string relative(query.source_relative_path_required, '\0');
    fly_catalog_entry entry{};
    entry.struct_size = FLY_CATALOG_ENTRY_V1_SIZE;
    entry.version = FLY_CATALOG_ENTRY_VERSION_1;
    entry.canonical_id_utf8 = canonical.data();
    entry.canonical_id_capacity = static_cast<std::uint32_t>(canonical.size());
    entry.variant_id_utf8 = variant.data();
    entry.variant_id_capacity = static_cast<std::uint32_t>(variant.size());
    entry.display_name_utf8 = display.data();
    entry.display_name_capacity = static_cast<std::uint32_t>(display.size());
    entry.source_relative_path_utf8 = relative.data();
    entry.source_relative_path_capacity = static_cast<std::uint32_t>(relative.size());
    check(fly_catalog_snapshot_get(snapshot, 0u, &entry) == FLY_RESULT_OK, "entry read succeeds");
    fly_catalog_snapshot_release(snapshot);
    return std::string(canonical.c_str());
}

fly_catalog_user_state get_user(const fly_app_t* app, const std::string& canonical_id)
{
    fly_catalog_user_state value{};
    value.struct_size = FLY_CATALOG_USER_STATE_V1_SIZE;
    value.version = FLY_CATALOG_USER_STATE_VERSION_1;
    check(fly_catalog_user_state_get(app,
                                     canonical_id.c_str(),
                                     static_cast<std::uint32_t>(canonical_id.size()),
                                     &value) == FLY_RESULT_OK,
          "user-state get succeeds");
    return value;
}

void test_abi_values()
{
    static_assert(FLY_RESULT_NOT_FOUND == -10, "NOT_FOUND append value changed");
    static_assert(FLY_RESULT_FORBIDDEN == -11, "FORBIDDEN append value changed");
}

void test_remove_missing_source_returns_not_found()
{
    TempRoot root;
    fly_app_t* app = make_app(root.utf8());
    const auto uuid = source_uuid(9u);
    check(fly_source_remove(app, uuid.data(), FLY_SOURCE_SCOPE_USER_DIRECTORY) ==
              FLY_RESULT_NOT_FOUND,
          "removing an unknown source returns NOT_FOUND");
    check(source_count(app) == 0u, "failed remove does not invent a source row");
    fly_app_destroy(app);
}

void test_remove_builtin_is_forbidden()
{
    TempRoot root;
    fly_app_t* app = make_app(root.utf8());
    TempFile rom(make_nes(3u));
    fly_scan_t* scan = begin_scan(app, 2u, FLY_SOURCE_SCOPE_BUILTIN);
    add_indexed(scan, make_scan_file("builtin.nes", "Builtin.nes", rom.fd()));
    check(fly_scan_commit(scan, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "builtin source commits");
    fly_scan_abort(scan);

    const auto uuid = source_uuid(2u);
    const std::uint64_t before = catalog_generation(app);
    check(fly_source_remove(app, uuid.data(), FLY_SOURCE_SCOPE_BUILTIN) == FLY_RESULT_FORBIDDEN,
          "removing a builtin source is forbidden");
    check(source_count(app) == 1u, "forbidden remove leaves the builtin source");
    check(catalog_count(app) == 1u, "forbidden remove leaves builtin entries");
    check(catalog_generation(app) == before, "forbidden remove does not bump generation");
    fly_app_destroy(app);
}

void test_remove_user_directory_drops_source_and_entries_keeps_others()
{
    TempRoot root;
    fly_app_t* app = make_app(root.utf8());

    TempFile rom_one(make_nes(11u));
    fly_scan_t* scan_one = begin_scan(app, 1u, FLY_SOURCE_SCOPE_USER_DIRECTORY);
    add_indexed(scan_one, make_scan_file("one.nes", "One.nes", rom_one.fd()));
    check(fly_scan_commit(scan_one, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "first source commits");
    fly_scan_abort(scan_one);
    const std::string kept_id = first_canonical_id(app);
    check(fly_catalog_favorite_set(app, kept_id.c_str(),
                                   static_cast<std::uint32_t>(kept_id.size()), 1u) ==
              FLY_RESULT_OK,
          "favorite for surviving game succeeds");
    check(fly_catalog_mark_played(app, kept_id.c_str(),
                                  static_cast<std::uint32_t>(kept_id.size())) == FLY_RESULT_OK,
          "mark played for surviving game succeeds");

    TempFile rom_two(make_nes(22u));
    fly_scan_t* scan_two = begin_scan(app, 2u, FLY_SOURCE_SCOPE_USER_DIRECTORY);
    add_indexed(scan_two, make_scan_file("two.nes", "Two.nes", rom_two.fd()));
    check(fly_scan_commit(scan_two, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "second source commits");
    fly_scan_abort(scan_two);

    check(source_count(app) == 2u, "two sources are registered");
    check(catalog_count(app) == 2u, "two catalog entries are present");
    const std::uint64_t before = catalog_generation(app);

    const auto remove_uuid = source_uuid(2u);
    check(fly_source_remove(app, remove_uuid.data(), FLY_SOURCE_SCOPE_USER_DIRECTORY) ==
              FLY_RESULT_OK,
          "removing a user-directory source succeeds");
    check(source_count(app) == 1u, "removed source disappears from the registry");
    check(catalog_count(app) == 1u, "entries belonging to the removed source disappear");
    check(catalog_generation(app) == before + 1u, "successful remove bumps generation");

    const fly_catalog_user_state user = get_user(app, kept_id);
    check(user.favorite == 1u && user.play_count == 1u && user.last_played_sequence == 1u,
          "user state for surviving games is preserved");

    fly_source_status status{};
    status.struct_size = FLY_SOURCE_STATUS_V1_SIZE;
    status.version = FLY_SOURCE_STATUS_VERSION_1;
    check(fly_source_status_get(app, 0u, &status) == FLY_RESULT_OK, "remaining source is readable");
    check(status.source_uuid[15] == 1u, "remaining source is the untouched UUID");
    check(status.source_scope == FLY_SOURCE_SCOPE_USER_DIRECTORY,
          "remaining source keeps its scope");
    fly_app_destroy(app);
}

void test_remove_rejects_null_and_zero_uuid()
{
    TempRoot root;
    fly_app_t* app = make_app(root.utf8());
    const auto uuid = source_uuid(1u);
    check(fly_source_remove(nullptr, uuid.data(), FLY_SOURCE_SCOPE_USER_DIRECTORY) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "remove rejects a null app");
    check(fly_source_remove(app, nullptr, FLY_SOURCE_SCOPE_USER_DIRECTORY) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "remove rejects a null UUID");
    std::array<std::uint8_t, 16> zero{};
    check(fly_source_remove(app, zero.data(), FLY_SOURCE_SCOPE_USER_DIRECTORY) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "remove rejects an all-zero UUID");
    check(fly_source_remove(app, uuid.data(), 0u) == FLY_RESULT_INVALID_ARGUMENT,
          "remove rejects an unknown scope");
    fly_app_destroy(app);
}

} // namespace

int main()
{
    test_abi_values();
    test_remove_rejects_null_and_zero_uuid();
    test_remove_missing_source_returns_not_found();
    test_remove_builtin_is_forbidden();
    test_remove_user_directory_drops_source_and_entries_keeps_others();
    if (failures == 0)
    {
        std::puts("flynes_catalog_source_remove_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
