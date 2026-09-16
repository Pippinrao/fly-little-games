#include "durable_root_store.hpp"

#include "../wire/sha256.hpp"

namespace flynes::session::recovery {
namespace {

bool hash_equal(const std::array<std::uint8_t, 32>& a,
                const std::array<std::uint8_t, 32>& b) noexcept
{
    return a == b;
}

} // namespace

fly_session_result_v2 DurableRootStoreV1::put_immutable(
    const std::uint8_t* bytes, std::size_t size,
    std::array<std::uint8_t, 32>* out_hash) noexcept
{
    if (bytes == nullptr && size != 0)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (out_hash == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    try
    {
        const auto hash = wire::sha256(bytes, size);
        pending_[hash] = std::vector<std::uint8_t>(bytes, bytes + size);
        *out_hash = hash;
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
}

fly_session_result_v2 DurableRootStoreV1::flush() noexcept
{
    if (crash_ == CrashPointV1::BeforeFlush)
        return FLY_SESSION_V2_UNAVAILABLE;
    try
    {
        for (auto& item : pending_)
            durable_[item.first] = item.second;
        pending_.clear();
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
}

fly_session_result_v2 DurableRootStoreV1::cas_replace(
    std::uint64_t expected_revision,
    const std::array<std::uint8_t, 32>& expected_hash,
    const std::uint8_t* new_bytes, std::size_t new_size) noexcept
{
    if (new_bytes == nullptr && new_size != 0)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (root_)
    {
        if (root_->revision != expected_revision ||
            !hash_equal(root_->hash, expected_hash))
            return FLY_SESSION_V2_STALE;
    }
    else if (expected_revision != 0)
        return FLY_SESSION_V2_STALE;

    std::array<std::uint8_t, 32> new_hash{};
    const auto put = put_immutable(new_bytes, new_size, &new_hash);
    if (put != FLY_SESSION_V2_OK)
        return put;
    const auto flushed = flush();
    if (flushed != FLY_SESSION_V2_OK)
        return flushed;
    if (crash_ == CrashPointV1::AfterFlushBeforeCas)
        return FLY_SESSION_V2_UNAVAILABLE;
    try
    {
        RootViewV1 next{};
        next.revision = root_ ? root_->revision + 1u : 1u;
        next.hash = new_hash;
        next.bytes.assign(new_bytes, new_bytes + new_size);
        root_ = std::move(next);
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
}

fly_session_result_v2 DurableRootStoreV1::read_root(RootViewV1* out) const
    noexcept
{
    if (out == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (!root_)
        return FLY_SESSION_V2_UNAVAILABLE;
    try
    {
        *out = *root_;
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
}

fly_session_result_v2 DurableRootStoreV1::read_object(
    const std::array<std::uint8_t, 32>& hash, std::vector<std::uint8_t>* out)
    const noexcept
{
    if (out == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    const auto found = durable_.find(hash);
    if (found == durable_.end())
        return FLY_SESSION_V2_UNAVAILABLE;
    try
    {
        *out = found->second;
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
}

void DurableRootStoreV1::restart() noexcept
{
    pending_.clear();
    crash_ = CrashPointV1::None;
}

} // namespace flynes::session::recovery
