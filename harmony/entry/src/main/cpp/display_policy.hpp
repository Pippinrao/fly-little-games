#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace flynes::harmony {

struct DisplayPolicyStatus final
{
    bool surface_ready = false;
    bool request_accepted = false;
    bool observation_valid = false;
    bool motion_qualified = false;
    bool protection_observation_valid = false;
    bool thermal_limited = false;
    bool low_battery = false;
    std::uint64_t surface_generation = 0;
    std::uint64_t request_generation = 0;
    std::int32_t refresh_policy = 1;
    std::int32_t temporal_mode = 1;
    std::int32_t requested_hz = 0;
    std::int32_t actual_millihz = 0;
    std::int32_t effective_hz = 60;
    std::int64_t observed_at_ns = 0;
    std::string fallback_reason;
};

// Keeps platform requests and measured display state separate.  The class is
// platform-free so expiry, generation and protection behavior can be host tested.
class DisplayPolicy final
{
public:
    void begin_surface(std::uint64_t generation);
    void end_surface(std::uint64_t generation);
    void configure(std::int32_t refresh_policy,
                   std::int32_t temporal_mode,
                   bool adaptive_protection);
    void record_request(std::uint64_t generation, bool accepted);
    void observe(std::uint64_t generation,
                 std::int32_t actual_millihz,
                 std::int64_t observed_at_ns);
    void set_protection(bool thermal_limited, bool low_battery,
                        bool observation_valid = true);
    DisplayPolicyStatus status(std::int64_t now_ns) const;

private:
    static std::int32_t requested_hz(std::int32_t policy);

    mutable std::mutex mutex_;
    std::uint64_t surface_generation_ = 0;
    std::uint64_t request_generation_ = 0;
    std::int32_t refresh_policy_ = 1;
    std::int32_t temporal_mode_ = 1;
    std::int32_t actual_millihz_ = 0;
    std::int64_t observed_at_ns_ = 0;
    bool surface_ready_ = false;
    bool request_accepted_ = false;
    bool adaptive_protection_ = true;
    bool protection_observation_valid_ = false;
    bool thermal_limited_ = false;
    bool low_battery_ = false;
};

} // namespace flynes::harmony
