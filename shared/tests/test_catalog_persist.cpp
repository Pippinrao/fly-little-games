#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <flynes/flynes_app.h>

#include "catalog/content_identity.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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

std::vector<std::uint8_t> make_nes()
{
    std::vector<std::uint8_t> bytes(16u + 16u * 1024u, 0u);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1Au;
    bytes[4] = 1u;
    for (std::size_t index = 0; index < 16u * 1024u; ++index)
    {
        bytes[16u + index] = static_cast<std::uint8_t>((index * 31u + 7u) & 0xFFu);
    }
    return bytes;
}

int open_temp(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return _open(path.string().c_str(),
                 _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                 _S_IREAD | _S_IWRITE);
#else
    return open(path.string().c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
#endif
}

int write_fd(int fd, const std::uint8_t* bytes, std::size_t size)
{
    std::size_t written = 0u;
    while (written < size)
    {
#if defined(_WIN32)
        const int amount = _write(fd, bytes + written, static_cast<unsigned int>(size - written));
#else
        const ssize_t amount = write(fd, bytes + written, size - written);
#endif
        if (amount <= 0)
        {
            return -1;
        }
        written += static_cast<std::size_t>(amount);
    }
    return 0;
}

void close_fd(int fd)
{
#if defined(_WIN32)
    static_cast<void>(_close(fd));
#else
    static_cast<void>(close(fd));
#endif
}

class TempFile final
{
public:
    explicit TempFile(const std::vector<std::uint8_t>& bytes)
    {
        for (unsigned int attempt = 0u; attempt < 100u; ++attempt)
        {
            path_ = std::filesystem::temp_directory_path() /
                    ("flynes-persist-rom-" + std::to_string(temp_sequence.fetch_add(1u)) +
                     ".bin");
            fd_ = open_temp(path_);
            if (fd_ >= 0)
            {
                break;
            }
        }
        check(fd_ >= 0, "persist test creates a ROM descriptor");
        if (fd_ >= 0)
        {
            check(write_fd(fd_, bytes.data(), bytes.size()) == 0, "persist test writes ROM bytes");
        }
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    ~TempFile()
    {
        if (fd_ >= 0)
        {
            close_fd(fd_);
        }
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    int fd() const noexcept { return fd_; }

private:
    std::filesystem::path path_;
    int fd_ = -1;
};

class TempRoot final
{
public:
    TempRoot()
    {
        for (unsigned int attempt = 0u; attempt < 100u; ++attempt)
        {
            path_ = std::filesystem::temp_directory_path() /
                    ("flynes-persist-" + std::to_string(temp_sequence.fetch_add(1u)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error))
            {
                break;
            }
            path_.clear();
        }
        check(!path_.empty(), "persist test creates an isolated data root");
    }

    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;

    ~TempRoot()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    std::string utf8() const
    {
        return path_.u8string();
    }

    std::filesystem::path catalog_path() const
    {
        return path_ / "catalog.flycat01";
    }

    std::filesystem::path tmp_path() const
    {
        return path_ / "catalog.flycat01.tmp";
    }

    const std::filesystem::path& path() const { return path_; }

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
    check(fly_app_create(&config, &app) == FLY_RESULT_OK, "persist test creates app");
    check(app != nullptr, "persist test create returns a handle");
    return app;
}

fly_scan_config make_scan_config(std::uint32_t scope = FLY_SOURCE_SCOPE_USER_DIRECTORY)
{
    fly_scan_config value{};
    value.struct_size = FLY_SCAN_CONFIG_V1_SIZE;
    value.version = FLY_SCAN_CONFIG_VERSION_1;
    value.source_uuid[15] = 1u;
    value.source_scope = scope;
    return value;
}

fly_scan_t* begin_scan(fly_app_t* app, std::uint32_t scope = FLY_SOURCE_SCOPE_USER_DIRECTORY)
{
    fly_scan_config config = make_scan_config(scope);
    fly_scan_t* scan = nullptr;
    check(fly_scan_begin(app, &config, &scan) == FLY_RESULT_OK, "persist test begins scan");
    return scan;
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
    return value;
}

fly_catalog_snapshot_t* snapshot_of(const fly_app_t* app)
{
    fly_catalog_snapshot_t* snapshot = nullptr;
    check(fly_catalog_snapshot(app, &snapshot) == FLY_RESULT_OK, "persist test snapshots");
    return snapshot;
}

std::uint64_t snapshot_generation(const fly_catalog_snapshot_t* snapshot)
{
    std::uint64_t value = UINT64_MAX;
    check(fly_catalog_snapshot_generation(snapshot, &value) == FLY_RESULT_OK,
          "persist test reads generation");
    return value;
}

std::uint64_t snapshot_count(const fly_catalog_snapshot_t* snapshot)
{
    std::uint64_t value = UINT64_MAX;
    check(fly_catalog_snapshot_count(snapshot, &value) == FLY_RESULT_OK,
          "persist test reads count");
    return value;
}

struct EntryValue final
{
    fly_catalog_entry entry{};
    std::string canonical_id;
    std::string variant_id;
    std::string display_name;
    std::string source_relative_path;
};

EntryValue read_entry(const fly_catalog_snapshot_t* snapshot)
{
    fly_catalog_entry query{};
    query.struct_size = FLY_CATALOG_ENTRY_V1_SIZE;
    query.version = FLY_CATALOG_ENTRY_VERSION_1;
    check(fly_catalog_snapshot_get(snapshot, 0u, &query) == FLY_RESULT_BUFFER_TOO_SMALL,
          "persist test queries entry sizes");
    EntryValue value;
    value.canonical_id.resize(query.canonical_id_required);
    value.variant_id.resize(query.variant_id_required);
    value.display_name.resize(query.display_name_required);
    value.source_relative_path.resize(query.source_relative_path_required);
    value.entry.struct_size = FLY_CATALOG_ENTRY_V1_SIZE;
    value.entry.version = FLY_CATALOG_ENTRY_VERSION_1;
    value.entry.canonical_id_utf8 = value.canonical_id.data();
    value.entry.canonical_id_capacity = static_cast<std::uint32_t>(value.canonical_id.size());
    value.entry.variant_id_utf8 = value.variant_id.data();
    value.entry.variant_id_capacity = static_cast<std::uint32_t>(value.variant_id.size());
    value.entry.display_name_utf8 = value.display_name.data();
    value.entry.display_name_capacity = static_cast<std::uint32_t>(value.display_name.size());
    value.entry.source_relative_path_utf8 = value.source_relative_path.data();
    value.entry.source_relative_path_capacity =
        static_cast<std::uint32_t>(value.source_relative_path.size());
    check(fly_catalog_snapshot_get(snapshot, 0u, &value.entry) == FLY_RESULT_OK,
          "persist test copies entry");
    value.canonical_id.resize(value.entry.canonical_id_required - 1u);
    value.variant_id.resize(value.entry.variant_id_required - 1u);
    value.display_name.resize(value.entry.display_name_required - 1u);
    value.source_relative_path.resize(value.entry.source_relative_path_required - 1u);
    return value;
}

std::vector<std::uint8_t> read_all(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        check(false, "persist test opens catalog file");
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}

void write_all(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    check(static_cast<bool>(output), "persist test writes catalog file");
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(output), "persist test finishes catalog write");
}

std::array<std::uint8_t, 32> sha256_of(const std::uint8_t* bytes, std::size_t size)
{
    const auto hex = flynes::catalog::sha256_hex(flynes::catalog::ContentBytes{bytes, size});
    check(hex.ok() && hex.value.size() == 64u, "persist test hashes catalog bytes");
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t index = 0; index < 32u; ++index)
    {
        const auto nibble = [](char value) -> int {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            return -1;
        };
        const int high = nibble(hex.value[index * 2u]);
        const int low = nibble(hex.value[index * 2u + 1u]);
        check(high >= 0 && low >= 0, "persist test decodes SHA-256 hex");
        digest[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return digest;
}

void commit_indexed_rom(fly_app_t* app)
{
    TempFile file(make_nes());
    fly_scan_t* scan = begin_scan(app);
    fly_scan_file candidate = make_scan_file("games/alpha.nes", "Alpha.nes", file.fd());
    fly_scan_file_result result{};
    result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE;
    result.version = FLY_SCAN_FILE_RESULT_VERSION_1;
    check(fly_scan_add_file(scan, &candidate, &result) == FLY_RESULT_OK &&
              result.outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "persist test indexes a ROM");
    check(fly_scan_commit(scan, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "persist test commits a FULL scan");
    fly_scan_abort(scan);
}

void test_round_trip_after_destroy_create()
{
    TempRoot root;
    const std::string data_root = root.utf8();
    fly_app_t* app = make_app(data_root);
    commit_indexed_rom(app);
    fly_catalog_snapshot_t* first = snapshot_of(app);
    check(snapshot_generation(first) == 1u && snapshot_count(first) == 1u,
          "committed catalog is generation one with one row");
    const EntryValue original = read_entry(first);
    fly_catalog_snapshot_release(first);
    fly_app_destroy(app);

    check(std::filesystem::exists(root.catalog_path()),
          "successful commit writes catalog.flycat01");
    check(!std::filesystem::exists(root.tmp_path()),
          "successful commit does not leave catalog.flycat01.tmp");
    const std::vector<std::uint8_t> encoded = read_all(root.catalog_path());
    check(encoded.size() >= 8u + 4u + 32u, "FLYCAT01 file has header, payload, and checksum");
    if (encoded.size() >= 12u)
    {
        check(std::memcmp(encoded.data(), "FLYCAT01", 8) == 0, "catalog magic is FLYCAT01");
        check(encoded[8] == 1u && encoded[9] == 0u && encoded[10] == 0u && encoded[11] == 0u,
              "catalog version is little-endian 1");
    }
    if (encoded.size() >= 32u)
    {
        const std::array<std::uint8_t, 32> digest =
            sha256_of(encoded.data(), encoded.size() - 32u);
        check(std::equal(digest.begin(), digest.end(), encoded.end() - 32),
              "trailer is SHA-256 of every preceding byte");
    }

    app = make_app(data_root);
    fly_catalog_snapshot_t* restored = snapshot_of(app);
    check(snapshot_generation(restored) == 1u && snapshot_count(restored) == 1u,
          "create loads the persisted generation and row");
    const EntryValue loaded = read_entry(restored);
    check(loaded.canonical_id == original.canonical_id, "canonical ID round-trips");
    check(loaded.variant_id == original.variant_id, "variant ID round-trips");
    check(loaded.display_name == original.display_name, "display name round-trips");
    check(loaded.source_relative_path == original.source_relative_path,
          "relative path round-trips");
    check(loaded.entry.payload_size == original.entry.payload_size &&
              loaded.entry.physical_size == original.entry.physical_size &&
              loaded.entry.rom_format == original.entry.rom_format &&
              loaded.entry.package_format == original.entry.package_format &&
              loaded.entry.freshness == original.entry.freshness &&
              loaded.entry.source_scope == original.entry.source_scope,
          "numeric catalog fields round-trip");
    check(std::memcmp(loaded.entry.source_uuid, original.entry.source_uuid, 16) == 0 &&
              std::memcmp(loaded.entry.payload_sha256, original.entry.payload_sha256, 32) == 0 &&
              std::memcmp(loaded.entry.physical_sha256, original.entry.physical_sha256, 32) == 0,
          "identity hashes round-trip");
    fly_catalog_snapshot_release(restored);
    fly_app_destroy(app);
}

void test_corrupt_checksum_does_not_clobber()
{
    TempRoot root;
    const std::string data_root = root.utf8();
    fly_app_t* app = make_app(data_root);
    commit_indexed_rom(app);
    fly_app_destroy(app);

    std::vector<std::uint8_t> encoded = read_all(root.catalog_path());
    check(!encoded.empty(), "corrupt test has a persisted file");
    if (encoded.empty())
    {
        return;
    }
    encoded.back() = static_cast<std::uint8_t>(encoded.back() ^ 0xFFu);
    write_all(root.catalog_path(), encoded);
    const std::vector<std::uint8_t> before = encoded;

    app = make_app(data_root);
    check(app != nullptr, "corrupt checksum still creates an app");
    fly_catalog_snapshot_t* snapshot = snapshot_of(app);
    check(snapshot_generation(snapshot) == 0u && snapshot_count(snapshot) == 0u,
          "checksum mismatch loads empty generation zero");
    fly_catalog_snapshot_release(snapshot);
    fly_app_destroy(app);

    const std::vector<std::uint8_t> after = read_all(root.catalog_path());
    check(after == before, "create does not overwrite a corrupt catalog file");
}

void test_truncated_and_unknown_version_do_not_clobber()
{
    TempRoot truncated_root;
    write_all(truncated_root.catalog_path(),
              std::vector<std::uint8_t>{'F', 'L', 'Y', 'C', 'A', 'T', '0', '1'});
    const std::vector<std::uint8_t> truncated_before = read_all(truncated_root.catalog_path());
    fly_app_t* app = make_app(truncated_root.utf8());
    fly_catalog_snapshot_t* snapshot = snapshot_of(app);
    check(snapshot_generation(snapshot) == 0u && snapshot_count(snapshot) == 0u,
          "truncated catalog loads empty generation zero");
    fly_catalog_snapshot_release(snapshot);
    fly_app_destroy(app);
    check(read_all(truncated_root.catalog_path()) == truncated_before,
          "create does not repair a truncated catalog in place");

    TempRoot version_root;
    std::vector<std::uint8_t> unknown{'F', 'L', 'Y', 'C', 'A', 'T', '0', '1', 2, 0, 0, 0};
    const auto digest = sha256_of(unknown.data(), unknown.size());
    unknown.insert(unknown.end(), digest.begin(), digest.end());
    write_all(version_root.catalog_path(), unknown);
    const std::vector<std::uint8_t> unknown_before = unknown;
    app = make_app(version_root.utf8());
    snapshot = snapshot_of(app);
    check(snapshot_generation(snapshot) == 0u && snapshot_count(snapshot) == 0u,
          "unknown version loads empty generation zero");
    fly_catalog_snapshot_release(snapshot);
    fly_app_destroy(app);
    check(read_all(version_root.catalog_path()) == unknown_before,
          "create does not overwrite an unknown-version catalog");
}

void test_atomic_replace_failed_tmp_keeps_live_file()
{
    TempRoot root;
    const std::string data_root = root.utf8();
    fly_app_t* app = make_app(data_root);
    commit_indexed_rom(app);
    fly_app_destroy(app);

    const std::vector<std::uint8_t> live_before = read_all(root.catalog_path());
    if (live_before.empty())
    {
        return;
    }
    std::error_code error;
    std::filesystem::create_directory(root.tmp_path(), error);
    check(!error, "atomic test turns the tmp path into a directory");

    app = make_app(data_root);
    fly_scan_t* scan = begin_scan(app);
    const fly_result commit = fly_scan_commit(scan, FLY_SCAN_COMPLETENESS_FULL);
    check(commit != FLY_RESULT_OK, "commit fails when the tmp path cannot be written");
    fly_scan_abort(scan);
    fly_app_destroy(app);

    check(std::filesystem::is_regular_file(root.catalog_path()),
          "failed replace leaves the live catalog file");
    check(read_all(root.catalog_path()) == live_before,
          "failed replace does not truncate a good catalog");

    std::filesystem::remove_all(root.tmp_path(), error);
    app = make_app(data_root);
    fly_catalog_snapshot_t* snapshot = snapshot_of(app);
    check(snapshot_generation(snapshot) == 1u && snapshot_count(snapshot) == 1u,
          "live catalog remains loadable after a failed replace");
    fly_catalog_snapshot_release(snapshot);
    fly_app_destroy(app);
}

void test_empty_full_scan_persists_pinned_scope()
{
    TempRoot root;
    const std::string data_root = root.utf8();
    fly_app_t* app = make_app(data_root);
    fly_scan_t* empty = begin_scan(app);
    check(fly_scan_commit(empty, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "empty FULL scan commits");
    fly_scan_abort(empty);
    fly_catalog_snapshot_t* snapshot = snapshot_of(app);
    check(snapshot_generation(snapshot) == 1u && snapshot_count(snapshot) == 0u,
          "empty FULL scan publishes generation one with zero rows");
    fly_catalog_snapshot_release(snapshot);
    fly_app_destroy(app);

    check(std::filesystem::is_regular_file(root.catalog_path()),
          "empty FULL scan still writes catalog.flycat01");

    app = make_app(data_root);
    snapshot = snapshot_of(app);
    check(snapshot_generation(snapshot) == 1u && snapshot_count(snapshot) == 0u,
          "reloaded empty FULL scan keeps generation and zero rows");
    fly_catalog_snapshot_release(snapshot);

    fly_scan_config changed = make_scan_config(FLY_SOURCE_SCOPE_USER_FILE);
    fly_scan_t* forbidden = reinterpret_cast<fly_scan_t*>(static_cast<std::uintptr_t>(1u));
    check(fly_scan_begin(app, &changed, &forbidden) == FLY_RESULT_CONFLICT &&
              forbidden == nullptr,
          "reloaded empty FULL scan still pins the source UUID scope");
    fly_scan_t* same = begin_scan(app, FLY_SOURCE_SCOPE_USER_DIRECTORY);
    check(same != nullptr, "same scope may begin after reloading a pinned empty source");
    fly_scan_abort(same);
    fly_app_destroy(app);
}

} // namespace

int main()
{
    test_round_trip_after_destroy_create();
    test_corrupt_checksum_does_not_clobber();
    test_truncated_and_unknown_version_do_not_clobber();
    test_atomic_replace_failed_tmp_keeps_live_file();
    test_empty_full_scan_persists_pinned_scope();
    if (failures == 0)
    {
        std::puts("flynes_catalog_persist_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
