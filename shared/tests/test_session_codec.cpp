#include "session_codec.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using flynes::session::wire::QuicChannel;
using flynes::session::wire::Status;

namespace {

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool read_all(const fs::path& path, std::vector<std::uint8_t>* out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0)
        return false;
    in.seekg(0, std::ios::beg);
    out->assign(static_cast<std::size_t>(end), 0);
    if (end != 0 && !in.read(reinterpret_cast<char*>(out->data()), end))
        return false;
    return true;
}

std::string read_text(const fs::path& path)
{
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    return line;
}

std::string hex_of(const std::uint8_t hash[32])
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i)
    {
        out[static_cast<std::size_t>(i) * 2u] = digits[hash[i] >> 4u];
        out[static_cast<std::size_t>(i) * 2u + 1u] = digits[hash[i] & 0x0Fu];
    }
    return out;
}

const char* status_name(Status status)
{
    switch (status)
    {
    case Status::Ok:
        return "ok";
    case Status::Truncated:
        return "truncated";
    case Status::Trailing:
        return "trailing";
    case Status::UnknownEnum:
        return "unknown_enum";
    case Status::NonzeroReserved:
        return "nonzero_reserved";
    case Status::UnknownKind:
        return "unknown_kind";
    case Status::UnknownCriticalTag:
        return "unknown_critical";
    case Status::InvalidField:
        return "invalid_field";
    default:
        return "other";
    }
}

void expect_status(const fs::path& file, const std::string& type, Status expected)
{
    std::vector<std::uint8_t> bytes;
    if (!read_all(file, &bytes))
    {
        check(false, file.string() + " missing");
        return;
    }
    std::uint8_t hash[32]{};
    const Status got = flynes::session::wire::check(
        type.c_str(), bytes.empty() ? nullptr : bytes.data(), bytes.size(), hash);
    check(got == expected, file.string() + " expected " + status_name(expected) +
                               " got " + status_name(got));
}

void test_case(const fs::path& dir)
{
    const std::string type = read_text(dir / "type.txt");
    check(!type.empty(), dir.string() + " missing type");
    const bool negative_only = fs::exists(dir / "negative_only.txt");
    if (negative_only)
    {
        expect_status(dir / "legal.bin", type, Status::UnknownCriticalTag);
        return;
    }

    std::vector<std::uint8_t> legal;
    check(read_all(dir / "legal.bin", &legal), dir.string() + " legal.bin");
    std::uint8_t hash[32]{};
    const Status got = flynes::session::wire::check(
        type.c_str(), legal.data(), legal.size(), hash);
    check(got == Status::Ok, dir.string() + " legal should decode");
    const std::string published = read_text(dir / "legal.hash");
    check(hex_of(hash) == published, dir.string() + " hash mismatch");

    expect_status(dir / "truncate.bin", type, Status::Truncated);
    expect_status(dir / "trailing.bin", type, Status::Trailing);
    if (!fs::exists(dir / "skip_enum.txt"))
        expect_status(dir / "unknown_enum.bin", type, Status::UnknownEnum);
    if (!fs::exists(dir / "skip_reserved.txt"))
        expect_status(dir / "nonzero_reserved.bin", type, Status::NonzeroReserved);
}

std::vector<std::uint8_t> pending_config_confirm_legal()
{
    std::vector<std::uint8_t> bytes(44u, 0u);
    bytes[1] = 1u;
    for (std::size_t i = 0; i < 32u; ++i)
        bytes[4u + i] = static_cast<std::uint8_t>(0xA1u + i);
    bytes[43] = 1u;
    return bytes;
}

void expect_check(const std::vector<std::uint8_t>& bytes, Status expected, const char* message)
{
    std::uint8_t hash[32]{};
    const Status got = flynes::session::wire::check(
        "0x0218", bytes.empty() ? nullptr : bytes.data(), bytes.size(), hash);
    check(got == expected, std::string(message) + " expected " + status_name(expected) +
                               " got " + status_name(got));
}

void pending_config_confirm_v1_codec()
{
    const auto legal = pending_config_confirm_legal();
    check(legal.size() == 44u, "PendingConfigConfirmV1 is 44 bytes");
    std::uint8_t hash[32]{};
    const Status got = flynes::session::wire::check("0x0218", legal.data(), legal.size(), hash);
    check(got == Status::Ok, "legal PendingConfigConfirmV1 decodes");
    const auto expected = flynes::session::wire::domain_hash(
        "flynes-pending-config-confirm-v1", legal.data(), legal.size());
    check(std::memcmp(hash, expected.data(), 32u) == 0,
          "PendingConfigConfirmV1 hashes under flynes-pending-config-confirm-v1");

    auto truncated = legal;
    truncated.pop_back();
    expect_check(truncated, Status::Truncated, "43-byte confirm");
    auto trailing = legal;
    trailing.push_back(0u);
    expect_check(trailing, Status::Trailing, "45-byte confirm");

    auto bad_version = legal;
    bad_version[1] = 2u;
    expect_check(bad_version, Status::InvalidField, "confirm version != 1");
    auto reserved = legal;
    reserved[2] = 1u;
    expect_check(reserved, Status::NonzeroReserved, "confirm reserved nonzero");
    auto zero_id = legal;
    std::fill(zero_id.begin() + 4, zero_id.begin() + 36, std::uint8_t{0});
    expect_check(zero_id, Status::InvalidField, "zero pending_config_id");
    auto zero_revision = legal;
    std::fill(zero_revision.begin() + 36, zero_revision.end(), std::uint8_t{0});
    expect_check(zero_revision, Status::InvalidField, "zero pending_config_revision");
}

