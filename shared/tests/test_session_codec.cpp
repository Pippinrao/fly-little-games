#include "session_codec.hpp"

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
    case Status::UnknownCriticalTag:
        return "unknown_critical";
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
    check(cases >= 30, "expected the full exact-size golden set");

    if (failures != 0)
    {
        std::cerr << "flynes_session_codec: FAIL (" << failures << ")\n";
        return 1;
    }
    std::cout << "flynes_session_codec: PASS (" << cases << " cases)\n";
    return 0;
}
