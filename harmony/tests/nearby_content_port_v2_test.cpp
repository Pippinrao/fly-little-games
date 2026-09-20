#include "content_port_v2.hpp"
#include <flynes/product/dual_start_identity.hpp>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace harmony_v2_tests {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
// Independent snapshot outlives app and original fd; metadata queries cannot read ROMs.
fly_catalog_snapshot_t* make_snapshot(std::uint32_t count, bool zipped = false) {
    static std::atomic<unsigned> sequence{0};
    const auto root = std::filesystem::temp_directory_path() /
        ("flynes-h1-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         "-" + std::to_string(++sequence));
    std::filesystem::create_directories(root);
    const std::string data = (root / "data").string(), cache = (root / "cache").string();
    fly_platform_capabilities caps{FLY_PLATFORM_CAPABILITIES_V1_SIZE, 1, 0};
    fly_app_config config{};
    config.struct_size = FLY_APP_CONFIG_V1_SIZE; config.version = 1;
    config.data_root_utf8 = data.c_str(); config.data_root_utf8_length = static_cast<std::uint32_t>(data.size());
    config.cache_root_utf8 = cache.c_str(); config.cache_root_utf8_length = static_cast<std::uint32_t>(cache.size());
    config.platform_capabilities = &caps;
    fly_app_t* app = nullptr;
    if (fly_app_create(&config, &app) != FLY_RESULT_OK) throw std::runtime_error("fixture app");
    std::FILE* rom = nullptr;
#if defined(_WIN32)
    if (::tmpfile_s(&rom) != 0) throw std::runtime_error("fixture temporary ROM");
#else
    rom = std::tmpfile();
#endif
    if (!rom) throw std::runtime_error("fixture temporary ROM");
    std::vector<std::uint8_t> bytes(16 + 16384, 0);
    bytes[0] = 'N'; bytes[1] = 'E'; bytes[2] = 'S'; bytes[3] = 0x1a; bytes[4] = 1;
    if (zipped) {
        const std::string raw_name = "folder/\xd6\xd0.nes"; // GBK, distinct from decoded UTF-8 title
        std::uint32_t crc = 0xffffffffu;
        for (auto byte : bytes) { crc ^= byte; for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1))); }
        crc ^= 0xffffffffu;
        std::vector<std::uint8_t> archive;
        const auto u16 = [&](std::uint16_t n) { archive.push_back(n & 255); archive.push_back(n >> 8); };
        const auto u32 = [&](std::uint32_t n) { u16(n & 65535); u16(n >> 16); };
        u32(0x04034b50); u16(20); u16(0); u16(0); u16(0); u16(0);
        u32(crc); u32(static_cast<std::uint32_t>(bytes.size())); u32(static_cast<std::uint32_t>(bytes.size())); u16(static_cast<std::uint16_t>(raw_name.size())); u16(0);
        archive.insert(archive.end(), raw_name.begin(), raw_name.end());
        archive.insert(archive.end(), bytes.begin(), bytes.end());
        const auto central = archive.size();
        u32(0x02014b50); u16(20); u16(20); u16(0); u16(0); u16(0); u16(0);
        u32(crc); u32(static_cast<std::uint32_t>(bytes.size())); u32(static_cast<std::uint32_t>(bytes.size())); u16(static_cast<std::uint16_t>(raw_name.size())); u16(0); u16(0);
        u16(0); u16(0); u32(0); u32(0); archive.insert(archive.end(), raw_name.begin(), raw_name.end());
        const auto central_size = archive.size() - central;
        u32(0x06054b50); u16(0); u16(0); u16(1); u16(1); u32(static_cast<std::uint32_t>(central_size)); u32(static_cast<std::uint32_t>(central)); u16(0);
        bytes = std::move(archive);
    }
    std::fwrite(bytes.data(), 1, bytes.size(), rom); std::fflush(rom);
#if defined(_WIN32)
    const int fd = _fileno(rom);
#else
    const int fd = fileno(rom);
