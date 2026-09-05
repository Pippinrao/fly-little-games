#include "friend_store.hpp"

namespace flynes::harmony::nearby {

SecureStoreBackend InMemoryFriendStore::backend() const
{
    return SecureStoreBackend::InMemoryFake;
}

fly_result InMemoryFriendStore::persist(const FriendRecord& record)
{
    if (record.contact_id.empty())
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    records_[record.contact_id] = record;
    return FLY_RESULT_OK;
}

fly_result InMemoryFriendStore::load(std::string_view contact_id, FriendRecord* out) const
{
    if (out == nullptr || contact_id.empty())
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    const auto found = records_.find(std::string(contact_id));
    if (found == records_.end())
    {
        return FLY_RESULT_OUT_OF_RANGE;
    }
    *out = found->second;
    return FLY_RESULT_OK;
}

} // namespace flynes::harmony::nearby
