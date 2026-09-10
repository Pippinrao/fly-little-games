#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include <flynes/flynes_app.h>

namespace flynes::harmony::nearby {

enum class SecureStoreBackend
{
    AndroidKeystore = 1,
    IosKeychain = 2,
    HarmonyHuks = 3,
    InMemoryFake = 4
};

struct FriendRecord
{
    std::string contact_id;
    std::array<std::uint8_t, 32> identity_key_id{};
};

class FriendStore
{
public:
    virtual ~FriendStore() = default;
    virtual SecureStoreBackend backend() const = 0;
    virtual fly_result persist(const FriendRecord& record) = 0;
    virtual fly_result load(std::string_view contact_id, FriendRecord* out) const = 0;
};

class InMemoryFriendStore final : public FriendStore
{
public:
    SecureStoreBackend backend() const override;
    fly_result persist(const FriendRecord& record) override;
    fly_result load(std::string_view contact_id, FriendRecord* out) const override;

private:
    std::unordered_map<std::string, FriendRecord> records_;
};

} // namespace flynes::harmony::nearby