#endif
    fly_scan_config scan_config{};
    scan_config.struct_size = FLY_SCAN_CONFIG_V1_SIZE; scan_config.version = 1;
    scan_config.source_uuid[0] = 7; scan_config.source_scope = FLY_SOURCE_SCOPE_MANAGED_LIBRARY;
    fly_scan_t* scan = nullptr;
    if (fly_scan_begin(app, &scan_config, &scan) != FLY_RESULT_OK) throw std::runtime_error("fixture scan");
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto path = "fixture-" + std::to_string(i) + (zipped ? ".zip" : ".nes");
        fly_scan_file file{};
        file.struct_size = FLY_SCAN_FILE_V1_SIZE; file.version = 1;
        file.source_relative_path_utf8 = path.c_str(); file.source_relative_path_utf8_length = static_cast<std::uint32_t>(path.size());
        file.display_name_utf8 = path.c_str(); file.display_name_utf8_length = static_cast<std::uint32_t>(path.size());
        file.borrowed_fd = fd; file.declared_size = bytes.size();
        fly_scan_file_result result{};
        result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE; result.version = 1;
        if (fly_scan_add_file(scan, &file, &result) != FLY_RESULT_OK || result.outcome != FLY_SCAN_FILE_OUTCOME_INDEXED)
            throw std::runtime_error("fixture index");
    }
    if (fly_scan_commit(scan, FLY_SCAN_COMPLETENESS_FULL) != FLY_RESULT_OK) throw std::runtime_error("fixture commit");
    fly_scan_abort(scan); std::fclose(rom);
    fly_catalog_snapshot_t* snapshot = nullptr;
    if (fly_catalog_snapshot(app, &snapshot) != FLY_RESULT_OK) throw std::runtime_error("fixture snapshot");
    fly_app_destroy(app);
    // Only this function's newly created unique temp tree is removed.
    std::filesystem::remove_all(root);
    return snapshot;
}

