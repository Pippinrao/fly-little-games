#include "platform/RomPackage.hpp"
#include "catalog/content_identity.hpp"
#include "catalog/bounded_zip_archive.hpp"
#include <flynes/flynes_app.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <iterator>

using namespace flynes::catalog;

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static std::string hash(const std::vector<std::uint8_t>& bytes) {
    return sha256_hex({bytes.data(), bytes.size()}).value;
}
static std::string hex(const std::vector<std::uint8_t>& bytes) {
    std::string result;
    for (auto byte : bytes) { result += "0123456789ABCDEF"[byte >> 4]; result += "0123456789ABCDEF"[byte & 15]; }
    return result;
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "ZIP fixture path required");
        const std::string source = "source:1:00112233445566778899AABBCCDDEEFF";
        const std::string path = "folder/game.zip";
        const auto pkg = package_id(source, path).value;
        std::vector<std::uint8_t> raw{1, 2, 3, 4};
        auto raw_id = variant_id(pkg, RAW_LOCATOR, hash(raw)).value;
        auto resolved = flynes::ios::resolve_rom_package(raw.data(), raw.size(), FLY_PACKAGE_FORMAT_RAW,
            source, path, raw_id, hash(raw), hash(raw));
        check(resolved == raw, "RAW payload must round-trip");
        bool rejected = false;
        try {
            auto changed = raw; changed[0] ^= 1;
            flynes::ios::resolve_rom_package(changed.data(), changed.size(), FLY_PACKAGE_FORMAT_RAW,
                source, path, raw_id, hash(raw), hash(raw));
        } catch (const std::exception&) { rejected = true; }
        check(rejected, "changed package must be rejected");
        std::ifstream input(argv[1], std::ios::binary);
        std::vector<std::uint8_t> zip{std::istreambuf_iterator<char>(input), {}};
        auto opened = open_bounded_zip(zip.data(), zip.size(), BoundedZipLimits::defaults());
        check(opened.succeeded(), "test ZIP must parse");
        const auto& archive = *opened.archive();
        check(archive.entries().size() == 2, "fixture contains duplicate names with different content");
        for (const auto& entry : archive.entries()) {
            auto payload = read_bounded_zip_payload(archive, entry.raw_name, entry.local_header_offset);
            check(payload.succeeded(), "fixture payload must inflate");
            const auto& bytes = *payload.payload();
            auto id = variant_id(pkg, hex(entry.raw_name) + "@" + std::to_string(entry.local_header_offset), hash(bytes)).value;
            auto actual = flynes::ios::resolve_rom_package(zip.data(), zip.size(), FLY_PACKAGE_FORMAT_ZIP,
                source, path, id, hash(bytes), hash(zip));
            check(actual == bytes, "must open exact entry, not the first matching filename");
            rejected = false;
            try {
                flynes::ios::resolve_rom_package(zip.data(), zip.size(), FLY_PACKAGE_FORMAT_ZIP,
                    source, "wrong-path.zip", id, hash(bytes), hash(zip));
            } catch (const std::exception&) { rejected = true; }
            check(rejected, "wrong package locator must be rejected");
        }
        std::cout << "ios_rom_package: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ios_rom_package: FAIL: " << error.what() << '\n';
        return 1;
    }
}
