#pragma once

#include <cstdint>

namespace flynes::video {

enum class TemporalSlotKind { INTERPOLATED, REAL };

struct TemporalSlot {
    TemporalSlotKind kind = TemporalSlotKind::INTERPOLATED;
    bool adjusts_nominal_alternation = false;
    double phase_after = 0.0;
};

struct SourceFrameCadence {
    int presentation_slots = 1;
    bool submit_interpolated = false;
    bool adjusts_nominal_alternation = false;
    double slot_phase_after = 0.0;
};

/** Fractional source/display phase accumulator driven once per presentation timestamp. */
class TemporalScheduler {
public:
    TemporalScheduler(double source_hz, double display_hz);

    TemporalSlot advance();
    TemporalSlot advance_at(std::int64_t presentation_time_ns);
    SourceFrameCadence advance_source_frame();
    void reset();

    double phase() const { return phase_; }
    std::uint64_t real_slots() const { return real_slots_; }
    std::uint64_t interpolated_slots() const { return interpolated_slots_; }
    std::uint64_t timestamp_discontinuities() const { return timestamp_discontinuities_; }
    std::uint64_t cadence_adjustments() const { return cadence_adjustments_; }

private:
    double source_hz_;
    double display_hz_;
    double phase_ = 0.0;
    bool has_previous_kind_ = false;
    TemporalSlotKind previous_kind_ = TemporalSlotKind::INTERPOLATED;
    bool has_timestamp_ = false;
    std::int64_t previous_timestamp_ns_ = 0;
    std::uint64_t real_slots_ = 0;
    std::uint64_t interpolated_slots_ = 0;
    std::uint64_t timestamp_discontinuities_ = 0;
    double source_slot_phase_ = 1.0;
    std::uint64_t cadence_adjustments_ = 0;
};

}  // namespace flynes::video
