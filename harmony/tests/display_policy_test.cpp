#include "display_policy.hpp"

#include <cassert>
#include <cstdint>
#include <string>

using flynes::harmony::DisplayPolicy;

int main()
{
    DisplayPolicy policy;
    constexpr std::uint64_t generation = 7;
    constexpr std::int64_t now = 10'000'000'000LL;

    policy.begin_surface(generation);
    policy.configure(5, 2, true); // 120 Hz + Motion + adaptive protection.
    auto requested = policy.status(now);
    assert(requested.requested_hz == 120);
    assert(requested.effective_hz == 60);
    assert(!requested.observation_valid);
    assert(requested.fallback_reason == "protection observation unavailable");

    policy.set_protection(false, false);
    requested = policy.status(now);
    assert(requested.fallback_reason == "display request not applied");

    policy.record_request(generation, true);
    policy.observe(generation, 120'000, now - 100'000'000LL);
    auto qualified = policy.status(now);
    assert(qualified.request_accepted);
    assert(qualified.observation_valid);
    assert(qualified.effective_hz == 120);
    assert(qualified.motion_qualified);
    assert(qualified.fallback_reason.empty());

    policy.observe(generation - 1, 120'000, now);
    assert(policy.status(now).effective_hz == 120); // stale generation ignored.

    auto expired = policy.status(now + 2'100'000'000LL);
    assert(!expired.observation_valid);
    assert(expired.effective_hz == 60);
    assert(expired.fallback_reason == "display observation stale");

    policy.observe(generation, 90'000, now + 2'100'000'000LL);
    auto rejected = policy.status(now + 2'100'000'000LL);
    assert(rejected.effective_hz == 90);
    assert(!rejected.motion_qualified);
    assert(rejected.fallback_reason == "display mode rejected");

    policy.set_protection(true, false);
    auto thermal = policy.status(now + 2'100'000'000LL);
    assert(thermal.thermal_limited);
    assert(!thermal.low_battery);
    assert(thermal.effective_hz == 60);
    assert(thermal.fallback_reason == "thermal protection active");

    policy.set_protection(false, true);
    auto battery = policy.status(now + 2'100'000'000LL);
    assert(!battery.thermal_limited);
    assert(battery.low_battery);
    assert(battery.effective_hz == 60);
    assert(battery.fallback_reason == "low battery protection active");

    policy.set_protection(false, false);
    policy.configure(1, 1, true); // Follow system never claims a specific request.
    policy.record_request(generation, true);
    policy.observe(generation, 90'000, now + 2'200'000'000LL);
    auto follow = policy.status(now + 2'200'000'000LL);
    assert(follow.requested_hz == 0);
    assert(follow.effective_hz == 90);
    assert(follow.fallback_reason.empty());

    policy.set_protection(false, false, false);
    policy.configure(5, 2, true);
    auto unavailable = policy.status(now + 2'200'000'000LL);
    assert(!unavailable.protection_observation_valid);
    assert(unavailable.effective_hz == 60);
    assert(unavailable.fallback_reason == "protection observation unavailable");

    policy.end_surface(generation);
    auto ended = policy.status(now + 2'200'000'000LL);
    assert(!ended.surface_ready);
    assert(!ended.observation_valid);
    return 0;
}
