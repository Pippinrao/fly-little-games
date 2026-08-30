#include "temporal_scheduler.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace flynes::video {

TemporalScheduler::TemporalScheduler(double source_hz, double display_hz)
        : source_hz_(source_hz), display_hz_(display_hz) {
    if (!std::isfinite(source_hz_) || !std::isfinite(display_hz_)
            || source_hz_ <= 0.0 || display_hz_ <= 0.0 || source_hz_ > display_hz_) {
        throw std::invalid_argument("temporal cadence requires 0 < source <= display");
    }
}

TemporalSlot TemporalScheduler::advance() {
    phase_ += source_hz_ / display_hz_;
    TemporalSlotKind kind = TemporalSlotKind::INTERPOLATED;
    if (phase_ >= 1.0) {
        phase_ -= 1.0;
        kind = TemporalSlotKind::REAL;
        ++real_slots_;
    } else {
        ++interpolated_slots_;
    }
    // A correction is exactly the repeated kind needed to preserve fractional
    // source cadence instead of drifting under a hard M/B alternation.
    const bool adjustment = has_previous_kind_ && previous_kind_ == kind;
    previous_kind_ = kind;
    has_previous_kind_ = true;
    return {kind, adjustment, phase_};
}

TemporalSlot TemporalScheduler::advance_at(std::int64_t presentation_time_ns) {
    if (!has_timestamp_) {
        previous_timestamp_ns_ = presentation_time_ns;
        has_timestamp_ = true;
        previous_kind_ = TemporalSlotKind::REAL;
        has_previous_kind_ = true;
        ++real_slots_;
        return {TemporalSlotKind::REAL, false, phase_};
    }
    if (presentation_time_ns <= previous_timestamp_ns_) {
        ++timestamp_discontinuities_;
        phase_ = 0.0;
        has_previous_kind_ = false;
        previous_timestamp_ns_ = presentation_time_ns;
        previous_kind_ = TemporalSlotKind::REAL;
        has_previous_kind_ = true;
        ++real_slots_;
        return {TemporalSlotKind::REAL, false, phase_};
    }
    const double delta_seconds = static_cast<double>(
            presentation_time_ns - previous_timestamp_ns_) / 1'000'000'000.0;
    previous_timestamp_ns_ = presentation_time_ns;
    phase_ += source_hz_ * delta_seconds;
    if (!std::isfinite(phase_) || phase_ > 2.0) {
        ++timestamp_discontinuities_;
        phase_ = 0.0;
        previous_kind_ = TemporalSlotKind::REAL;
        has_previous_kind_ = true;
        ++real_slots_;
        return {TemporalSlotKind::REAL, false, phase_};
    }
    TemporalSlotKind kind = TemporalSlotKind::INTERPOLATED;
    if (phase_ >= 1.0) {
        phase_ -= std::floor(phase_);
        kind = TemporalSlotKind::REAL;
        ++real_slots_;
    } else {
        ++interpolated_slots_;
    }
    const bool adjustment = has_previous_kind_ && previous_kind_ == kind;
    previous_kind_ = kind;
    has_previous_kind_ = true;
    return {kind, adjustment, phase_};
}

SourceFrameCadence TemporalScheduler::advance_source_frame() {
    source_slot_phase_ += display_hz_ / source_hz_;
    int slots = static_cast<int>(std::floor(source_slot_phase_));
    source_slot_phase_ -= slots;
    slots = std::clamp(slots, 1, 2);
    const bool adjustment = slots == 1;
    if (adjustment) ++cadence_adjustments_;
    return {slots, slots == 2, adjustment, source_slot_phase_};
}

void TemporalScheduler::reset() {
    phase_ = 0.0;
    has_previous_kind_ = false;
    has_timestamp_ = false;
    previous_timestamp_ns_ = 0;
    real_slots_ = 0;
    interpolated_slots_ = 0;
    timestamp_discontinuities_ = 0;
    source_slot_phase_ = 1.0;
    cadence_adjustments_ = 0;
}

}  // namespace flynes::video
