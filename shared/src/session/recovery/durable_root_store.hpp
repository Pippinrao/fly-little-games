#ifndef FLYNES_SESSION_RECOVERY_DURABLE_ROOT_STORE_HPP
#define FLYNES_SESSION_RECOVERY_DURABLE_ROOT_STORE_HPP

/*
 * Task 12 / REC-DUAL step 3: ObjectStore root / WAL atomic switch.
 *
 * Immutable children are written first; a guarded replace then switches the
 * manifest. Failure keeps the previous whole group. A mixed revisionB +
 * hashA publication is forbidden. Crash injection drops unflushed bytes.
 */

#include "flynes/flynes_session.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace flynes::session::recovery {

enum class CrashPointV1 : std::uint8_t
{
    None = 0,
    BeforeFlush = 1,
    AfterFlushBeforeCas = 2,
    AfterCasBeforeAck = 3
};

struct RootViewV1 final
{
    std::uint64_t revision = 0;
    std::array<std::uint8_t, 32> hash{};
    std::vector<std::uint8_t> bytes{};
};

class DurableRootStoreV1 final
{
public:
    fly_session_result_v2 put_immutable(const std::uint8_t* bytes,
                                        std::size_t size,
                                        std::array<std::uint8_t, 32>* out_hash)
        noexcept;
    fly_session_result_v2 flush() noexcept;
    fly_session_result_v2 cas_replace(
        std::uint64_t expected_revision,
        const std::array<std::uint8_t, 32>& expected_hash,
        const std::uint8_t* new_bytes, std::size_t new_size) noexcept;
    fly_session_result_v2 read_root(RootViewV1* out) const noexcept;
    fly_session_result_v2 read_object(const std::array<std::uint8_t, 32>& hash,
                                      std::vector<std::uint8_t>* out) const
        noexcept;

    void inject_crash(CrashPointV1 point) noexcept { crash_ = point; }
    void restart() noexcept;

    [[nodiscard]] std::uint64_t revision() const noexcept
    {
        return root_ ? root_->revision : 0;
    }
    [[nodiscard]] bool has_root() const noexcept { return root_.has_value(); }
    [[nodiscard]] std::size_t durable_object_count() const noexcept
    {
        return durable_.size();
    }

private:
    using Hash = std::array<std::uint8_t, 32>;
    std::map<Hash, std::vector<std::uint8_t>> durable_{};
    std::map<Hash, std::vector<std::uint8_t>> pending_{};
    std::optional<RootViewV1> root_{};
    CrashPointV1 crash_ = CrashPointV1::None;
};

} // namespace flynes::session::recovery

#endif
