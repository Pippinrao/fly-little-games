#include "friend_store.hpp"
#include "nearby_adapter.hpp"
#include "nearby_dtos.hpp"

#include <flynes/flynes_session.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::vector<std::uint8_t> read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0)
    {
        return {};
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input)
    {
        return {};
    }
    return bytes;
}

std::string read_text(const char* path)
{
    const std::vector<std::uint8_t> bytes = read_file(path);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

fly_session_t* create_session()
{
    fly_session_config config{};
    config.struct_size = FLY_SESSION_CONFIG_V1_SIZE;
    config.version = FLY_SESSION_CONFIG_VERSION_1;
    fly_session_t* session = nullptr;
    expect(fly_session_create(&config, &session) == FLY_RESULT_OK, "session create");
    expect(session != nullptr, "session handle");
    return session;
}

void test_poll_maps_none_and_executes_platform_commands()
{
    fly_session_t* session = create_session();
    flynes::harmony::nearby::InMemoryFriendStore store;
    flynes::harmony::nearby::NearbyAdapter adapter(session, &store);

    fly_session_command command{};
    command.struct_size = FLY_SESSION_COMMAND_V1_SIZE;
    command.version = FLY_SESSION_COMMAND_VERSION_1;
    expect(fly_session_poll_command(session, &command) == FLY_RESULT_OK, "poll");
    expect(adapter.map_polled_kind(command.kind) ==
               flynes::harmony::nearby::NearbyCommandKind::None,
           "stub reducer poll maps to None and does not start radios");

    const flynes::harmony::nearby::NearbyCommandKind kinds[] = {
        flynes::harmony::nearby::NearbyCommandKind::StartDiscovery,
        flynes::harmony::nearby::NearbyCommandKind::StopDiscovery,
        flynes::harmony::nearby::NearbyCommandKind::ShowQr,
        flynes::harmony::nearby::NearbyCommandKind::ConfirmSas,
        flynes::harmony::nearby::NearbyCommandKind::CreateBearer,
        flynes::harmony::nearby::NearbyCommandKind::OpenQuic,
        flynes::harmony::nearby::NearbyCommandKind::PersistFriend,
    };
    for (const auto kind : kinds)
    {
        const flynes::harmony::nearby::NearbyCommandResult result = adapter.execute(kind);
        expect(result.kind == kind, "execute echoes the requested adapter command");
        expect(result.submitted_event, "adapter submits a pairing/confirm event");
        expect(result.session_result == FLY_RESULT_INVALID_STATE,
               "shared stub may reject the event; adapter still must not parse BLE DTOs");
    }

    fly_session_destroy(session);
}

void test_host_stream_encoder_is_unsupported_on_host()
{
    fly_session_t* session = create_session();
    flynes::harmony::nearby::InMemoryFriendStore store;
    flynes::harmony::nearby::NearbyAdapter adapter(session, &store);

    const flynes::harmony::nearby::NearbyCommandResult result =
        adapter.execute(flynes::harmony::nearby::NearbyCommandKind::StartHostStreamEncoder);
    expect(result.result == FLY_RESULT_UNSUPPORTED_VERSION,
           "HOST_STREAM encoder start is an unsupported host stub");
    expect(!result.submitted_event, "encoder stub must not pretend a media command completed");

    fly_session_destroy(session);
}

void test_fake_transport_forwards_channel_bind_without_seats()
{
    fly_session_t* session = create_session();
    flynes::harmony::nearby::InMemoryFriendStore store;
    flynes::harmony::nearby::NearbyAdapter adapter(session, &store);

    const std::string golden = std::string(FLYNES_SESSION_GOLDEN_DIR) +
                               "/channel_bind_v1_initial/legal.bin";
    const std::vector<std::uint8_t> bytes = read_file(golden.c_str());
    expect(!bytes.empty(), "ChannelBindV1 golden bytes are readable");

    const fly_result delivered =
        adapter.deliver_stream(FLY_SESSION_QUIC_CONTROL, bytes.data(), bytes.size());
    expect(delivered == FLY_RESULT_INVALID_STATE,
           "adapter forwards golden Control bytes even if the reducer is a stub");
    expect(!adapter.interpreted_seats(),
           "adapter must not decode seats, mode, or ChannelBind fields");

    fly_session_destroy(session);
}

void test_in_memory_friend_store_round_trip()
{
    flynes::harmony::nearby::InMemoryFriendStore store;
    expect(store.backend() == flynes::harmony::nearby::SecureStoreBackend::InMemoryFake,
           "host tests use the fake store, not Keystore/Keychain/HUKS");

    flynes::harmony::nearby::FriendRecord record{};
    record.contact_id = "friend-1";
    record.identity_key_id.fill(0x11u);
    expect(store.persist(record) == FLY_RESULT_OK, "persist friend");

    flynes::harmony::nearby::FriendRecord loaded{};
    expect(store.load("friend-1", &loaded) == FLY_RESULT_OK, "load friend");
    expect(loaded.contact_id == record.contact_id, "loaded contact id");
    expect(loaded.identity_key_id == record.identity_key_id, "loaded key id");
}

void test_ble_dtos_stay_in_platform_trees()
{
    const std::string session = read_text(FLYNES_SHARED_SESSION_HEADER);
    const std::string runtime = read_text(FLYNES_SHARED_RUNTIME_HEADER);
    const std::string app = read_text(FLYNES_SHARED_APP_HEADER);
    const std::string shared = session + runtime + app;
    expect(shared.find("NearbyBleAdvertisementDto") == std::string::npos,
           "shared public C ABI must not contain BLE advertisement DTOs");
    expect(shared.find("NearbyQrInviteDto") == std::string::npos,
           "shared public C ABI must not contain QR invite DTOs");
    expect(shared.find("NearbyWifiBearerDto") == std::string::npos,
           "shared public C ABI must not contain Wi-Fi bearer DTOs");
    expect(shared.find("GATT") == std::string::npos,
           "shared public C ABI must not contain GATT types");

    const std::string android = read_text(FLYNES_ANDROID_NEARBY_DTOS);
    const std::string ios = read_text(FLYNES_IOS_NEARBY_DTOS);
    const std::string harmony = read_text(FLYNES_HARMONY_NEARBY_DTOS);
    for (const char* label : {"NearbyBleAdvertisementDto", "NearbyQrInviteDto",
                              "NearbyWifiBearerDto"})
    {
        expect(android.find(label) != std::string::npos, "Android nearby DTOs");
        expect(ios.find(label) != std::string::npos, "iOS nearby DTOs");
        expect(harmony.find(label) != std::string::npos, "Harmony nearby DTOs");
    }

    expect(sizeof(flynes::harmony::nearby::NearbyBleAdvertisementDto) > 0u,
           "Harmony BLE DTO is a real platform struct");
    expect(flynes::harmony::nearby::kBleServiceUuid.size() == 16u,
           "BLE service UUID stays in the platform adapter");
}

} // namespace

int main()
{
    test_poll_maps_none_and_executes_platform_commands();
    test_host_stream_encoder_is_unsupported_on_host();
    test_fake_transport_forwards_channel_bind_without_seats();
    test_in_memory_friend_store_round_trip();
    test_ble_dtos_stay_in_platform_trees();

    if (failures != 0)
    {
        std::fprintf(stderr, "flynes_harmony_nearby_adapter_test: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("PASS: Harmony nearby adapter host contract");
    return 0;
}
