#pragma once

#include "render_mailbox.hpp"
#include "display_policy.hpp"
#include "motion_frame_scheduler.hpp"

#include <cstdint>
#include <mutex>
#include <string>

#include "napi/native_api.h"

struct OH_NativeXComponent;

namespace flynes::harmony {

struct HarmonyRenderStatus final
{
    RenderMailboxStatus mailbox;
    bool component_bound = false;
    bool native_ready = false;
    bool fallback_active = false;
    bool timing_valid = false;
    DisplayPolicyStatus display;
    std::int32_t requested_spatial = 1;
    std::int32_t effective_spatial = 1;
    std::int32_t requested_post = 1;
    std::int64_t vsync_period_ns = 0;
    bool gpu_timing_valid = false;
    std::int64_t gpu_time_ns = 0;
    std::int64_t gpu_time_max_ns = 0;
    std::uint64_t gpu_timing_samples = 0;
    MotionSchedulerStatus motion;
    std::string temporal_state = "NATIVE";
    std::string temporal_fallback_reason;
    std::string fallback_reason;
};

class HarmonyRenderer final
{
public:
    HarmonyRenderer();
    ~HarmonyRenderer();
    HarmonyRenderer(const HarmonyRenderer&) = delete;
    HarmonyRenderer& operator=(const HarmonyRenderer&) = delete;

    bool bind_component(napi_env env, napi_value exports);
    bool submit_frame(std::uint64_t frame_index,
                      std::uint32_t width,
                      std::uint32_t height,
                      const std::vector<std::uint8_t>& rgb565);
    void configure(std::int32_t refresh_policy,
                   std::int32_t temporal_mode,
                   std::int32_t spatial,
                   std::int32_t post,
                   bool adaptive_protection);
    void set_paused(bool paused);
    void set_protection(bool thermal_limited, bool low_battery,
                        bool observation_valid);
    HarmonyRenderStatus status() const;

private:
    struct Impl;
    Impl* impl_;
};

HarmonyRenderer& harmony_renderer();

} // namespace flynes::harmony
