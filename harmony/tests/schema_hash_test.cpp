#include "schema_registry_hash.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

bool read_all(const std::string& path, std::vector<std::uint8_t>* out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0)
        return false;
    in.seekg(0, std::ios::beg);
    out->assign(static_cast<std::size_t>(end), 0);
    if (end != 0 && !in.read(reinterpret_cast<char*>(out->data()), end))
        return false;
    return true;
}

} // namespace

int main()
{
    const std::string schema_path =
        std::string(FLYNES_HARMONY_SESSION_SCHEMA_DIR) + "/flynes_session_v1.schema";
    const std::string hash_path =
        std::string(FLYNES_HARMONY_SESSION_SCHEMA_DIR) + "/schema_registry_hash.txt";
    std::vector<std::uint8_t> schema;
    std::vector<std::uint8_t> hash_file;
    check(read_all(schema_path, &schema), "Harmony schema copy must exist");
    check(read_all(hash_path, &hash_file), "Harmony schema hash copy must exist");

    std::string published(reinterpret_cast<const char*>(hash_file.data()), hash_file.size());
    while (!published.empty() && (published.back() == '\n' || published.back() == '\r'))
        published.pop_back();
    check(published == FLYNES_SESSION_SCHEMA_REGISTRY_HASH_HEX,
          "Harmony hash copy must match frozen C string");
    check(!schema.empty(), "Harmony schema copy must not be empty");

    if (failures != 0)
    {
        std::fprintf(stderr, "flynes_harmony_session_schema: FAIL\n");
        return 1;
    }
    std::puts("flynes_harmony_session_schema: PASS");
    return 0;
}
