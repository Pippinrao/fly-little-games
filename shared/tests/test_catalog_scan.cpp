#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <flynes/flynes_app.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
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

struct ScanDataRoots final
{
    std::vector<std::filesystem::path> paths;
    ~ScanDataRoots()
    {
        std::error_code ignored;
        for (const std::filesystem::path& path : paths)
        {
            std::filesystem::remove_all(path, ignored);
        }
    }
};

ScanDataRoots scan_data_roots;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16u));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24u));
}

std::uint32_t crc32_of(const std::vector<std::uint8_t>& bytes)
{
    std::uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (const std::uint8_t byte : bytes)
    {
        crc ^= byte;
        for (unsigned int bit = 0; bit < 8u; ++bit)
        {
            crc = (crc >> 1u) ^ ((crc & 1u) != 0u ? UINT32_C(0xEDB88320) : 0u);
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

std::vector<std::uint8_t> make_nes(std::uint8_t seed = 7u, bool nes2 = false)
{
    std::vector<std::uint8_t> bytes(16u + 16u * 1024u, 0u);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1Au;
    bytes[4] = 1u;
    bytes[7] = nes2 ? 0x08u : 0u;
    for (std::size_t index = 0; index < 16u * 1024u; ++index)
    {
        bytes[16u + index] = static_cast<std::uint8_t>((index * 31u + seed) & 0xFFu);
    }
    return bytes;
}

std::vector<std::uint8_t> make_fds()
{
    std::vector<std::uint8_t> bytes(16u + 65'500u, 0u);
    const std::array<std::uint8_t, 4> header = {'F', 'D', 'S', 0x1Au};
    std::copy(header.begin(), header.end(), bytes.begin());
    bytes[4] = 1u;
    const std::array<std::uint8_t, 15> side = {
        0x01u, '*', 'N', 'I', 'N', 'T', 'E', 'N', 'D', 'O', '-', 'H', 'V', 'C', '*'};
    std::copy(side.begin(), side.end(), bytes.begin() + 16);
    return bytes;
}

std::vector<std::uint8_t> make_unif()
{
    std::vector<std::uint8_t> bytes(41u, 0u);
    bytes[0] = 'U';
    bytes[1] = 'N';
    bytes[2] = 'I';
    bytes[3] = 'F';
    bytes[32] = 'P';
    bytes[33] = 'R';
    bytes[34] = 'G';
    bytes[35] = '0';
    bytes[36] = 1u;
    bytes[40] = 0x42u;
    return bytes;
}

std::vector<std::uint8_t> make_stored_zip(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& entries)
{
    struct Central final
    {
        std::string name;
        std::uint32_t crc = 0;
        std::uint32_t size = 0;
        std::uint32_t offset = 0;
    };

    std::vector<std::uint8_t> bytes;
    std::vector<Central> central;
    for (const auto& item : entries)
    {
        Central value;
        value.name = item.first;
        value.crc = crc32_of(item.second);
        value.size = static_cast<std::uint32_t>(item.second.size());
        value.offset = static_cast<std::uint32_t>(bytes.size());
        append_u32(bytes, UINT32_C(0x04034B50));
        append_u16(bytes, 20u);
        append_u16(bytes, 0u);
        append_u16(bytes, 0u);
        append_u16(bytes, 0u);
        append_u16(bytes, 0u);
        append_u32(bytes, value.crc);
        append_u32(bytes, value.size);
        append_u32(bytes, value.size);
        append_u16(bytes, static_cast<std::uint16_t>(value.name.size()));
        append_u16(bytes, 0u);
        bytes.insert(bytes.end(), value.name.begin(), value.name.end());
        bytes.insert(bytes.end(), item.second.begin(), item.second.end());
        central.push_back(value);
    }

    const std::uint32_t central_offset = static_cast<std::uint32_t>(bytes.size());
    for (const Central& value : central)
    {
        append_u32(bytes, UINT32_C(0x02014B50));
        append_u16(bytes, 20u);
        append_u16(bytes, 20u);
        append_u16(bytes, 0u);
        append_u16(bytes, 0u);
        append_u16(bytes, 0u);
        append_u16(bytes, 0u);
        append_u32(bytes, value.crc);
        append_u32(bytes, value.size);
        append_u32(bytes, value.size);
        append_u16(bytes, static_cast<std::uint16_t>(value.name.size()));
        append_u16(bytes, 0u);
        append_u16(bytes, 0u);
        append_u16(bytes, 0u);
        append_u16(bytes, 0u);
        append_u32(bytes, 0u);
        append_u32(bytes, value.offset);
        bytes.insert(bytes.end(), value.name.begin(), value.name.end());
    }
    const std::uint32_t central_size =
        static_cast<std::uint32_t>(bytes.size()) - central_offset;
    append_u32(bytes, UINT32_C(0x06054B50));
    append_u16(bytes, 0u);
    append_u16(bytes, 0u);
    append_u16(bytes, static_cast<std::uint16_t>(central.size()));
    append_u16(bytes, static_cast<std::uint16_t>(central.size()));
    append_u32(bytes, central_size);
    append_u32(bytes, central_offset);
    append_u16(bytes, 0u);
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
        const std::size_t remaining = size - written;
#if defined(_WIN32)
        const unsigned int chunk = static_cast<unsigned int>(
            std::min<std::size_t>(remaining, static_cast<std::size_t>(UINT32_MAX)));
        const int amount = _write(fd, bytes + written, chunk);
#else
        const ssize_t amount = write(fd, bytes + written, remaining);
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

bool fd_is_open(int fd)
{
#if defined(_WIN32)
    return _get_osfhandle(fd) != -1;
#else
    return fcntl(fd, F_GETFD) != -1;
#endif
}

std::int64_t fd_offset(int fd)
{
#if defined(_WIN32)
    return static_cast<std::int64_t>(_lseeki64(fd, 0, SEEK_CUR));
#else
    return static_cast<std::int64_t>(lseek(fd, 0, SEEK_CUR));
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
                    ("flynes-scan-" + std::to_string(temp_sequence.fetch_add(1u)) + ".bin");
            fd_ = open_temp(path_);
            if (fd_ >= 0)
            {
                break;
            }
        }
        check(fd_ >= 0, "test creates a temporary descriptor");
        if (fd_ >= 0)
        {
            check(write_fd(fd_, bytes.data(), bytes.size()) == 0,
                  "test writes all temporary descriptor bytes");
        }
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    ~TempFile()
    {
        close_now();
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    int fd() const noexcept
    {
        return fd_;
    }

    void close_now() noexcept
    {
        if (fd_ >= 0)
        {
            close_fd(fd_);
            fd_ = -1;
        }
    }

private:
    std::filesystem::path path_;
    int fd_ = -1;
};

fly_platform_capabilities make_capabilities()
{
    fly_platform_capabilities value{};
    value.struct_size = FLY_PLATFORM_CAPABILITIES_V1_SIZE;
    value.version = FLY_PLATFORM_CAPABILITIES_VERSION_1;
    return value;
}

fly_app_t* make_app()
{
    std::filesystem::path data_path;
    for (unsigned int attempt = 0u; attempt < 100u; ++attempt)
    {
        data_path = std::filesystem::temp_directory_path() /
                    ("flynes-scan-data-" + std::to_string(temp_sequence.fetch_add(1u)));
        std::error_code error;
        if (std::filesystem::create_directory(data_path, error))
        {
            break;
        }
        data_path.clear();
    }
    check(!data_path.empty(), "scan test creates an isolated data root");
    scan_data_roots.paths.push_back(data_path);
    const std::string data_root = data_path.u8string();

    static const fly_platform_capabilities capabilities = make_capabilities();
    fly_app_config config{};
    config.struct_size = FLY_APP_CONFIG_V1_SIZE;
    config.version = FLY_APP_CONFIG_VERSION_1;
    config.data_root_utf8 = data_root.data();
    config.cache_root_utf8 = data_root.data();
    config.platform_capabilities = &capabilities;
    config.data_root_utf8_length = static_cast<std::uint32_t>(data_root.size());
    config.cache_root_utf8_length = static_cast<std::uint32_t>(data_root.size());
    fly_app_t* app = nullptr;
    check(fly_app_create(&config, &app) == FLY_RESULT_OK, "scan test creates app");
    return app;
}

std::array<std::uint8_t, 16> source_uuid(std::uint8_t last = 1u)
{
    std::array<std::uint8_t, 16> value{};
    value[15] = last;
    return value;
}

fly_scan_config make_scan_config(
    std::uint8_t uuid_last = 1u,
    std::uint32_t scope = FLY_SOURCE_SCOPE_USER_DIRECTORY)
{
    fly_scan_config value{};
    value.struct_size = FLY_SCAN_CONFIG_V1_SIZE;
    value.version = FLY_SCAN_CONFIG_VERSION_1;
    const auto uuid = source_uuid(uuid_last);
    std::copy(uuid.begin(), uuid.end(), value.source_uuid);
    value.source_scope = scope;
    return value;
}

fly_scan_t* begin_scan(fly_app_t* app,
                       std::uint8_t uuid_last = 1u,
                       std::uint32_t scope = FLY_SOURCE_SCOPE_USER_DIRECTORY)
{
    fly_scan_config config = make_scan_config(uuid_last, scope);
    fly_scan_t* scan = nullptr;
    check(fly_scan_begin(app, &config, &scan) == FLY_RESULT_OK, "scan begins");
    check(scan != nullptr, "scan begin returns handle");
    return scan;
}

fly_scan_file make_scan_file(const char* path,
                             const char* display,
                             int fd,
                             std::uint64_t declared_size)
{
    fly_scan_file value{};
    value.struct_size = FLY_SCAN_FILE_V1_SIZE;
    value.version = FLY_SCAN_FILE_VERSION_1;
    value.source_relative_path_utf8 = path;
    value.display_name_utf8 = display;
    value.source_relative_path_utf8_length =
        path == nullptr ? 0u : static_cast<std::uint32_t>(std::strlen(path));
    value.display_name_utf8_length =
        display == nullptr ? 0u : static_cast<std::uint32_t>(std::strlen(display));
    value.borrowed_fd = fd;
    value.declared_size = declared_size;
    value.modified_time_hint_ns = INT64_C(123456789);
    return value;
}

fly_scan_file_result recorded_add(fly_scan_t* scan, const fly_scan_file& file)
{
    fly_scan_file_result result{};
    result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE;
    result.version = FLY_SCAN_FILE_RESULT_VERSION_1;
    check(fly_scan_add_file(scan, &file, &result) == FLY_RESULT_OK,
          "valid candidate is completely accounted by scan add");
    return result;
}

fly_catalog_snapshot_t* snapshot_of(const fly_app_t* app)
{
    fly_catalog_snapshot_t* snapshot = nullptr;
    check(fly_catalog_snapshot(app, &snapshot) == FLY_RESULT_OK, "snapshot acquired");
    return snapshot;
}

std::uint64_t snapshot_generation(const fly_catalog_snapshot_t* snapshot)
{
    std::uint64_t value = UINT64_MAX;
    check(fly_catalog_snapshot_generation(snapshot, &value) == FLY_RESULT_OK,
          "snapshot generation queried");
    return value;
}

std::uint64_t snapshot_count(const fly_catalog_snapshot_t* snapshot)
{
    std::uint64_t value = UINT64_MAX;
    check(fly_catalog_snapshot_count(snapshot, &value) == FLY_RESULT_OK,
          "snapshot count queried");
    return value;
}

std::string hex_of(const std::uint8_t* bytes, std::size_t size)
{
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string value;
    value.reserve(size * 2u);
    for (std::size_t index = 0; index < size; ++index)
    {
        value.push_back(digits[bytes[index] >> 4u]);
        value.push_back(digits[bytes[index] & 0x0Fu]);
    }
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

EntryValue read_entry(const fly_catalog_snapshot_t* snapshot, std::uint64_t index)
{
    fly_catalog_entry query{};
    query.struct_size = FLY_CATALOG_ENTRY_V1_SIZE;
    query.version = FLY_CATALOG_ENTRY_VERSION_1;
    check(fly_catalog_snapshot_get(snapshot, index, &query) == FLY_RESULT_BUFFER_TOO_SMALL,
          "entry size query reports required caller buffers");
    check(query.canonical_id_required > 1u && query.variant_id_required > 1u &&
              query.display_name_required > 1u && query.source_relative_path_required > 1u,
          "entry size query reports every string requirement");

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
    check(fly_catalog_snapshot_get(snapshot, index, &value.entry) == FLY_RESULT_OK,
          "entry copied to caller-owned buffers");
    value.canonical_id.resize(value.entry.canonical_id_required - 1u);
    value.variant_id.resize(value.entry.variant_id_required - 1u);
    value.display_name.resize(value.entry.display_name_required - 1u);
    value.source_relative_path.resize(value.entry.source_relative_path_required - 1u);
    return value;
}

void test_abi_values()
{
    static_assert(FLY_RESULT_INVALID_STATE == -8, "invalid-state result changed");
    static_assert(FLY_RESULT_CONFLICT == -9, "conflict result changed");
    static_assert(FLY_SOURCE_SCOPE_BUILTIN == 1, "builtin scope changed");
    static_assert(FLY_SOURCE_SCOPE_USER_DIRECTORY == 2, "directory scope changed");
    static_assert(FLY_SOURCE_SCOPE_USER_FILE == 3, "file scope changed");
    static_assert(FLY_SOURCE_SCOPE_MANAGED_LIBRARY == 4, "managed scope changed");
    static_assert(FLY_SCAN_COMPLETENESS_FULL == 1, "full completeness changed");
    static_assert(FLY_SCAN_COMPLETENESS_PARTIAL == 2, "partial completeness changed");
    static_assert(FLY_SCAN_COMPLETENESS_FATAL == 3, "fatal completeness changed");
    static_assert(FLY_PACKAGE_FORMAT_RAW == 1, "raw package value changed");
    static_assert(FLY_PACKAGE_FORMAT_ZIP == 2, "ZIP package value changed");
    static_assert(FLY_ROM_FORMAT_INES == 1 && FLY_ROM_FORMAT_NES2 == 2 &&
                      FLY_ROM_FORMAT_FDS == 3 && FLY_ROM_FORMAT_UNIF == 4,
                  "ROM format values changed");
    static_assert(FLY_COMPATIBILITY_PLAYABLE == 1 && FLY_COMPATIBILITY_UNSUPPORTED == 2 &&
                      FLY_COMPATIBILITY_INVALID == 3,
                  "compatibility state values changed");
    static_assert(FLY_COMPATIBILITY_REASON_PLAYABLE_NES == 1,
                  "compatibility reason values changed");
    static_assert(FLY_COMPATIBILITY_REASON_FDS_BIOS_API_NOT_IMPLEMENTED == 2,
                  "FDS reason changed");
    static_assert(FLY_COMPATIBILITY_REASON_UNIF_PRODUCT_DISABLED == 3,
                  "UNIF reason changed");
    static_assert(FLY_CATALOG_FRESHNESS_FRESH == 1 && FLY_CATALOG_FRESHNESS_STALE == 2,
                  "freshness values changed");
    static_assert(offsetof(fly_scan_config, struct_size) == 0u,
                  "scan config prefix changed");
    static_assert(offsetof(fly_scan_file, struct_size) == 0u, "scan file prefix changed");
    static_assert(offsetof(fly_catalog_entry, struct_size) == 0u, "entry prefix changed");
    static_assert(sizeof(((fly_catalog_entry*)nullptr)->source_uuid) == 16u,
                  "source UUID size changed");
    static_assert(sizeof(((fly_catalog_entry*)nullptr)->payload_sha1) == 20u,
                  "payload SHA-1 size changed");
    static_assert(sizeof(((fly_catalog_entry*)nullptr)->payload_sha256) == 32u,
                  "payload SHA-256 size changed");
    static_assert(sizeof(((fly_catalog_entry*)nullptr)->physical_sha256) == 32u,
                  "physical SHA-256 size changed");
    static_assert(sizeof(((fly_catalog_entry*)nullptr)->payload_crc32) == 4u,
                  "payload CRC-32 size changed");
}

void test_raw_commit_hashes_snapshot_and_fd_ownership()
{
    fly_app_t* app = make_app();
    fly_catalog_snapshot_t* before = snapshot_of(app);
    const std::vector<std::uint8_t> rom = make_nes();
    TempFile file(rom);
    const int borrowed = file.fd();
    fly_scan_t* scan = begin_scan(app);
    fly_scan_file candidate = make_scan_file("games/alpha.nes", "Alpha.nes", borrowed, 1u);
    const std::int64_t offset_before_add = fd_offset(borrowed);
    const fly_scan_file_result added = recorded_add(scan, candidate);
    check(added.outcome == FLY_SCAN_FILE_OUTCOME_INDEXED && added.variant_count == 1u,
          "raw MIT-style ROM is indexed despite an untrusted declared size hint");
    check(fd_is_open(borrowed), "scan add does not close its borrowed descriptor");
    check(fd_offset(borrowed) == offset_before_add,
          "scan add leaves the borrowed descriptor offset unchanged");
    file.close_now();
    check(fly_scan_commit(scan, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "scan commit succeeds after caller closes descriptor");
    check(fly_scan_commit(scan, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_INVALID_STATE,
          "committed scan is closed");

    fly_catalog_snapshot_t* after = snapshot_of(app);
    check(snapshot_generation(before) == 0u && snapshot_count(before) == 0u,
          "old immutable snapshot remains generation zero and empty");
    check(snapshot_generation(after) == 1u && snapshot_count(after) == 1u,
          "commit publishes generation one with a real row");
    EntryValue entry = read_entry(after, 0u);
    check(entry.entry.payload_size == rom.size(), "entry records actual payload size");
    check(entry.entry.physical_size == rom.size(), "entry records actual physical size");
    check(entry.entry.expected_bytes == rom.size(),
          "entry records parser-expected payload size");
    check(entry.entry.prg_bytes == 16u * 1024u && entry.entry.chr_bytes == 0u,
          "entry records PRG and CHR analysis byte counts");
    check(entry.entry.mapper == 0 && entry.entry.submapper == 0 &&
              entry.entry.disk_sides == 0u,
          "entry records mapper, submapper, and disk-side analysis");
    check(entry.entry.package_format == FLY_PACKAGE_FORMAT_RAW, "entry records raw package");
    check(entry.entry.rom_format == FLY_ROM_FORMAT_INES, "entry records iNES format");
    check(entry.entry.compatibility_state == FLY_COMPATIBILITY_PLAYABLE,
          "entry records playable compatibility");
    check(entry.entry.compatibility_reason == FLY_COMPATIBILITY_REASON_PLAYABLE_NES,
          "entry records playable reason");
    check(entry.entry.freshness == FLY_CATALOG_FRESHNESS_FRESH,
          "newly scanned entry is fresh");
    check(hex_of(entry.entry.payload_sha1, 20u) ==
              "5BCA67BC3627B5A2D2A49FEB18686D098212E8E1",
          "entry exposes complete payload SHA-1 bytes");
    check(hex_of(entry.entry.payload_sha256, 32u) ==
              "1E095A30CCAEC2D5D6C438538C1106DC7B8FD8D5631C5FAB156E00F579E5E06B",
          "entry exposes complete payload SHA-256 bytes");
    check(hex_of(entry.entry.physical_sha256, 32u) ==
              "1E095A30CCAEC2D5D6C438538C1106DC7B8FD8D5631C5FAB156E00F579E5E06B",
          "entry exposes complete physical SHA-256 bytes");
    const std::uint32_t crc = crc32_of(rom);
    check(entry.entry.payload_crc32[0] == static_cast<std::uint8_t>(crc >> 24u) &&
              entry.entry.payload_crc32[1] == static_cast<std::uint8_t>(crc >> 16u) &&
              entry.entry.payload_crc32[2] == static_cast<std::uint8_t>(crc >> 8u) &&
              entry.entry.payload_crc32[3] == static_cast<std::uint8_t>(crc),
          "entry exposes complete big-endian payload CRC-32 bytes");
    check(entry.canonical_id ==
              "game:1E095A30CCAEC2D5D6C438538C1106DC7B8FD8D5631C5FAB156E00F579E5E06B",
          "entry exposes full canonical ID without truncation");
    check(entry.variant_id.rfind("variant:", 0u) == 0u && entry.variant_id.size() == 72u,
          "entry exposes full variant ID without truncation");
    check(entry.display_name == "Alpha.nes", "entry copies the display filename");
    check(entry.source_relative_path == "games/alpha.nes",
          "entry exposes only the platform-neutral relative path");
    const auto expected_uuid = source_uuid();
    check(std::equal(expected_uuid.begin(), expected_uuid.end(), entry.entry.source_uuid),
          "entry exposes source UUID bytes");
    check(entry.entry.source_scope == FLY_SOURCE_SCOPE_USER_DIRECTORY,
          "entry exposes source scope");

    fly_app_destroy(app);
    check(snapshot_generation(after) == 1u && snapshot_count(after) == 1u,
          "new snapshot remains valid after app destruction");
    fly_catalog_snapshot_release(after);
    fly_catalog_snapshot_release(before);
    fly_scan_abort(scan);
}

void test_expected_hash_and_failure_preservation()
{
    fly_app_t* app = make_app();
    const std::vector<std::uint8_t> rom = make_nes();
    TempFile first_file(rom);
    fly_scan_t* first = begin_scan(app);
    fly_scan_file candidate =
        make_scan_file("same/game.nes", "Original.nes", first_file.fd(), rom.size());
    candidate.flags = FLY_SCAN_FILE_FLAG_EXPECTED_PHYSICAL_SHA256;
    const std::array<std::uint8_t, 32> expected = {
        0x1E, 0x09, 0x5A, 0x30, 0xCC, 0xAE, 0xC2, 0xD5,
        0xD6, 0xC4, 0x38, 0x53, 0x8C, 0x11, 0x06, 0xDC,
        0x7B, 0x8F, 0xD8, 0xD5, 0x63, 0x1C, 0x5F, 0xAB,
        0x15, 0x6E, 0x00, 0xF5, 0x79, 0xE5, 0xE0, 0x6B};
    std::copy(expected.begin(), expected.end(), candidate.expected_physical_sha256);
    fly_scan_file_result added = recorded_add(first, candidate);
    check(added.outcome == FLY_SCAN_FILE_OUTCOME_INDEXED &&
              added.reason == FLY_SCAN_FILE_REASON_INDEXED,
          "matching expected physical hash is accepted");
    check(fly_scan_commit(first, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "initial expected-hash scan commits");

    TempFile second_file(rom);
    fly_scan_t* second = begin_scan(app);
    candidate = make_scan_file("same/game.nes", "Changed.nes", second_file.fd(), rom.size());
    candidate.flags = FLY_SCAN_FILE_FLAG_EXPECTED_PHYSICAL_SHA256;
    candidate.expected_physical_sha256[0] = 0xFFu;
    added = recorded_add(second, candidate);
    check(added.outcome == FLY_SCAN_FILE_OUTCOME_REJECTED &&
              added.reason == FLY_SCAN_FILE_REASON_HASH_MISMATCH &&
              added.variant_count == 0u,
          "mismatched expected physical hash rejects and accounts for the candidate");
    check(fly_scan_commit(second, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "full scan can commit while preserving a failed candidate");
    fly_catalog_snapshot_t* after = snapshot_of(app);
    check(snapshot_generation(after) == 2u && snapshot_count(after) == 1u,
          "failed replacement publishes one stale old entry at next generation");
    EntryValue retained = read_entry(after, 0u);
    check(retained.display_name == "Original.nes" &&
              retained.entry.freshness == FLY_CATALOG_FRESHNESS_STALE,
          "candidate failure keeps the old value and marks it stale");

    fly_catalog_snapshot_release(after);
    fly_scan_abort(second);
    fly_scan_abort(first);
    fly_app_destroy(app);
}

void test_formats_zip_filtering_and_deterministic_order()
{
    fly_app_t* app = make_app();
    fly_scan_t* scan = begin_scan(app);
    std::vector<TempFile*> files;
    std::vector<std::vector<std::uint8_t>> payloads = {
        make_fds(), make_unif(), make_nes(9u, true)};
    const std::array<const char*, 3> paths = {"z/fds.fds", "z/unif.unf", "z/nes2.nes"};
    const std::array<const char*, 3> names = {"Disk.fds", "Board.unf", "Modern.nes"};
    for (std::size_t index = 0u; index < payloads.size(); ++index)
    {
        files.push_back(new TempFile(payloads[index]));
        fly_scan_file candidate = make_scan_file(
            paths[index], names[index], files.back()->fd(), payloads[index].size());
        const fly_scan_file_result added = recorded_add(scan, candidate);
        check(added.outcome == FLY_SCAN_FILE_OUTCOME_INDEXED && added.variant_count == 1u,
              "recognized raw format is indexed");
    }

    const std::vector<std::uint8_t> nested = make_stored_zip({{"deep.nes", make_nes(3u)}});
    const std::vector<std::uint8_t> package = make_stored_zip({
        {"folder/beta.nes", make_nes(11u)},
        {"folder/alpha.nes", make_nes(13u)},
        {"folder/readme.txt", {'h', 'i'}},
        {"folder/tool.exe", {'M', 'Z', 0u}},
        {"folder/nested.zip", nested},
        {"../escape.nes", make_nes(15u)},
        {"slash\\evil.nes", make_nes(17u)},
    });
    TempFile zip_file(package);
    fly_scan_file zip_candidate =
        make_scan_file("archives/set.zip", "Set.zip", zip_file.fd(), package.size());
    const fly_scan_file_result zip_added = recorded_add(scan, zip_candidate);
    check(zip_added.outcome == FLY_SCAN_FILE_OUTCOME_INDEXED &&
              zip_added.variant_count == 2u,
          "multi-entry ZIP indexes safe ROM entries and skips unsafe content");
    check(fly_scan_commit(scan, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "multi-format scan commits");
    fly_catalog_snapshot_t* snapshot = snapshot_of(app);
    check(snapshot_count(snapshot) == 5u,
          "catalog includes NES2, FDS, UNIF and two safe ZIP variants only");

    bool saw_fds = false;
    bool saw_unif = false;
    bool saw_nes2 = false;
    std::vector<std::string> variant_ids;
    for (std::uint64_t index = 0u; index < snapshot_count(snapshot); ++index)
    {
        const EntryValue entry = read_entry(snapshot, index);
        variant_ids.push_back(entry.variant_id);
        if (entry.entry.rom_format == FLY_ROM_FORMAT_FDS)
        {
            saw_fds = entry.entry.compatibility_state == FLY_COMPATIBILITY_UNSUPPORTED &&
                      entry.entry.compatibility_reason ==
                          FLY_COMPATIBILITY_REASON_FDS_BIOS_API_NOT_IMPLEMENTED &&
                      entry.entry.disk_sides == 1u && entry.entry.mapper == -1 &&
                      entry.entry.submapper == -1;
        }
        if (entry.entry.rom_format == FLY_ROM_FORMAT_UNIF)
        {
            saw_unif = entry.entry.compatibility_state == FLY_COMPATIBILITY_UNSUPPORTED &&
                       entry.entry.compatibility_reason ==
                           FLY_COMPATIBILITY_REASON_UNIF_PRODUCT_DISABLED;
        }
        if (entry.entry.rom_format == FLY_ROM_FORMAT_NES2)
        {
            saw_nes2 = entry.entry.compatibility_state == FLY_COMPATIBILITY_PLAYABLE;
        }
        check(entry.source_relative_path != "../escape.nes" &&
                  entry.display_name.find("nested.zip") == std::string::npos &&
                  entry.display_name.find("tool.exe") == std::string::npos &&
                  entry.display_name.find("slash\\") == std::string::npos,
              "unsafe, executable, and nested archive ZIP entries are absent");
    }
    check(saw_fds, "FDS is retained with the existing unsupported reason");
    check(saw_unif, "UNIF is retained with the existing unsupported reason");
    check(saw_nes2, "NES2 is retained as playable");
    check(std::is_sorted(variant_ids.begin(), variant_ids.end()),
          "catalog rows have deterministic variant-ID order");

    fly_catalog_snapshot_release(snapshot);
    fly_scan_abort(scan);
    for (TempFile* file : files)
    {
        delete file;
    }
    fly_app_destroy(app);
}

void test_raw_rejections_and_path_validation()
{
    fly_app_t* app = make_app();
    fly_scan_t* scan = begin_scan(app);
    TempFile valid(make_nes());
    const std::array<const char*, 9> bad_paths = {
        "", "/root.nes", "\\root.nes", "C:/root.nes", "a\\b.nes",
        "a/../b.nes", "a/./b.nes", "a//b.nes", "a/"};
    for (const char* path : bad_paths)
    {
        fly_scan_file candidate = make_scan_file(path, "Game.nes", valid.fd(), 0u);
        fly_scan_file_result result{};
        result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE;
        result.version = FLY_SCAN_FILE_RESULT_VERSION_1;
        const fly_scan_file_result before = result;
        check(fly_scan_add_file(scan, &candidate, &result) == FLY_RESULT_INVALID_ARGUMENT &&
                  std::memcmp(&result, &before, sizeof(result)) == 0,
              "unsafe physical relative path is rejected before descriptor use");
    }
    const char invalid_utf8[] = {'a', '/', static_cast<char>(0xC0),
                                 static_cast<char>(0xAF), '.', 'n', 'e', 's'};
    fly_scan_file invalid = make_scan_file("ok.nes", "Game.nes", valid.fd(), 0u);
    invalid.source_relative_path_utf8 = invalid_utf8;
    invalid.source_relative_path_utf8_length = sizeof(invalid_utf8);
    fly_scan_file_result invalid_result{};
    invalid_result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE;
    invalid_result.version = FLY_SCAN_FILE_RESULT_VERSION_1;
    check(fly_scan_add_file(scan, &invalid, &invalid_result) == FLY_RESULT_INVALID_ARGUMENT,
          "invalid UTF-8 physical path is rejected");
    const char embedded_nul[] = {'a', '\0', 'b'};
    invalid = make_scan_file("ok.nes", "Game.nes", valid.fd(), 0u);
    invalid.source_relative_path_utf8 = embedded_nul;
    invalid.source_relative_path_utf8_length = sizeof(embedded_nul);
    check(fly_scan_add_file(scan, &invalid, &invalid_result) == FLY_RESULT_INVALID_ARGUMENT,
          "NUL in physical path is rejected");
    std::string long_path(FLY_SCAN_RELATIVE_PATH_MAX_UTF8_BYTES + 1u, 'a');
    invalid = make_scan_file("ok.nes", "Game.nes", valid.fd(), 0u);
    invalid.source_relative_path_utf8 = long_path.data();
    invalid.source_relative_path_utf8_length = static_cast<std::uint32_t>(long_path.size());
    check(fly_scan_add_file(scan, &invalid, &invalid_result) == FLY_RESULT_INVALID_ARGUMENT,
          "overlong physical path is rejected");

    TempFile unknown({'n', 'o', 'p', 'e'});
    fly_scan_file candidate = make_scan_file("unknown.bin", "Unknown.bin", unknown.fd(), 4u);
    fly_scan_file_result added = recorded_add(scan, candidate);
    check(added.outcome == FLY_SCAN_FILE_OUTCOME_SKIPPED &&
              added.reason == FLY_SCAN_FILE_REASON_NO_CATALOGABLE_ROM,
          "unknown raw payload is safely accounted as non-catalog content");
    TempFile executable({'M', 'Z', 0u, 1u});
    candidate = make_scan_file("tool.exe", "Tool.exe", executable.fd(), 4u);
    added = recorded_add(scan, candidate);
    check(added.outcome == FLY_SCAN_FILE_OUTCOME_SKIPPED &&
              added.reason == FLY_SCAN_FILE_REASON_NO_CATALOGABLE_ROM,
          "executable raw payload is safely accounted as non-catalog content");
    TempFile invalid_rom({'N', 'E', 'S', 0x1Au});
    candidate = make_scan_file("broken.nes", "Broken.nes", invalid_rom.fd(), 4u);
    added = recorded_add(scan, candidate);
    check(added.outcome == FLY_SCAN_FILE_OUTCOME_INDEXED && added.variant_count == 1u,
          "recognized invalid ROM is represented with compatibility metadata");
    check(fly_scan_commit(scan, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "rejection scan commits atomically");
    fly_catalog_snapshot_t* snapshot = snapshot_of(app);
    check(snapshot_count(snapshot) == 1u, "unknown and executable are absent, invalid ROM remains");
    EntryValue entry = read_entry(snapshot, 0u);
    check(entry.entry.compatibility_state == FLY_COMPATIBILITY_INVALID &&
              entry.entry.compatibility_reason ==
                  FLY_COMPATIBILITY_REASON_NES_HEADER_INVALID,
          "invalid iNES reason is retained");

    fly_catalog_snapshot_release(snapshot);
    fly_scan_abort(scan);
    fly_app_destroy(app);
}

void test_descriptor_errors_limit_abort_conflict_and_lifetime()
{
    fly_app_t* app = make_app();
    fly_scan_t* aborted = begin_scan(app);
    fly_scan_abort(aborted);
    fly_catalog_snapshot_t* unchanged = snapshot_of(app);
    check(snapshot_generation(unchanged) == 0u, "abort does not advance generation");
    fly_catalog_snapshot_release(unchanged);

    fly_scan_t* first = begin_scan(app);
    fly_scan_t* second = begin_scan(app);
    TempFile one(make_nes(1u));
    TempFile two(make_nes(2u));
    fly_scan_file candidate = make_scan_file("one.nes", "One.nes", one.fd(), 0u);
    check(recorded_add(first, candidate).outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "first concurrent scan accepts candidate");
    candidate = make_scan_file("two.nes", "Two.nes", two.fd(), 0u);
    check(recorded_add(second, candidate).outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "second concurrent scan accepts candidate");
    check(fly_scan_commit(first, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "first concurrent scan wins");
    check(fly_scan_commit(second, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_CONFLICT,
          "stale base generation returns fixed conflict result");
    check(fly_scan_commit(second, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_INVALID_STATE,
          "conflicted scan is closed");
    fly_catalog_snapshot_t* generation_one = snapshot_of(app);
    check(snapshot_generation(generation_one) == 1u,
          "conflict does not advance catalog generation");
    fly_catalog_snapshot_release(generation_one);

    fly_scan_t* bad_fd_scan = begin_scan(app);
    candidate = make_scan_file("missing.nes", "Missing.nes", -1, 0u);
    fly_scan_file_result added = recorded_add(bad_fd_scan, candidate);
    check(added.outcome == FLY_SCAN_FILE_OUTCOME_REJECTED &&
              added.reason == FLY_SCAN_FILE_REASON_IO_ERROR,
          "invalid descriptor is a recorded per-candidate I/O failure");

    std::vector<std::uint8_t> oversized(FLY_SCAN_MAX_PACKAGE_BYTES + 1u, 0x5Au);
    TempFile huge(oversized);
    fly_scan_t* huge_scan = begin_scan(app);
    candidate = make_scan_file("huge.bin", "Huge.bin", huge.fd(), 1u);
    added = recorded_add(huge_scan, candidate);
    check(added.outcome == FLY_SCAN_FILE_OUTCOME_REJECTED &&
              added.reason == FLY_SCAN_FILE_REASON_PACKAGE_LIMIT_EXCEEDED,
          "actual 8 MiB plus one byte is rejected despite small declared size");

    fly_scan_t* orphan = begin_scan(app);
    fly_app_destroy(app);
    candidate = make_scan_file("late.nes", "Late.nes", one.fd(), 0u);
    fly_scan_file_result orphan_result{};
    orphan_result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE;
    orphan_result.version = FLY_SCAN_FILE_RESULT_VERSION_1;
    check(fly_scan_add_file(orphan, &candidate, &orphan_result) == FLY_RESULT_INVALID_STATE,
          "scan detects destroyed app through weak state");
    check(fly_scan_commit(orphan, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_INVALID_STATE,
          "orphan scan cannot publish");
    fly_scan_abort(orphan);
    fly_scan_abort(huge_scan);
    fly_scan_abort(bad_fd_scan);
    fly_scan_abort(second);
    fly_scan_abort(first);
    fly_scan_abort(nullptr);
}

void test_completeness_and_atomic_get()
{
    fly_app_t* app = make_app();
    TempFile alpha(make_nes(21u));
    TempFile beta(make_nes(22u));
    fly_scan_t* initial = begin_scan(app);
    fly_scan_file candidate = make_scan_file("alpha.nes", "Alpha.nes", alpha.fd(), 0u);
    check(recorded_add(initial, candidate).outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "initial alpha added");
    candidate = make_scan_file("beta.nes", "Beta.nes", beta.fd(), 0u);
    check(recorded_add(initial, candidate).outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "initial beta added");
    check(fly_scan_commit(initial, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "initial pair commits");

    fly_scan_t* partial = begin_scan(app);
    candidate = make_scan_file("alpha.nes", "Alpha new.nes", alpha.fd(), 0u);
    check(recorded_add(partial, candidate).outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "partial scan adds alpha");
    check(fly_scan_commit(partial, FLY_SCAN_COMPLETENESS_PARTIAL) == FLY_RESULT_OK,
          "partial scan commits");
    fly_catalog_snapshot_t* partial_snapshot = snapshot_of(app);
    check(snapshot_count(partial_snapshot) == 2u,
          "partial scan preserves unenumerated beta as stale");
    bool beta_stale = false;
    for (std::uint64_t index = 0u; index < 2u; ++index)
    {
        EntryValue entry = read_entry(partial_snapshot, index);
        if (entry.source_relative_path == "beta.nes")
        {
            beta_stale = entry.entry.freshness == FLY_CATALOG_FRESHNESS_STALE;
        }
    }
    check(beta_stale, "partial unenumerated entry is marked stale");

    fly_scan_t* fatal = begin_scan(app);
    check(recorded_add(fatal, candidate).outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "scan may enumerate before learning its final fatal completeness");
    check(fly_scan_commit(fatal, FLY_SCAN_COMPLETENESS_FATAL) == FLY_RESULT_OK,
          "fatal scan discards current candidates and publishes stale state");
    fly_catalog_snapshot_t* fatal_snapshot = snapshot_of(app);
    check(snapshot_generation(fatal_snapshot) == 3u && snapshot_count(fatal_snapshot) == 2u,
          "fatal commit keeps entries and advances one generation");
    for (std::uint64_t index = 0u; index < 2u; ++index)
    {
        check(read_entry(fatal_snapshot, index).entry.freshness ==
                  FLY_CATALOG_FRESHNESS_STALE,
              "fatal commit marks every source entry stale");
    }

    fly_catalog_entry query;
    std::memset(&query, 0xA5, sizeof(query));
    query.struct_size = FLY_CATALOG_ENTRY_V1_SIZE;
    query.version = FLY_CATALOG_ENTRY_VERSION_1;
    std::array<char, 4> canonical_buffer{};
    std::array<char, 4> variant_buffer{};
    std::array<char, 4> display_buffer{};
    std::array<char, 4> path_buffer{};
    canonical_buffer.fill('C');
    variant_buffer.fill('V');
    display_buffer.fill('D');
    path_buffer.fill('P');
    query.canonical_id_utf8 = canonical_buffer.data();
    query.canonical_id_capacity = static_cast<std::uint32_t>(canonical_buffer.size());
    query.variant_id_utf8 = variant_buffer.data();
    query.variant_id_capacity = static_cast<std::uint32_t>(variant_buffer.size());
    query.display_name_utf8 = display_buffer.data();
    query.display_name_capacity = static_cast<std::uint32_t>(display_buffer.size());
    query.source_relative_path_utf8 = path_buffer.data();
    query.source_relative_path_capacity = static_cast<std::uint32_t>(path_buffer.size());
    fly_catalog_entry expected = query;
    check(fly_catalog_snapshot_get(fatal_snapshot, 0u, &query) ==
              FLY_RESULT_BUFFER_TOO_SMALL,
          "insufficient entry buffers report a retryable result");
    expected.canonical_id_required = query.canonical_id_required;
    expected.variant_id_required = query.variant_id_required;
    expected.display_name_required = query.display_name_required;
    expected.source_relative_path_required = query.source_relative_path_required;
    check(std::memcmp(&query, &expected, sizeof(query)) == 0,
          "insufficient buffers modify only all four required-size fields");
    check(std::all_of(canonical_buffer.begin(), canonical_buffer.end(),
                      [](char value) { return value == 'C'; }) &&
              std::all_of(variant_buffer.begin(), variant_buffer.end(),
                          [](char value) { return value == 'V'; }) &&
              std::all_of(display_buffer.begin(), display_buffer.end(),
                          [](char value) { return value == 'D'; }) &&
              std::all_of(path_buffer.begin(), path_buffer.end(),
                          [](char value) { return value == 'P'; }),
          "insufficient buffers do not modify any character");

    fly_catalog_entry invalid = query;
    invalid.version = FLY_CATALOG_ENTRY_VERSION_1 + 1u;
    const fly_catalog_entry invalid_before = invalid;
    check(fly_catalog_snapshot_get(fatal_snapshot, 0u, &invalid) ==
              FLY_RESULT_UNSUPPORTED_VERSION &&
              std::memcmp(&invalid, &invalid_before, sizeof(invalid)) == 0,
          "version error does not modify entry output");
    fly_catalog_entry out_of_range = query;
    const fly_catalog_entry range_before = out_of_range;
    check(fly_catalog_snapshot_get(fatal_snapshot, 99u, &out_of_range) ==
              FLY_RESULT_OUT_OF_RANGE &&
              std::memcmp(&out_of_range, &range_before, sizeof(out_of_range)) == 0,
          "index error does not modify entry output");

    fly_catalog_snapshot_release(fatal_snapshot);
    fly_catalog_snapshot_release(partial_snapshot);
    fly_scan_abort(fatal);
    fly_scan_abort(partial);
    fly_scan_abort(initial);
    fly_app_destroy(app);
}

void test_full_source_isolation_and_scope_immutability()
{
    fly_app_t* app = make_app();
    TempFile alpha(make_nes(31u));
    TempFile beta(make_nes(32u));
    TempFile gamma(make_nes(33u));

    fly_scan_t* source_one = begin_scan(app, 1u);
    fly_scan_file candidate = make_scan_file("alpha.nes", "Alpha.nes", alpha.fd(), 0u);
    check(recorded_add(source_one, candidate).outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "source one alpha is indexed");
    candidate = make_scan_file("beta.nes", "Beta.nes", beta.fd(), 0u);
    check(recorded_add(source_one, candidate).outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "source one beta is indexed");
    check(fly_scan_commit(source_one, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "source one initial full scan commits");

    fly_scan_t* source_two = begin_scan(app, 2u);
    candidate = make_scan_file("gamma.nes", "Gamma.nes", gamma.fd(), 0u);
    check(recorded_add(source_two, candidate).outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "source two gamma is indexed");
    check(fly_scan_commit(source_two, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "source two full scan commits");

    fly_scan_t* replacement = begin_scan(app, 1u);
    candidate = make_scan_file("alpha.nes", "Alpha new.nes", alpha.fd(), 0u);
    check(recorded_add(replacement, candidate).outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "replacement full scan accounts for alpha only");
    check(fly_scan_commit(replacement, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "replacement full scan commits");
    fly_catalog_snapshot_t* snapshot = snapshot_of(app);
    check(snapshot_count(snapshot) == 2u,
          "full scan removes unmentioned same-source beta but preserves other-source gamma");
    bool alpha_present = false;
    bool beta_present = false;
    bool gamma_present = false;
    for (std::uint64_t index = 0u; index < snapshot_count(snapshot); ++index)
    {
        EntryValue entry = read_entry(snapshot, index);
        alpha_present |= entry.source_relative_path == "alpha.nes" &&
                         entry.display_name == "Alpha new.nes" &&
                         entry.entry.source_uuid[15] == 1u;
        beta_present |= entry.source_relative_path == "beta.nes";
        gamma_present |= entry.source_relative_path == "gamma.nes" &&
                         entry.entry.source_uuid[15] == 2u;
    }
    check(alpha_present && !beta_present && gamma_present,
          "full source update matrix is isolated by UUID and path");

    fly_scan_config changed_scope =
        make_scan_config(1u, FLY_SOURCE_SCOPE_USER_FILE);
    fly_scan_t* forbidden = reinterpret_cast<fly_scan_t*>(static_cast<std::uintptr_t>(1u));
    check(fly_scan_begin(app, &changed_scope, &forbidden) == FLY_RESULT_CONFLICT &&
              forbidden == nullptr,
          "an existing source UUID cannot silently change portable scope");

    fly_catalog_snapshot_release(snapshot);
    fly_scan_abort(replacement);
    fly_scan_abort(source_two);
    fly_scan_abort(source_one);
    fly_app_destroy(app);
}

void test_empty_full_scan_still_pins_source_scope()
{
    fly_app_t* app = make_app();
    TempFile rom(make_nes(41u));
    fly_scan_t* initial = begin_scan(app, 1u);
    fly_scan_file candidate = make_scan_file("alpha.nes", "Alpha.nes", rom.fd(), 0u);
    check(recorded_add(initial, candidate).outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "initial row is indexed before empty replacement");
    check(fly_scan_commit(initial, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "initial source commit succeeds");

    fly_scan_t* empty = begin_scan(app, 1u);
    check(fly_scan_commit(empty, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "empty full scan commits");
    fly_catalog_snapshot_t* snapshot = snapshot_of(app);
    check(snapshot_count(snapshot) == 0u, "empty full scan removes source rows");

    fly_scan_config changed_scope =
        make_scan_config(1u, FLY_SOURCE_SCOPE_USER_FILE);
    fly_scan_t* forbidden = reinterpret_cast<fly_scan_t*>(static_cast<std::uintptr_t>(1u));
    check(fly_scan_begin(app, &changed_scope, &forbidden) == FLY_RESULT_CONFLICT &&
              forbidden == nullptr,
          "source UUID scope remains pinned after all rows disappear");
    fly_scan_t* same_scope = begin_scan(app, 1u, FLY_SOURCE_SCOPE_USER_DIRECTORY);
    check(same_scope != nullptr, "same scope may begin after empty full scan");

    fly_catalog_snapshot_release(snapshot);
    fly_scan_abort(same_scope);
    fly_scan_abort(empty);
    fly_scan_abort(initial);
    fly_app_destroy(app);
}

void test_zip_exact_identity_replacement_and_skip()
{
    fly_app_t* app = make_app();
    const std::vector<std::uint8_t> rom = make_nes();
    const std::vector<std::uint8_t> duplicate_zip = make_stored_zip({
        {"same.nes", rom},
        {"same.nes", rom},
    });
    TempFile duplicate_file(duplicate_zip);
    fly_scan_t* first = begin_scan(app);
    fly_scan_file candidate =
        make_scan_file("duplicate.zip", "Duplicate.zip", duplicate_file.fd(), 0u);
    const fly_scan_file_result duplicate_result = recorded_add(first, candidate);
    check(duplicate_result.outcome == FLY_SCAN_FILE_OUTCOME_INDEXED &&
              duplicate_result.variant_count == 2u,
          "duplicate raw ZIP names remain distinct exact variants by local offset");
    check(fly_scan_commit(first, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "duplicate-name ZIP commits");
    fly_catalog_snapshot_t* duplicate_snapshot = snapshot_of(app);
    check(snapshot_count(duplicate_snapshot) == 2u,
          "duplicate-name ZIP publishes two variants");
    std::vector<std::string> exact_ids;
    for (std::uint64_t index = 0u; index < 2u; ++index)
    {
        EntryValue entry = read_entry(duplicate_snapshot, index);
        exact_ids.push_back(entry.variant_id);
        check(entry.source_relative_path == "duplicate.zip",
              "ZIP variant retains the outer physical source path");
        check(entry.display_name == "same.nes",
              "decoded ZIP entry name is display-only metadata");
    }
    const std::vector<std::string> expected_ids = {
        "variant:0086DB6524CAE6D4A598FB6C23AB0AFCE8F0F3D29B5F4A28C49EBFA72213479A",
        "variant:47F24DEA0E348F0BDFDE2A0E220E458C5741C7117178EEB9D6527A80A402DBCC",
    };
    check(exact_ids == expected_ids,
          "raw name plus local-header offset has a frozen exact variant-ID golden");

    const std::vector<std::uint8_t> one_zip = make_stored_zip({{"same.nes", rom}});
    TempFile one_file(one_zip);
    fly_scan_t* replacement = begin_scan(app);
    candidate = make_scan_file("duplicate.zip", "Duplicate.zip", one_file.fd(), 0u);
    check(recorded_add(replacement, candidate).variant_count == 1u,
          "replacement ZIP has one accounted variant");
    check(fly_scan_commit(replacement, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "replacement ZIP full scan commits");
    fly_catalog_snapshot_t* replaced = snapshot_of(app);
    check(snapshot_count(replaced) == 1u &&
              read_entry(replaced, 0u).variant_id == expected_ids[1],
          "one ZIP path replacement removes the disappeared exact variant");

    TempFile text({'n', 'o', 't', '-', 'a', '-', 'r', 'o', 'm'});
    fly_scan_t* skipped = begin_scan(app);
    candidate = make_scan_file("duplicate.zip", "Duplicate.zip", text.fd(), 0u);
    const fly_scan_file_result skipped_result = recorded_add(skipped, candidate);
    check(skipped_result.outcome == FLY_SCAN_FILE_OUTCOME_SKIPPED &&
              skipped_result.reason == FLY_SCAN_FILE_REASON_NO_CATALOGABLE_ROM,
          "accounted non-ROM replacement is explicitly skipped");
    check(fly_scan_commit(skipped, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "skipped replacement commits");
    fly_catalog_snapshot_t* empty = snapshot_of(app);
    check(snapshot_count(empty) == 0u,
          "SKIPPED removes all prior variants for its accounted path");

    fly_catalog_snapshot_release(empty);
    fly_catalog_snapshot_release(replaced);
    fly_catalog_snapshot_release(duplicate_snapshot);
    fly_scan_abort(skipped);
    fly_scan_abort(replacement);
    fly_scan_abort(first);
    fly_app_destroy(app);
}

void test_versioned_scan_structs_duplicate_and_display_validation()
{
    fly_app_t* app = make_app();
    TempFile file(make_nes());

    fly_scan_config config = make_scan_config();
    fly_scan_t* scan = reinterpret_cast<fly_scan_t*>(static_cast<std::uintptr_t>(1u));
    config.struct_size = FLY_SCAN_CONFIG_V1_SIZE - 1u;
    check(fly_scan_begin(app, &config, &scan) == FLY_RESULT_STRUCT_TOO_SMALL &&
              scan == nullptr,
          "scan begin rejects a short config and clears output");
    config = make_scan_config();
    config.version = FLY_SCAN_CONFIG_VERSION_1 + 1u;
    scan = reinterpret_cast<fly_scan_t*>(static_cast<std::uintptr_t>(1u));
    check(fly_scan_begin(app, &config, &scan) == FLY_RESULT_UNSUPPORTED_VERSION &&
              scan == nullptr,
          "scan begin rejects an unknown config version");
    config = make_scan_config();
    config.struct_size = FLY_SCAN_CONFIG_V1_SIZE + 16u;
    scan = nullptr;
    check(fly_scan_begin(app, &config, &scan) == FLY_RESULT_OK && scan != nullptr,
          "scan begin accepts a larger complete known prefix");

    fly_scan_file candidate = make_scan_file("one.nes", "One.nes", file.fd(), 0u);
    fly_scan_file_result result{};
    result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE;
    result.version = FLY_SCAN_FILE_RESULT_VERSION_1;
    result.outcome = UINT32_C(0xAAAAAAAA);
    const fly_scan_file_result unchanged = result;

    fly_scan_file invalid = candidate;
    invalid.struct_size = FLY_SCAN_FILE_V1_SIZE - 1u;
    check(fly_scan_add_file(scan, &invalid, &result) == FLY_RESULT_STRUCT_TOO_SMALL &&
              std::memcmp(&result, &unchanged, sizeof(result)) == 0,
          "short file input leaves result and transaction unchanged");
    invalid = candidate;
    invalid.version = FLY_SCAN_FILE_VERSION_1 + 1u;
    check(fly_scan_add_file(scan, &invalid, &result) == FLY_RESULT_UNSUPPORTED_VERSION &&
              std::memcmp(&result, &unchanged, sizeof(result)) == 0,
          "unknown file version leaves result unchanged");
    invalid = candidate;
    invalid.flags = UINT32_C(0x80000000);
    check(fly_scan_add_file(scan, &invalid, &result) == FLY_RESULT_INVALID_ARGUMENT &&
              std::memcmp(&result, &unchanged, sizeof(result)) == 0,
          "unknown file flags leave result unchanged");

    fly_scan_file_result short_result = result;
    short_result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE - 1u;
    const fly_scan_file_result short_before = short_result;
    check(fly_scan_add_file(scan, &candidate, &short_result) == FLY_RESULT_STRUCT_TOO_SMALL &&
              std::memcmp(&short_result, &short_before, sizeof(short_result)) == 0,
          "short result output prevents candidate append and remains unchanged");
    fly_scan_file_result future_result = result;
    future_result.version = FLY_SCAN_FILE_RESULT_VERSION_1 + 1u;
    const fly_scan_file_result future_before = future_result;
    check(fly_scan_add_file(scan, &candidate, &future_result) ==
                  FLY_RESULT_UNSUPPORTED_VERSION &&
              std::memcmp(&future_result, &future_before, sizeof(future_result)) == 0,
          "unknown result version prevents append and remains unchanged");

    const std::array<const char*, 5> invalid_displays = {
        "", ".", "..", "dir/name.nes", "dir\\name.nes"};
    for (const char* display : invalid_displays)
    {
        invalid = make_scan_file("valid.nes", display, file.fd(), 0u);
        result = unchanged;
        check(fly_scan_add_file(scan, &invalid, &result) == FLY_RESULT_INVALID_ARGUMENT &&
                  std::memcmp(&result, &unchanged, sizeof(result)) == 0,
              "display name must be one safe non-empty filename");
    }
    const char invalid_utf8[] = {static_cast<char>(0xE2), static_cast<char>(0x82)};
    invalid = candidate;
    invalid.display_name_utf8 = invalid_utf8;
    invalid.display_name_utf8_length = sizeof(invalid_utf8);
    result = unchanged;
    check(fly_scan_add_file(scan, &invalid, &result) == FLY_RESULT_INVALID_ARGUMENT &&
              std::memcmp(&result, &unchanged, sizeof(result)) == 0,
          "display name requires strict complete UTF-8");
    const char nul_display[] = {'a', '\0', 'b'};
    invalid = candidate;
    invalid.display_name_utf8 = nul_display;
    invalid.display_name_utf8_length = sizeof(nul_display);
    result = unchanged;
    check(fly_scan_add_file(scan, &invalid, &result) == FLY_RESULT_INVALID_ARGUMENT &&
              std::memcmp(&result, &unchanged, sizeof(result)) == 0,
          "display name rejects embedded NUL");
    std::string long_display(FLY_SCAN_DISPLAY_NAME_MAX_UTF8_BYTES + 1u, 'n');
    invalid = candidate;
    invalid.display_name_utf8 = long_display.data();
    invalid.display_name_utf8_length = static_cast<std::uint32_t>(long_display.size());
    result = unchanged;
    check(fly_scan_add_file(scan, &invalid, &result) == FLY_RESULT_INVALID_ARGUMENT &&
              std::memcmp(&result, &unchanged, sizeof(result)) == 0,
          "display name rejects more than 1024 UTF-8 bytes");

    candidate.struct_size = FLY_SCAN_FILE_V1_SIZE + 16u;
    result = unchanged;
    result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE + 16u;
    check(fly_scan_add_file(scan, &candidate, &result) == FLY_RESULT_OK &&
              result.outcome == FLY_SCAN_FILE_OUTCOME_INDEXED,
          "file add accepts larger complete known prefixes");
    const fly_scan_file_result duplicate_before = unchanged;
    result = duplicate_before;
    check(fly_scan_add_file(scan, &candidate, &result) == FLY_RESULT_INVALID_ARGUMENT &&
              std::memcmp(&result, &duplicate_before, sizeof(result)) == 0,
          "duplicate relative path is rejected without changing result or prior candidate");
    check(fly_scan_commit(scan, 0u) == FLY_RESULT_INVALID_ARGUMENT,
          "invalid final completeness does not consume the transaction");
    check(fly_scan_commit(scan, FLY_SCAN_COMPLETENESS_FULL) == FLY_RESULT_OK,
          "transaction remains committable after invalid final completeness");
    fly_catalog_snapshot_t* snapshot = snapshot_of(app);
    check(snapshot_count(snapshot) == 1u,
          "only the one valid candidate was appended through validation failures");

    fly_catalog_snapshot_release(snapshot);
    fly_scan_abort(scan);
    fly_app_destroy(app);
}

void test_scan_begin_validation()
{
    fly_app_t* app = make_app();
    fly_scan_t* output = reinterpret_cast<fly_scan_t*>(static_cast<std::uintptr_t>(1u));
    fly_scan_config config = make_scan_config();
    check(fly_scan_begin(nullptr, &config, &output) == FLY_RESULT_INVALID_ARGUMENT &&
              output == nullptr,
          "scan begin rejects null app and clears output");
    output = reinterpret_cast<fly_scan_t*>(static_cast<std::uintptr_t>(1u));
    std::fill(std::begin(config.source_uuid),
              std::end(config.source_uuid),
              static_cast<std::uint8_t>(0u));
    check(fly_scan_begin(app, &config, &output) == FLY_RESULT_INVALID_ARGUMENT &&
              output == nullptr,
          "scan begin rejects all-zero UUID and clears output");
    config = make_scan_config();
    config.source_scope = 0u;
    output = reinterpret_cast<fly_scan_t*>(static_cast<std::uintptr_t>(1u));
    check(fly_scan_begin(app, &config, &output) == FLY_RESULT_INVALID_ARGUMENT &&
              output == nullptr,
          "scan begin rejects unknown source scope");
    config = make_scan_config();
    fly_app_destroy(app);
}

} // namespace

int main()
{
    test_abi_values();
    test_scan_begin_validation();
    test_raw_commit_hashes_snapshot_and_fd_ownership();
    test_expected_hash_and_failure_preservation();
    test_formats_zip_filtering_and_deterministic_order();
    test_raw_rejections_and_path_validation();
    test_descriptor_errors_limit_abort_conflict_and_lifetime();
    test_completeness_and_atomic_get();
    test_full_source_isolation_and_scope_immutability();
    test_empty_full_scan_still_pins_source_scope();
    test_zip_exact_identity_replacement_and_skip();
    test_versioned_scan_structs_duplicate_and_display_validation();
    if (failures == 0)
    {
        std::puts("flynes_catalog_scan_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