void session_signing_binding_rejects_semantic_mutations(const fs::path& root)
{
    std::vector<std::uint8_t> legal;
    check(read_all(root / "session_signing_key_binding_v1" / "legal.bin",
                   &legal) && legal.size() == 312,
          "session signing binding golden is exact 312 bytes");
    if (legal.size() != 312) return;
    const auto rejected = [&legal](std::size_t offset,
                                   std::uint8_t value,
                                   const char* message) {
        auto mutated = legal;
        mutated[offset] = value;
        std::uint8_t hash[32]{};
        check(flynes::session::wire::check(
                  "0x0212", mutated.data(), mutated.size(), hash) !=
                  Status::Ok,
              message);
    };
    auto zero_transcript = legal;
    std::fill(zero_transcript.begin() + 8, zero_transcript.begin() + 40,
              std::uint8_t{0});
    std::uint8_t hash[32]{};
    check(flynes::session::wire::check(
              "0x0212", zero_transcript.data(), zero_transcript.size(), hash) !=
              Status::Ok,
          "zero pair transcript is rejected");
    auto zero_session = legal;
    std::fill(zero_session.begin() + 40, zero_session.begin() + 56,
              std::uint8_t{0});
    check(flynes::session::wire::check(
              "0x0212", zero_session.data(), zero_session.size(), hash) !=
              Status::Ok,
          "zero session id is rejected");
    rejected(65, 0, "nested identity verifier version is checked");
    rejected(66, 1, "nested identity verifier reserved bytes are checked");
    rejected(72, static_cast<std::uint8_t>(legal[72] ^ 1),
             "nested identity key id is recomputed");
    rejected(104, 2, "compressed identity point is rejected");
    rejected(169, 1, "nested identity tail padding is checked");
    rejected(176, 2, "compressed session signing point is rejected");

    auto duplicate_key = legal;
    std::copy_n(duplicate_key.begin() + 104, 65,
                duplicate_key.begin() + 176);
    check(flynes::session::wire::check(
              "0x0212", duplicate_key.data(), duplicate_key.size(), hash) !=
              Status::Ok,
          "session signing key cannot equal the long-term identity key");

    auto high_s = legal;
    std::fill(high_s.begin() + 280, high_s.end(), std::uint8_t{0xff});
    check(flynes::session::wire::check(
              "0x0212", high_s.data(), high_s.size(), hash) != Status::Ok,
          "non-canonical high-S identity signature is rejected");

    auto out_of_range_r = legal;
    std::fill(out_of_range_r.begin() + 248,
              out_of_range_r.begin() + 280, std::uint8_t{0xff});
    check(flynes::session::wire::check(
              "0x0212", out_of_range_r.data(), out_of_range_r.size(), hash) !=
              Status::Ok,
          "out-of-range P-256 signature r is rejected");
}

} // namespace

int main()
{
    check(static_cast<int>(QuicChannel::Control) == 1, "QUIC Control mapping");
    check(static_cast<int>(QuicChannel::Input) == 2, "QUIC Input mapping");
    check(static_cast<int>(QuicChannel::StateCommit) == 3, "QUIC StateCommit mapping");
    check(static_cast<int>(QuicChannel::Bulk) == 4, "QUIC Bulk mapping");
    check(static_cast<int>(QuicChannel::Rom) == 5, "QUIC ROM mapping");
    check(static_cast<int>(QuicChannel::Video) == 6, "QUIC Video mapping");
    check(static_cast<int>(QuicChannel::Audio) == 7, "QUIC Audio mapping");

    std::uint8_t cursor[16]{};
    std::size_t written = 0u;
    check(flynes::session::wire::encode_frame_cursor(0, 0, cursor, &written) == Status::Ok,
          "encode GENESIS");
    check(written == 16u && cursor[0] == 0, "GENESIS wire kind");

    pending_config_confirm_v1_codec();

    const fs::path root(FLYNES_SESSION_GOLDEN_DIR);
    check(fs::exists(root / "manifest.json"), "golden manifest exists");
    int cases = 0;
    for (const auto& entry : fs::directory_iterator(root))
    {
        if (!entry.is_directory())
            continue;
        test_case(entry.path());
        ++cases;
    }
    session_signing_binding_rejects_semantic_mutations(root);
    check(cases >= 30, "expected the full exact-size golden set");

    if (failures != 0)
    {
        std::cerr << "flynes_session_codec: FAIL (" << failures << ")\n";
        return 1;
    }
    std::cout << "flynes_session_codec: PASS (" << cases << " cases)\n";
    return 0;
}