void content_tests() {
    using flynes::harmony::nearby::ContentPortV2;
    auto random = [](ContentPortV2::Ref& out) { out.fill(0x73); return true; };
    ContentPortV2 content(random);
    auto* zip_snapshot = make_snapshot(1, true);
    check(content.publish(zip_snapshot, 1) == FLY_SESSION_V2_OK, "actual ZIP catalog captured");
    std::vector<std::uint8_t> zip_record;
    check(content.query_record(0, &zip_record) == FLY_SESSION_V2_OK, "ZIP metadata record");
    if (zip_record.size() >= 153) {
        ContentPortV2::Ref ref{}; std::copy_n(zip_record.data() + 4, 16, ref.begin());
        ContentPortV2::Selection zip_selection;
        check(content.resolve(ref, &zip_selection) && zip_selection.zip_offset == 0 &&
              std::string(zip_selection.zip_raw_name.begin(), zip_selection.zip_raw_name.end()) == "folder/\xd6\xd0.nes" &&
              zip_selection.display_name != "folder/\xd6\xd0.nes" &&
              zip_selection.package_format == FLY_PACKAGE_FORMAT_ZIP &&
              zip_selection.physical_hash != zip_selection.payload_hash, "ZIP raw bytes and offset do not normalize to display name");
    }
    fly_catalog_snapshot_release(zip_snapshot);
    ContentPortV2 untouched(random);
    auto* snapshot = make_snapshot(3);
    check(!untouched.query_status().attempted, "unqueried differs from empty");
    check(content.publish(snapshot, 9) == FLY_SESSION_V2_OK, "capture actual immutable catalog");
    fly_catalog_snapshot_release(snapshot);
    std::vector<std::uint8_t> first, second, last;
    check(content.query_record(0, &first) == FLY_SESSION_V2_OK, "enumerate metadata after app and ROM fd destruction");
    check(content.query_record(1, &second) == FLY_SESSION_V2_OK, "same payload from distinct source paths stays distinct");
    check(content.query_record(2, &last) == FLY_SESSION_V2_OK, "third metadata record available");
    check(content.query_record(3, &last) == FLY_SESSION_V2_EMPTY, "catalog has explicit terminal EMPTY");
    check(content.query_status().attempted && content.query_status().result == FLY_SESSION_V2_EMPTY, "query completion status observable");
    ContentPortV2::Ref old{};
    if (first.size() >= 153 && second.size() >= 153) {
        std::copy_n(first.data() + 4, 16, old.begin());
        ContentPortV2::Selection one, two;
        ContentPortV2::Ref other{}; std::copy_n(second.data() + 4, 16, other.begin());
        check(old != other, "source refs unique even with deterministic test entropy");
        check(content.resolve(old, &one) && content.resolve(other, &two), "exact selections resolve");
        check(one.payload_hash == two.payload_hash && one.relative_path != two.relative_path &&
              one.variant_id != two.variant_id, "same hash never substitutes exact variant");
        check(one.scope == FLY_SOURCE_SCOPE_MANAGED_LIBRARY && one.source_uuid[0] == 7 &&
              one.physical_size == 16400 && one.payload_size == 16400 && one.policy_revision == 9 &&
              one.zip_raw_name.empty() && one.zip_offset == -1, "capture complete raw source metadata");
        const auto identity = flynes::product::canonical_dual_start_identity_v1();
        const auto name_size = first[55];
        check(first[1] == 2 && first.size() == 152u + name_size &&
              std::memcmp(first.data() + 56 + name_size, identity.core_id.data(), 32) == 0,
              "canonical v2 record uses shared start identity");
    } else check(false, "complete canonical v2 records required");
    content.invalidate_policy();
    ContentPortV2::Selection selection;
    check(!content.resolve(old, &selection), "observed policy invalidation revokes old ref");
    check(content.query_record(1, &last) == FLY_SESSION_V2_UNAVAILABLE, "invalidated batch cannot mix generations");
    snapshot = make_snapshot(1);
    check(content.publish(snapshot, 9) == FLY_SESSION_V2_OK, "policy restored with explicit new publication");
    check(!content.resolve(old, &selection), "restoring same policy cannot resurrect old ref");
    check(content.query_record(1, &last) == FLY_SESSION_V2_STALE, "new publication requires enumeration index zero");
    check(content.query_record(0, &last) == FLY_SESSION_V2_OK, "new batch enumeration begins explicitly");
    ContentPortV2 unavailable([](ContentPortV2::Ref&) { return false; });
    check(unavailable.publish(snapshot, 9) == FLY_SESSION_V2_UNAVAILABLE, "entropy failure rejects batch");
    check(content.publish(snapshot, 0) == FLY_SESSION_V2_UNAVAILABLE, "missing source policy is unavailable");
    fly_catalog_snapshot_release(snapshot);
    snapshot = make_snapshot(0);
    check(content.publish(snapshot, 1) == FLY_SESSION_V2_OK && content.query_record(0, &last) == FLY_SESSION_V2_EMPTY, "genuine empty catalog");
    fly_catalog_snapshot_release(snapshot);
    snapshot = make_snapshot(4097);
    check(content.publish(snapshot, 1) == FLY_SESSION_V2_UNAVAILABLE && content.query_record(0, &last) == FLY_SESSION_V2_UNAVAILABLE,
          "4097 rejects entire batch without truncation");
    fly_catalog_snapshot_release(snapshot);
    auto table = content.port();
    check(table.struct_size == FLY_SESSION_CONTENT_PORT_V2_SIZE && table.abi_version == 2 && table.query && table.cancel,
          "real content provider table");
    content.close();
    check(content.query_record(0, &last) == FLY_SESSION_V2_CLOSED && !content.resolve(old, &selection), "closed source cannot resolve");
    auto retained = std::make_unique<ContentPortV2>(random);
    auto retained_table = retained->port(); retained_table.retain(retained_table.context);
    retained.reset();
    fly_session_op_token_v2 token{};
    token.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE; token.abi_version = 2; token.operation_id = 1;
    token.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE; token.scope.abi_version = 2; token.scope.kind = FLY_SESSION_SCOPE_ENGINE_V2;
    check(retained_table.cancel(retained_table.context, &token) == FLY_SESSION_V2_CLOSED, "retained content context survives owner destruction closed");
    retained_table.release(retained_table.context);
}
}
#ifndef FLYNES_HARMONY_CONTENT_NO_MAIN
int main() { harmony_v2_tests::content_tests(); return harmony_v2_tests::failures ? 1 : 0; }
#endif
