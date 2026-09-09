#include "display_policy.hpp"

#include <algorithm>
#include <cstdlib>

namespace flynes::harmony {
namespace {
constexpr std::int64_t kObservationTtlNs = 2'000'000'000LL;
constexpr std::int32_t kRefreshToleranceMilliHz = 1'000;
}

std::int32_t DisplayPolicy::requested_hz(std::int32_t policy)
{
    switch (policy)
    {
    case 3: return 60;
    case 4: return 90;
    case 5: return 120;
    default: return 0;
    }
}

void DisplayPolicy::begin_surface(std::uint64_t generation)
{
    std::lock_guard lock(mutex_);
    surface_generation_ = generation;
    request_generation_ = 0;
    actual_millihz_ = 0;
    observed_at_ns_ = 0;
    surface_ready_ = generation != 0;
    request_accepted_ = false;
}

void DisplayPolicy::end_surface(std::uint64_t generation)
{
    std::lock_guard lock(mutex_);
    if (generation != surface_generation_) return;
    surface_ready_ = false;
    request_accepted_ = false;
    actual_millihz_ = 0;
    observed_at_ns_ = 0;
}

void DisplayPolicy::configure(std::int32_t refresh_policy,
                              std::int32_t temporal_mode,
                              bool adaptive_protection)
{
    std::lock_guard lock(mutex_);
    refresh_policy_ = std::clamp(refresh_policy, 1, 5);
    temporal_mode_ = std::clamp(temporal_mode, 1, 2);
    adaptive_protection_ = adaptive_protection;
    request_generation_ = 0;
    request_accepted_ = false;
}

void DisplayPolicy::record_request(std::uint64_t generation, bool accepted)
{
    std::lock_guard lock(mutex_);
    if (!surface_ready_ || generation != surface_generation_) return;
    request_generation_ = generation;
    request_accepted_ = accepted;
}

void DisplayPolicy::observe(std::uint64_t generation,
                            std::int32_t actual_millihz,
                            std::int64_t observed_at_ns)
{
    std::lock_guard lock(mutex_);
    if (!surface_ready_ || generation != surface_generation_ ||
        actual_millihz <= 0 || observed_at_ns <= 0)
    {
        return;
    }
    actual_millihz_ = actual_millihz;
    observed_at_ns_ = observed_at_ns;
}

void DisplayPolicy::set_protection(bool thermal_limited, bool low_battery,
                                   bool observation_valid)
{
    std::lock_guard lock(mutex_);
    thermal_limited_ = thermal_limited;
    low_battery_ = low_battery;
    protection_observation_valid_ = observation_valid;
}

DisplayPolicyStatus DisplayPolicy::status(std::int64_t now_ns) const
{
    std::lock_guard lock(mutex_);
    DisplayPolicyStatus result;
    result.surface_ready = surface_ready_;
    result.request_accepted = request_accepted_ && request_generation_ == surface_generation_;
    result.surface_generation = surface_generation_;
    result.request_generation = request_generation_;
    result.refresh_policy = refresh_policy_;
    result.temporal_mode = temporal_mode_;
    result.protection_observation_valid = protection_observation_valid_;
    result.thermal_limited = thermal_limited_;
    result.low_battery = low_battery_;
    result.requested_hz = requested_hz(refresh_policy_);
    result.actual_millihz = actual_millihz_;
    result.observed_at_ns = observed_at_ns_;
    result.observation_valid = surface_ready_ && observed_at_ns_ > 0 && now_ns >= observed_at_ns_ &&
        now_ns - observed_at_ns_ <= kObservationTtlNs;

    const std::int32_t observed_hz = result.observation_valid
        ? std::max(1, (actual_millihz_ + 500) / 1000) : 60;
    result.effective_hz = observed_hz;

    if (!surface_ready_)
    {
        result.effective_hz = 60;
        result.fallback_reason = "surface unavailable";
        return result;
    }
    if (adaptive_protection_ && !protection_observation_valid_)
    {
        result.effective_hz = 60;
        result.fallback_reason = "protection observation unavailable";
        return result;
    }
    if (adaptive_protection_ && thermal_limited_)
    {
        result.effective_hz = 60;
        result.fallback_reason = "thermal protection active";
        return result;
    }
    if (adaptive_protection_ && low_battery_)
    {
        result.effective_hz = 60;
        result.fallback_reason = "low battery protection active";
        return result;
    }
    if (!result.request_accepted)
    {
        result.effective_hz = 60;
        result.fallback_reason = "display request not applied";
        return result;
    }
    if (!result.observation_valid)
    {
        result.effective_hz = 60;
        result.fallback_reason = "display observation stale";
        return result;
    }
    if (result.requested_hz > 0 &&
        std::abs(actual_millihz_ - result.requested_hz * 1000) > kRefreshToleranceMilliHz)
    {
        result.fallback_reason = "display mode rejected";
        return result;
    }
    result.motion_qualified = temporal_mode_ == 2 && result.effective_hz == 120 &&
        std::abs(actual_millihz_ - 120'000) <= kRefreshToleranceMilliHz;
    return result;
}

} // namespace flynes::harmony
