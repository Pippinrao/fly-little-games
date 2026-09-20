#include <flynes/flynes_session.h>

#include <array>
#include <cstdint>
#include <cstdio>

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

} // namespace

int main()
{
    const std::array<std::uint8_t, 5> source{{1, 2, 3, 4, 5}};
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 bytes{
        source.data(), static_cast<std::uint32_t>(source.size()), 0};
    check(fly_session_buffer_create_copy_v2(bytes, &buffer) ==
                  FLY_SESSION_V2_OK &&
              buffer != nullptr,
          "provider creates an immutable public buffer from a copied range");
    fly_session_buffer_v2_t* rejected = buffer;
    auto invalid = bytes;
    invalid.reserved_zero = 1;
    check(fly_session_buffer_create_copy_v2(invalid, &rejected) ==
                  FLY_SESSION_V2_INVALID_ARGUMENT &&
              rejected == nullptr,
          "buffer factory rejects non-zero reserved input without an output");

    std::uint64_t size = 0;
    check(fly_session_buffer_size_v2(buffer, &size) == FLY_SESSION_V2_OK &&
              size == source.size(),
          "buffer reports its exact 64-bit size");

    std::array<std::uint8_t, 8> destination{};
    fly_session_write_bytes_v2 output{};
    output.data = destination.data();
    output.capacity = 2;
    std::uint64_t written = 99;
    check(fly_session_buffer_read_v2(buffer, 1, output, &written) ==
                  FLY_SESSION_V2_OK &&
              written == 2 && destination[0] == 2 && destination[1] == 3,
          "read copies min(capacity, remaining) bytes from offset");

    output.capacity = destination.size();
    check(fly_session_buffer_read_v2(buffer, source.size(), output, &written) ==
                  FLY_SESSION_V2_OK &&
              written == 0,
          "read at end is an empty success");
    check(fly_session_buffer_read_v2(buffer, source.size() + 1, output, &written) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "read beyond end is rejected");

    output.data = nullptr;
    output.capacity = 1;
    check(fly_session_buffer_read_v2(buffer, 0, output, &written) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "non-empty write requires storage");
    check(fly_session_buffer_size_v2(nullptr, &size) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "null handle is rejected");

    fly_session_buffer_retain_v2(buffer);
    fly_session_buffer_release_v2(buffer);
    check(fly_session_buffer_size_v2(buffer, &size) == FLY_SESSION_V2_OK,
          "retained handle survives a balanced release");
    fly_session_buffer_release_v2(buffer);

    return failures == 0 ? 0 : 1;
}
