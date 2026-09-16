#ifndef FLYNES_SESSION_DUAL_DUAL_RUNTIME_ADAPTER_HPP
#define FLYNES_SESSION_DUAL_DUAL_RUNTIME_ADAPTER_HPP

/*
 * Task 10 / step 2: the engine's DUAL runtime port.
 *
 * The frozen internal seam is `dual::DualRuntimePort`
 * (dual_runtime_contract.hpp) — a C++ interface, because the scheduler that
 * drives it is C++. The public session boundary is C, so the provider that a
 * platform supplies is `fly_session_dual_runtime_port_v2` (a table of function
 * pointers, tail-appended to fly_session_ports_v2). This adapter is the single
 * bridge between the two.
 *
 * It is a faithful pass-through and nothing else:
 *   - every call is forwarded once, with the ABI's own argument shapes and the
 *     provider's `context` pointer, and the provider's result code is returned
 *     unchanged — a failure is never translated, softened or retried;
 *   - the ABI outcome/ digest objects are zeroed before the call and copied back
 *     verbatim afterwards, so a provider that writes only part of an outcome can
 *     never leave stale bytes for the scheduler to read as this frame's result;
 *   - no default, fallback, timing or policy decision lives here. Everything the
 *     scheduler must know it learns from the provider's return code.
 *
 * The engine owns the adapter and calls it serially from the simulation worker,
 * exactly as the frozen contract requires.
 */

#include "dual_runtime_contract.hpp"

#include "flynes/flynes_session.h"

namespace flynes::session::dual {

class CAbiDualRuntimePortV1 final : public DualRuntimePort
{
public:
    /*
     * `port` must be the provider table captured by SessionPorts and must
     * already have passed validate_dual_runtime(); a null table is rejected by
     * the engine before the adapter exists.
     */
    explicit CAbiDualRuntimePortV1(
        const fly_session_dual_runtime_port_v2* port) noexcept;

    ~CAbiDualRuntimePortV1() override = default;

    CAbiDualRuntimePortV1(const CAbiDualRuntimePortV1&) = delete;
    CAbiDualRuntimePortV1& operator=(const CAbiDualRuntimePortV1&) = delete;

    fly_session_result_v2 load(const DualContentRefV1& content) noexcept override;

    fly_session_result_v2 step(const DualInputBundleV1& input,
                               DualFrameOutcomeV1* out) noexcept override;

    fly_session_result_v2 export_state(
        std::uint8_t* out, std::size_t capacity, std::size_t* out_written,
        std::array<std::uint8_t, 32>* out_hash) noexcept override;

    fly_session_result_v2 import_state(const std::uint8_t* bytes,
                                       std::size_t size) noexcept override;

    fly_session_result_v2 state_digest(std::uint64_t frame_index,
                                       DualStateDigestV1* out) noexcept override;

    /* Diagnostics only: how many calls were forwarded. The engine never makes a
     * decision from this, and the provider never sees it. */
    [[nodiscard]] std::uint64_t forwarded() const noexcept { return forwarded_; }

private:
    const fly_session_dual_runtime_port_v2* port_ = nullptr;
    std::uint64_t forwarded_ = 0;
};

} // namespace flynes::session::dual

#endif
