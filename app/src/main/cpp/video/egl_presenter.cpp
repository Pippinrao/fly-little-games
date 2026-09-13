#include "egl_presenter.h"

#include <android/log.h>
#include <swappy/swappyGL.h>
#include <swappy/swappyGL_extra.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <time.h>

namespace flynes::video {

namespace {
std::int64_t monotonic_now_ns() {
    timespec value{};
    return clock_gettime(CLOCK_MONOTONIC, &value) == 0
            ? static_cast<std::int64_t>(value.tv_sec) * 1'000'000'000LL + value.tv_nsec : -1;
}
}

EglPresenter::EglPresenter(AAssetManager* assets, bool swappy_initialized)
        : assets_(assets), swappy_initialized_(swappy_initialized) {
    // thread_ precedes the queues and worker flags in declaration order. Start only after
    // every member is initialized, so thread_main cannot inspect an unconstructed deque.
    thread_ = std::thread(&EglPresenter::thread_main, this);
}

EglPresenter::~EglPresenter() {
    if (thread_.joinable()) thread_.join();
    if (swappy_initialized_) SwappyGL_destroy();
}

bool EglPresenter::shutdown() {
    std::unique_lock lock(mutex_);
    accepting_frames_ = false;
    pending_frame_.reset();
    motion_frames_.clear();
    motion_enabled_ = false;
    motion_shadow_enabled_ = false;
    motion_reset_requested_ = true;
    if (!stopping_) {
        stopping_ = true;
        const auto id = ++command_id_;
        commands_.push_back({Command::STOP, id, 0, nullptr, 0, 0, 0.0f});
        wake_.notify_one();
    }
    const bool exited = acknowledged_.wait_for(lock, std::chrono::milliseconds(1200),
                                                [&] { return thread_exited_; });
    lock.unlock();
    if (exited && thread_.joinable()) thread_.join();
    return exited;
}

bool EglPresenter::surface_created(ANativeWindow* window, std::uint64_t epoch) {
    if (!window) return false;
    std::unique_lock lock(mutex_);
    if (stopping_ || epoch == 0 || epoch <= ui_surface_epoch_) {
        ANativeWindow_release(window);
        return false;
    }
    ui_surface_epoch_ = epoch;
    ui_surface_state_ = UiSurfaceState::ACTIVE;
    accepting_frames_ = false;
    pending_frame_.reset();
    motion_frames_.clear();
    motion_enabled_ = false;
    motion_shadow_enabled_ = false;
    motion_reset_requested_ = true;
    {
        std::lock_guard presentation_lock(actual_presentations_mutex_);
        actual_real_presentations_.clear();
    }
    const auto id = ++command_id_;
    commands_.push_back({Command::CREATE, id, epoch, window, 0, 0, 0.0f});
    wake_.notify_one();
    // EGL window creation must not block SurfaceView.surfaceCreated() on the UI thread.
    return true;
}

void EglPresenter::surface_changed(int width, int height, std::uint64_t epoch) {
    if (width <= 0 || height <= 0) return;
    std::unique_lock lock(mutex_);
    if (stopping_ || ui_surface_state_ != UiSurfaceState::ACTIVE
            || epoch != ui_surface_epoch_) return;
    const auto id = ++command_id_;
    commands_.push_back({Command::RESIZE, id, epoch, nullptr, width, height, 0.0f});
    wake_.notify_one();
}

bool EglPresenter::surface_destroyed(std::uint64_t epoch) {
    std::unique_lock lock(mutex_);
    if (stopping_ || ui_surface_state_ != UiSurfaceState::ACTIVE
            || epoch != ui_surface_epoch_) return false;
    ui_surface_state_ = UiSurfaceState::DESTROYING;
    destroy_result_available_ = false;
    destroy_result_epoch_ = epoch;
    destroy_clear_succeeded_ = false;
    accepting_frames_ = false;
    pending_frame_.reset();
    const auto id = ++command_id_;
    commands_.push_back({Command::DESTROY, id, epoch, nullptr, 0, 0, 0.0f});
    wake_.notify_one();
    const bool completed = acknowledged_.wait_for(lock, std::chrono::milliseconds(1200), [&] {
        return (destroy_result_available_ && destroy_result_epoch_ == epoch) || thread_exited_;
    });
    return completed && destroy_result_available_ && destroy_result_epoch_ == epoch
            && destroy_clear_succeeded_;
}

bool EglPresenter::enqueue(const void* pixels, std::size_t capacity,
                           std::uint64_t sequence, int width, int height,
                           int pitch, int format, int bytes) {
    if (!pixels || width <= 0 || height <= 0 || pitch <= 0 || bytes <= 0 ||
        format < static_cast<int>(PixelFormat::RGB565) ||
        format > static_cast<int>(PixelFormat::RGBA8888)) return false;
    PixelFormat pixel_format = static_cast<PixelFormat>(format);
    int bpp = bytes_per_pixel(pixel_format);
    if (width > std::numeric_limits<int>::max() / bpp || pitch < width * bpp ||
        height > std::numeric_limits<int>::max() / pitch || bytes < pitch * height ||
        static_cast<std::size_t>(bytes) > capacity) return false;

    auto frame = std::make_unique<StagedFrame>();
    frame->sequence = sequence;
    frame->width = width;
    frame->height = height;
    frame->format = pixel_format;
    const std::size_t compact_pitch = static_cast<std::size_t>(width) * bpp;
    frame->pixels.resize(compact_pitch * static_cast<std::size_t>(height));
    const auto* source = static_cast<const std::uint8_t*>(pixels);
    for (int row = 0; row < height; ++row) {
        std::memcpy(frame->pixels.data() + static_cast<std::size_t>(row) * compact_pitch,
                    source + static_cast<std::size_t>(row) * pitch, compact_pitch);
    }

    std::lock_guard lock(mutex_);
    if (!accepting_frames_ || stopping_) return false;
    if (has_enqueued_sequence_ && sequence <= last_enqueued_sequence_) return false;
    if (has_enqueued_sequence_ && sequence > last_enqueued_sequence_ + 1) {
        metrics_.on_skipped(sequence - last_enqueued_sequence_ - 1);
    }
    if (motion_enabled_ || motion_shadow_enabled_) {
        if (has_enqueued_sequence_ && sequence != last_enqueued_sequence_ + 1) {
            metrics_.on_runtime_failure(101); // SOURCE_SEQUENCE_GAP
            motion_enabled_ = false;
            motion_shadow_enabled_ = false;
            motion_frames_.clear();
            motion_reset_requested_ = true;
            if (!motion_exit_queued_) {
                motion_exit_queued_ = true;
                commands_.push_front({Command::EXIT_MOTION_HOLD, ++command_id_,
                                      ui_surface_epoch_, nullptr, 0, 0, 0.0f});
            }
            wake_.notify_one();
            return false;
        }
        if (motion_frames_.size() >= 3) {
            metrics_.on_runtime_failure(102); // STAGING_OVERFLOW
            motion_enabled_ = false;
            motion_shadow_enabled_ = false;
            motion_frames_.clear();
            motion_reset_requested_ = true;
            if (!motion_exit_queued_) {
                motion_exit_queued_ = true;
                commands_.push_front({Command::EXIT_MOTION_HOLD, ++command_id_,
                                      ui_surface_epoch_, nullptr, 0, 0, 0.0f});
            }
            wake_.notify_one();
            return false;
        }
        last_enqueued_sequence_ = sequence;
        has_enqueued_sequence_ = true;
        motion_frames_.push_back(std::move(frame));
        wake_.notify_one();
        return true;
    }
    if (pending_frame_) metrics_.on_skipped(1);
    last_enqueued_sequence_ = sequence;
    has_enqueued_sequence_ = true;
    pending_frame_ = std::move(frame);
    wake_.notify_one();
    return true;
}

void EglPresenter::set_filter(int filter) {
    if (filter < static_cast<int>(FilterMode::EDGE_ENHANCED) ||
        filter > static_cast<int>(FilterMode::SCALEFX)) {
        filter = static_cast<int>(FilterMode::EDGE_ENHANCED);
    }
    std::lock_guard lock(mutex_);
    if (filter != requested_filter_) failed_filter_ = -1;
    requested_filter_ = filter;
}

void EglPresenter::set_active(bool active) {
    std::lock_guard lock(mutex_);
    active_ = active;
    accepting_frames_ = active_ && surface_ready_;
    if (!active_) pending_frame_.reset();
    wake_.notify_one();
}

void EglPresenter::reset_sequence() {
    std::lock_guard lock(mutex_);
    pending_frame_.reset();
    motion_frames_.clear();
    motion_reset_requested_ = true;
    has_enqueued_sequence_ = false;
    last_enqueued_sequence_ = 0;
    {
        std::lock_guard presentation_lock(actual_presentations_mutex_);
        actual_real_presentations_.clear();
    }
}

std::int64_t EglPresenter::actual_real_presentation_ns(std::uint64_t sequence) const {
    std::lock_guard lock(actual_presentations_mutex_);
    for (auto it = actual_real_presentations_.rbegin();
         it != actual_real_presentations_.rend(); ++it) {
        if (it->first == sequence) return it->second;
        if (it->first < sequence) break;
    }
    return -1;
}

bool EglPresenter::request_frame_rate(std::uint64_t epoch, float source_fps) {
    if (!std::isfinite(source_fps) || source_fps <= 0.0f) return false;
    std::unique_lock lock(mutex_);
    if (stopping_ || ui_surface_state_ != UiSurfaceState::ACTIVE
            || epoch != ui_surface_epoch_) return false;
    const auto id = ++command_id_;
    pending_result_waiters_.insert(id);
    commands_.push_back({Command::VOTE_FRAME_RATE, id, epoch, nullptr,
                         0, 0, source_fps});
    wake_.notify_one();
    const bool completed = acknowledged_.wait_for(lock, std::chrono::milliseconds(750), [&] {
        return command_results_.find(id) != command_results_.end() || thread_exited_;
    });
    if (!completed) {
        pending_result_waiters_.erase(id);
        return false;
    }
    auto result = command_results_.find(id);
    if (result == command_results_.end()) {
        pending_result_waiters_.erase(id);
        return false;
    }
    const bool succeeded = result->second;
    command_results_.erase(result);
    pending_result_waiters_.erase(id);
    return succeeded;
}

bool EglPresenter::clear_frame_rate(std::uint64_t epoch) {
    std::unique_lock lock(mutex_);
    if (stopping_ || epoch != ui_surface_epoch_) return false;
    if (ui_surface_state_ == UiSurfaceState::DESTROYING) {
        const bool completed = acknowledged_.wait_for(lock, std::chrono::milliseconds(500), [&] {
            return (destroy_result_available_ && destroy_result_epoch_ == epoch)
                    || thread_exited_;
        });
        return completed && destroy_result_available_ && destroy_result_epoch_ == epoch
                && destroy_clear_succeeded_;
    }
    if (ui_surface_state_ == UiSurfaceState::EMPTY) {
        if (destroy_result_available_ && destroy_result_epoch_ == epoch
                && destroy_clear_succeeded_) return true;
        // A destroy can finish before its bounded platform clear. Re-enter the
        // render thread so the coordinator can observe the original pending token.
    }
    if (ui_surface_state_ != UiSurfaceState::ACTIVE
            && ui_surface_state_ != UiSurfaceState::EMPTY) return false;
    const auto id = ++command_id_;
    pending_result_waiters_.insert(id);
    commands_.push_back({Command::CLEAR_FRAME_RATE, id, epoch, nullptr,
                         0, 0, 0.0f});
    wake_.notify_one();
    const bool completed = acknowledged_.wait_for(lock, std::chrono::milliseconds(500), [&] {
        return command_results_.find(id) != command_results_.end() || thread_exited_;
    });
    if (!completed) {
        pending_result_waiters_.erase(id);
        return false;
    }
    auto result = command_results_.find(id);
    if (result == command_results_.end()) {
        pending_result_waiters_.erase(id);
        return false;
    }
    const bool succeeded = result->second;
    command_results_.erase(result);
    pending_result_waiters_.erase(id);
    return succeeded;
}

bool EglPresenter::configure_motion(std::uint64_t epoch,
                                    std::uint64_t display_generation,
                                    float display_hz, float source_fps,
                                    std::int64_t lease_deadline_ns,
                                    bool force_pacer_disabled) {
    const std::int64_t now = monotonic_now_ns();
    constexpr std::int64_t kMaximumLeaseNs = 1'500'000'000LL;
    if (now < 0 || !std::isfinite(display_hz) || display_hz < 119.0f || display_hz > 121.0f
            || !std::isfinite(source_fps) || source_fps <= 0.0f
            || display_generation == 0 || lease_deadline_ns <= now
            || lease_deadline_ns - now > kMaximumLeaseNs) return false;
    std::unique_lock lock(mutex_);
    if (stopping_ || active_ || ui_surface_state_ != UiSurfaceState::ACTIVE
            || epoch != ui_surface_epoch_) return false;
    const auto id = ++command_id_;
    pending_result_waiters_.insert(id);
    PendingCommand pending{Command::ENTER_MOTION, id, epoch, nullptr, 0, 0, source_fps};
    pending.display_generation = display_generation;
    pending.display_hz = display_hz;
    pending.lease_deadline_ns = lease_deadline_ns;
    pending.force_pacer_disabled = force_pacer_disabled;
    commands_.push_back(pending);
    wake_.notify_one();
    const bool completed = acknowledged_.wait_for(lock, std::chrono::milliseconds(900), [&] {
        return command_results_.find(id) != command_results_.end() || thread_exited_;
    });
    auto result = command_results_.find(id);
    if (!completed || result == command_results_.end()) {
        pending_result_waiters_.erase(id);
        cancelled_commands_.insert(id);
        wake_.notify_one();
        return false;
    }
    const bool succeeded = result->second;
    command_results_.erase(result);
    pending_result_waiters_.erase(id);
    return succeeded;
}

bool EglPresenter::update_motion_lease(std::uint64_t epoch,
                                       std::uint64_t display_generation,
                                       std::int64_t lease_deadline_ns) {
    const std::int64_t now = monotonic_now_ns();
    if (now < 0 || lease_deadline_ns <= now
            || lease_deadline_ns - now > 1'500'000'000LL) return false;
    std::lock_guard lock(mutex_);
    if (!motion_enabled_ || epoch != ui_surface_epoch_
            || display_generation != motion_display_generation_.load(
                    std::memory_order_acquire)) return false;
    motion_lease_deadline_ns_.store(lease_deadline_ns, std::memory_order_release);
    return true;
}

std::int64_t EglPresenter::begin_motion_shadow(std::uint64_t epoch, float source_fps) {
    if (!std::isfinite(source_fps) || source_fps <= 0.0f) return -1;
    std::lock_guard lock(mutex_);
    if (stopping_ || active_ || ui_surface_state_ != UiSurfaceState::ACTIVE
            || epoch != ui_surface_epoch_ || motion_enabled_ || motion_shadow_enabled_) return -1;
    const auto id = ++command_id_;
    commands_.push_back({Command::ENTER_MOTION_SHADOW, id, epoch, nullptr,
                         0, 0, source_fps});
    wake_.notify_one();
    return static_cast<std::int64_t>(id);
}

std::int64_t EglPresenter::exit_motion(std::uint64_t epoch, bool drain_to_immediate) {
    std::lock_guard lock(mutex_);
    if (stopping_ || ui_surface_state_ != UiSurfaceState::ACTIVE
            || epoch != ui_surface_epoch_) return -1;
    if (motion_exit_queued_) {
        if (drain_to_immediate && !motion_drain_queued_) {
            bool upgraded = false;
            for (auto& pending : commands_) {
                if (pending.type == Command::EXIT_MOTION_HOLD) {
                    pending.type = Command::EXIT_MOTION_DRAIN;
                    upgraded = true;
                    motion_drain_queued_ = true;
                    wake_.notify_one();
                    return static_cast<std::int64_t>(pending.id);
                }
            }
            if (!upgraded) {
                const auto drain_id = ++command_id_;
                commands_.push_back({Command::EXIT_MOTION_DRAIN, drain_id, epoch,
                                     nullptr, 0, 0, 0.0f});
                motion_drain_queued_ = true;
                wake_.notify_one();
                return static_cast<std::int64_t>(drain_id);
            }
        }
        for (const auto& pending : commands_) {
            if (pending.type == (drain_to_immediate ? Command::EXIT_MOTION_DRAIN
                                                    : Command::EXIT_MOTION_HOLD)) {
                return static_cast<std::int64_t>(pending.id);
            }
        }
        return active_exit_id_ == 0 ? -1 : static_cast<std::int64_t>(active_exit_id_);
    }
    const auto id = ++command_id_;
    motion_exit_queued_ = true;
    motion_drain_queued_ = drain_to_immediate;
    commands_.push_back({drain_to_immediate ? Command::EXIT_MOTION_DRAIN
                                            : Command::EXIT_MOTION_HOLD,
                         id, epoch, nullptr, 0, 0, 0.0f});
    wake_.notify_one();
    // EXIT is irrevocable once queued. Later frame-rate commands are serialized
    // behind it on this same render-thread queue, so Java can release Motion
    // ownership without racing a still-attached Swappy writer.
    return static_cast<std::int64_t>(id);
}

void EglPresenter::thread_main() {
    for (;;) {
        std::unique_ptr<StagedFrame> frame;
        Command command = Command::NONE;
        std::uint64_t command_id = 0;
        std::uint64_t epoch = 0;
        ANativeWindow* window = nullptr;
        int width = 0;
        int height = 0;
        float frame_rate = 0.0f;
        std::uint64_t display_generation = 0;
        float display_hz = 0.0f;
        std::int64_t lease_deadline_ns = 0;
        bool force_pacer_disabled = false;
        int filter = 0;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return !commands_.empty() || pending_frame_
                    || !motion_frames_.empty() || motion_reset_requested_ || stopping_; });
            if (motion_reset_requested_) {
                motion_previous_.reset();
                motion_reset_requested_ = false;
                if (commands_.empty() && !pending_frame_ && motion_frames_.empty()) continue;
            }
            if (!commands_.empty()) {
                PendingCommand pending = commands_.front();
                commands_.pop_front();
                command = pending.type;
                command_id = pending.id;
                if (command == Command::EXIT_MOTION_HOLD
                        || command == Command::EXIT_MOTION_DRAIN) {
                    active_exit_id_ = command_id;
                }
                epoch = pending.epoch;
                window = pending.window;
                width = pending.width;
                height = pending.height;
                frame_rate = pending.frame_rate;
                display_generation = pending.display_generation;
                display_hz = pending.display_hz;
                lease_deadline_ns = pending.lease_deadline_ns;
                force_pacer_disabled = pending.force_pacer_disabled;
            } else if ((motion_enabled_ || motion_shadow_enabled_) && !motion_frames_.empty()
                    && active_ && surface_ready_) {
                frame = std::move(motion_frames_.front());
                motion_frames_.pop_front();
                metrics_.on_motion_queue_depth(motion_frames_.size());
                filter = requested_filter_ == failed_filter_
                        ? static_cast<int>(FilterMode::NEAREST) : requested_filter_;
            } else if (pending_frame_ && active_ && surface_ready_) {
                frame = std::move(pending_frame_);
                filter = requested_filter_ == failed_filter_
                        ? static_cast<int>(FilterMode::NEAREST) : requested_filter_;
            } else if (stopping_) {
                command = Command::STOP;
                command_id = command_id_;
            } else {
                continue;
            }
        }

        if (command != Command::NONE) {
            bool created = false;
            bool command_succeeded = true;
            bool command_entered_from_shadow = false;
            int final_pacing_owner = -1;
            if (command == Command::CREATE) {
                created = coordinator_.begin_create(epoch);
                if (created) {
                    destroy_egl();
                    created = create_egl(window, 2);
                } else if (window) {
                    ANativeWindow_release(window);
                }
                if (created) {
                    int native_width = ANativeWindow_getWidth(active_window_);
                    int native_height = ANativeWindow_getHeight(active_window_);
                    pipeline_.resize(std::max(1, native_width), std::max(1, native_height));
                }
                final_pacing_owner = created ? 1 : 0;
            } else if (command == Command::RESIZE) {
                if (surface_ != EGL_NO_SURFACE) pipeline_.resize(width, height);
            } else if (command == Command::VOTE_FRAME_RATE) {
                auto result = coordinator_.request_frame_rate(epoch, active_window_, frame_rate);
                metrics_.on_frame_rate_vote(static_cast<int>(std::lround(frame_rate * 1000.0f)),
                                            static_cast<int>(result));
                command_succeeded = result == PresentationCoordinator::FrameRateVoteResult::APPLIED;
            } else if (command == Command::CLEAR_FRAME_RATE) {
                auto result = coordinator_.clear_frame_rate(epoch, active_window_);
                metrics_.on_frame_rate_vote(0, static_cast<int>(result));
                command_succeeded = result == PresentationCoordinator::FrameRateVoteResult::CLEARED
                        || result == PresentationCoordinator::FrameRateVoteResult::UNSUPPORTED;
            } else if (command == Command::ENTER_MOTION_SHADOW) {
                motion_pipeline_.destroy();
                bool es31_context = false;
                const bool context_ready = recreate_egl_with_fallback(3, 2, &es31_context);
                command_succeeded = context_ready && es31_context
                        && motion_pipeline_.initialize()
                        && coordinator_.begin_motion_shadow(epoch);
                if (command_succeeded) {
                    motion_source_fps_ = frame_rate;
                    metrics_.reset_shadow();
                    final_pacing_owner = 1;
                } else {
                    motion_pipeline_.destroy();
                    const bool baseline_context_ready = es31_context
                            ? recreate_egl_exact(2) : context_ready;
                    coordinator_.enter_buffered_hold(epoch);
                    final_pacing_owner = baseline_context_ready ? 1 : 0;
                }
            } else if (command == Command::ENTER_MOTION) {
                command_entered_from_shadow = coordinator_.temporal_state()
                        == PresentationCoordinator::TemporalState::PRIMING_SHADOW;
                {
                    std::lock_guard lock(mutex_);
                    if (cancelled_commands_.erase(command_id) != 0) command_succeeded = false;
                }
                const auto clear = coordinator_.clear_frame_rate(epoch, active_window_);
                metrics_.on_frame_rate_vote(0, static_cast<int>(clear));
                const bool cleared = clear == PresentationCoordinator::FrameRateVoteResult::CLEARED
                        || clear == PresentationCoordinator::FrameRateVoteResult::UNSUPPORTED;
                motion_pipeline_.destroy();
                bool es31_context = false;
                const bool context_ready = cleared
                        && recreate_egl_with_fallback(3, 2, &es31_context);
                command_succeeded = command_succeeded && cleared && context_ready && es31_context
                        && !force_pacer_disabled
                        && swappy_initialized_
                        && SwappyGL_isEnabled() && display_hz >= 119.0f && display_hz <= 121.0f
                        && lease_deadline_ns > monotonic_now_ns()
                        && coordinator_.begin_motion_priming(epoch)
                        && motion_pipeline_.initialize()
                        && SwappyGL_setWindow(active_window_);
                if (command_succeeded) {
                    SwappyGL_setAutoSwapInterval(false);
                    SwappyGL_setAutoPipelineMode(false);
                    SwappyGL_setSwapIntervalNS(static_cast<std::uint64_t>(
                            std::llround(1'000'000'000.0 / display_hz)));
                    SwappyGL_enableStats(true);
                    SwappyGL_clearStats();
                    swappy_attached_ = true;
                    motion_source_fps_ = frame_rate;
                    motion_display_generation_ = display_generation;
                    motion_lease_deadline_ns_ = lease_deadline_ns;
                    motion_swaps_ = 0;
                    last_swappy_total_frames_ = 0;
                    last_swappy_progress_ns_ = monotonic_now_ns();
                    temporal_scheduler_ = std::make_unique<TemporalScheduler>(
                            static_cast<double>(frame_rate), static_cast<double>(display_hz));
                    final_pacing_owner = 2;
                } else {
                    temporal_scheduler_.reset();
                }
                if (!command_succeeded) {
                    motion_pipeline_.destroy();
                    if (swappy_attached_) SwappyGL_setWindow(nullptr);
                    swappy_attached_ = false;
                    temporal_scheduler_.reset();
                    const bool baseline_context_ready = es31_context
                            ? recreate_egl_exact(2) : context_ready;
                    const bool fallback_state_ready = command_entered_from_shadow
                            ? coordinator_.enter_buffered_hold(epoch)
                            : coordinator_.enter_draining(epoch);
                    const auto restored = baseline_context_ready && fallback_state_ready
                            ? coordinator_.request_frame_rate(epoch, active_window_, frame_rate)
                            : PresentationCoordinator::FrameRateVoteResult::FAILED;
                    metrics_.on_frame_rate_vote(
                            static_cast<int>(std::lround(frame_rate * 1000.0f)),
                            static_cast<int>(restored));
                    const bool vote_ready = restored
                                    == PresentationCoordinator::FrameRateVoteResult::APPLIED
                            || restored
                                    == PresentationCoordinator::FrameRateVoteResult::UNSUPPORTED;
                    const bool native_ready = baseline_context_ready && fallback_state_ready
                            && vote_ready && (command_entered_from_shadow
                                || coordinator_.resume_immediate_native(epoch));
                    final_pacing_owner = native_ready ? 1 : 0;
                }
            } else if (command == Command::EXIT_MOTION_HOLD
                    || command == Command::EXIT_MOTION_DRAIN) {
                const bool drain = command == Command::EXIT_MOTION_DRAIN;
                command_succeeded = drain ? coordinator_.enter_draining(epoch)
                                          : coordinator_.enter_buffered_hold(epoch);
                if (swappy_attached_) SwappyGL_setWindow(nullptr);
                swappy_attached_ = false;
                motion_pipeline_.destroy();
                temporal_scheduler_.reset();
                command_succeeded = command_succeeded && recreate_egl_exact(2);
                const auto restored = coordinator_.request_frame_rate(
                        epoch, active_window_, motion_source_fps_);
                metrics_.on_frame_rate_vote(
                        static_cast<int>(std::lround(motion_source_fps_ * 1000.0f)),
                        static_cast<int>(restored));
                command_succeeded = command_succeeded
                        && (restored == PresentationCoordinator::FrameRateVoteResult::APPLIED
                            || restored == PresentationCoordinator::FrameRateVoteResult::UNSUPPORTED);
                if (command_succeeded && drain) {
                    command_succeeded = coordinator_.resume_immediate_native(epoch);
                }
                final_pacing_owner = command_succeeded ? 1 : 0;
            } else if (command == Command::DESTROY || command == Command::STOP) {
                const std::uint64_t destroy_epoch = command == Command::STOP
                        ? coordinator_.epoch() : epoch;
                const bool destroying = coordinator_.begin_destroy(destroy_epoch);
                if (active_window_) {
                    if (swappy_attached_) SwappyGL_setWindow(nullptr);
                    swappy_attached_ = false;
                    motion_pipeline_.destroy();
                    temporal_scheduler_.reset();
                    coordinator_.suspend_surface(destroy_epoch);
                    auto result = coordinator_.clear_frame_rate(destroy_epoch, active_window_);
                    metrics_.on_frame_rate_vote(0, static_cast<int>(result));
                    command_succeeded = result
                            == PresentationCoordinator::FrameRateVoteResult::CLEARED
                            || result
                            == PresentationCoordinator::FrameRateVoteResult::UNSUPPORTED;
                }
                destroy_egl();
                if (destroying) coordinator_.complete_destroy(destroy_epoch);
                final_pacing_owner = 0;
            }
            {
                std::unique_lock lock(mutex_);
                // Linearization point for ENTER_MOTION: either the waiter observes this
                // success while the mutex is held, or its timeout token is already visible
                // and the render thread rolls the complete transaction back before publish.
                if (command == Command::ENTER_MOTION
                        && cancelled_commands_.erase(command_id) != 0
                        && command_succeeded) {
                    motion_pipeline_.destroy();
                    if (swappy_attached_) SwappyGL_setWindow(nullptr);
                    swappy_attached_ = false;
                    temporal_scheduler_.reset();
                    const bool context_ready = recreate_egl_exact(2);
                    const bool fallback_state_ready = command_entered_from_shadow
                            ? coordinator_.enter_buffered_hold(epoch)
                            : coordinator_.enter_draining(epoch);
                    const auto restored = context_ready && fallback_state_ready
                            ? coordinator_.request_frame_rate(epoch, active_window_, frame_rate)
                            : PresentationCoordinator::FrameRateVoteResult::FAILED;
                    metrics_.on_frame_rate_vote(
                            static_cast<int>(std::lround(frame_rate * 1000.0f)),
                            static_cast<int>(restored));
                    const bool vote_ready = restored
                                    == PresentationCoordinator::FrameRateVoteResult::APPLIED
                            || restored
                                    == PresentationCoordinator::FrameRateVoteResult::UNSUPPORTED;
                    final_pacing_owner = context_ready && fallback_state_ready && vote_ready
                            && (command_entered_from_shadow
                                || coordinator_.resume_immediate_native(epoch)) ? 1 : 0;
                    command_succeeded = false;
                }
                if (final_pacing_owner >= 0) metrics_.on_pacing_owner(final_pacing_owner);
                if (command == Command::CREATE) {
                    coordinator_.complete_create(epoch, created);
                    surface_ready_ = created && coordinator_.is_active(epoch);
                    accepting_frames_ = surface_ready_ && active_;
                    if (surface_ready_) metrics_.on_surface_epoch(epoch);
                } else if (command == Command::DESTROY) {
                    surface_ready_ = false;
                    accepting_frames_ = false;
                    motion_enabled_ = false;
                    motion_shadow_enabled_ = false;
                    if (ui_surface_epoch_ == epoch) ui_surface_state_ = UiSurfaceState::EMPTY;
                    destroy_result_epoch_ = epoch;
                    destroy_clear_succeeded_ = command_succeeded;
                    destroy_result_available_ = true;
                }
                if (command == Command::CLEAR_FRAME_RATE
                        || command == Command::VOTE_FRAME_RATE
                        || command == Command::ENTER_MOTION
                        || command == Command::ENTER_MOTION_SHADOW
                        || command == Command::EXIT_MOTION_HOLD
                        || command == Command::EXIT_MOTION_DRAIN) {
                    if (command == Command::CLEAR_FRAME_RATE
                            && destroy_result_available_
                            && destroy_result_epoch_ <= epoch
                            && command_succeeded) {
                        destroy_clear_succeeded_ = true;
                    }
                    if (pending_result_waiters_.find(command_id)
                            != pending_result_waiters_.end()) {
                        command_results_[command_id] = command_succeeded;
                    }
                    if (command == Command::ENTER_MOTION_SHADOW) {
                        motion_shadow_enabled_ = command_succeeded;
                        motion_enabled_ = false;
                        motion_frames_.clear();
                        motion_previous_.reset();
                        has_enqueued_sequence_ = false;
                    } else if (command == Command::ENTER_MOTION) {
                        motion_enabled_ = command_succeeded;
                        motion_shadow_enabled_ = false;
                        motion_exit_queued_ = false;
                        motion_drain_queued_ = false;
                        motion_frames_.clear();
                        motion_previous_.reset();
                        has_enqueued_sequence_ = false;
                    } else if (command == Command::EXIT_MOTION_HOLD
                            || command == Command::EXIT_MOTION_DRAIN) {
                        motion_enabled_ = false;
                        motion_shadow_enabled_ = false;
                        if (command == Command::EXIT_MOTION_DRAIN) {
                            motion_drain_queued_ = false;
                        }
                        motion_exit_queued_ = std::any_of(
                                commands_.begin(), commands_.end(), [](const auto& pending) {
                                    return pending.type == Command::EXIT_MOTION_HOLD
                                            || pending.type == Command::EXIT_MOTION_DRAIN;
                                });
                        active_exit_id_ = 0;
                        if (!motion_frames_.empty()) {
                            pending_frame_ = std::move(motion_frames_.back());
                            motion_frames_.clear();
                        }
                        metrics_.on_motion_queue_depth(0);
                        if (command == Command::EXIT_MOTION_DRAIN) motion_previous_.reset();
                    }
                }
                // Publish transition generation last. Readers use this release/acquire
                // marker as the commit record for all owner/state/resource mutations above.
                if (command == Command::CREATE || command == Command::DESTROY
                        || command == Command::ENTER_MOTION
                        || command == Command::ENTER_MOTION_SHADOW
                        || command == Command::EXIT_MOTION_HOLD
                        || command == Command::EXIT_MOTION_DRAIN) {
                    metrics_.on_transition(command_id,
                            static_cast<int>(coordinator_.temporal_state()));
                }
                acknowledged_id_ = std::max(acknowledged_id_, command_id);
                if (command == Command::STOP) thread_exited_ = true;
                acknowledged_.notify_all();
            }
            if (command == Command::STOP) return;
            continue;
        }

        auto render_and_present = [&](const StagedFrame& target, bool paced,
                                      bool real_slot) {
            const auto lease_is_fresh = [&] {
                const std::int64_t now = monotonic_now_ns();
                return now >= 0 && now < motion_lease_deadline_ns_.load(
                        std::memory_order_acquire);
            };
            if (paced && !lease_is_fresh()) return false;
            pipeline_.set_filter(static_cast<FilterMode>(filter));
            if (paced) SwappyGL_recordFrameStart(display_, surface_);
            gpu_timer_.begin();
            bool rendered = pipeline_.render(target);
            gpu_timer_.end();
            gpu_timer_.poll();
            metrics_.on_gpu_timing(static_cast<int>(gpu_timer_.status()),
                                   gpu_timer_.last_duration_ns());
            if (!rendered) {
                metrics_.on_runtime_failure(static_cast<int>(pipeline_.last_failure()));
                pipeline_.set_filter(FilterMode::NEAREST);
                rendered = pipeline_.render(target);
            }
            if (!rendered) return false;
            metrics_.on_upload();
            // Rendering may itself consume the remaining lease. This check must be
            // adjacent to the only paced swap, not merely before shader work.
            if (paced && !lease_is_fresh()) return false;
            EGLuint64KHR frame_id = 0;
            const bool timestamp_requested = get_next_frame_id_
                    && get_next_frame_id_(display_, surface_, &frame_id) == EGL_TRUE;
            const bool swapped = paced ? SwappyGL_swap(display_, surface_)
                                       : eglSwapBuffers(display_, surface_) == EGL_TRUE;
            if (!swapped) return false;
            if (timestamp_requested) {
                pending_presentations_.push_back({frame_id, target.sequence, real_slot});
            }
            poll_presentation_timestamps();
            const std::int64_t presented_at = monotonic_now_ns();
            metrics_.on_submit(target.sequence, presented_at);
            if (paced && ++motion_swaps_ % 120u == 0u) {
                SwappyStats stats{};
                SwappyGL_getStats(&stats);
                if (stats.totalFrames > last_swappy_total_frames_) {
                    last_swappy_total_frames_ = stats.totalFrames;
                    last_swappy_progress_ns_ = presented_at;
                    metrics_.on_swappy_progress(stats.totalFrames, presented_at);
                } else if (presented_at - last_swappy_progress_ns_ >= 1'500'000'000LL) {
                    return false;
                }
                std::uint64_t slow_offsets = 0;
                for (int bucket = 2; bucket < MAX_FRAME_BUCKETS; ++bucket)
                    slow_offsets += stats.offsetFromPreviousFrame[bucket];
                if (stats.totalFrames >= 100 && slow_offsets * 4u > stats.totalFrames)
                    return false;
            }
            return true;
        };

        bool use_motion = false;
        bool use_shadow = false;
        {
            std::lock_guard lock(mutex_);
            use_motion = motion_enabled_;
            use_shadow = motion_shadow_enabled_;
        }
        if (use_shadow) {
            if (!motion_previous_) {
                motion_previous_ = std::move(frame);
                continue;
            }
            bool shadow_ok = frame && frame->format == PixelFormat::RGB565
                    && frame->width == motion_previous_->width
                    && frame->height == motion_previous_->height
                    && render_and_present(*motion_previous_, false, true);
            MotionComputePipeline::Output midpoint;
            if (shadow_ok) {
                motion_gpu_timer_.begin();
                midpoint = motion_pipeline_.interpolate(
                        reinterpret_cast<const std::uint16_t*>(motion_previous_->pixels.data()),
                        reinterpret_cast<const std::uint16_t*>(frame->pixels.data()),
                        frame->width, frame->height);
                motion_gpu_timer_.end();
                motion_gpu_timer_.poll();
                metrics_.on_shadow_gpu_timing(
                        static_cast<int>(motion_gpu_timer_.status()),
                        motion_gpu_timer_.last_duration_ns());
                shadow_ok = midpoint.failure == MotionComputePipeline::Failure::NONE;
            }
            if (shadow_ok) {
                metrics_.on_shadow_pair(midpoint.unsafe_ratio);
                motion_previous_ = std::move(frame);
                continue;
            }
            metrics_.on_runtime_failure(104); // SHADOW_COMPUTE_FAILURE
            motion_pipeline_.destroy();
            coordinator_.enter_buffered_hold(coordinator_.epoch());
            const bool native_ready = recreate_egl_exact(2);
            {
                std::lock_guard lock(mutex_);
                motion_shadow_enabled_ = false;
                if (!motion_frames_.empty()) {
                    pending_frame_ = std::move(motion_frames_.back());
                    motion_frames_.clear();
                }
                metrics_.on_motion_queue_depth(0);
            }
            metrics_.on_pacing_owner(native_ready ? 1 : 0);
            if (frame) motion_previous_ = std::move(frame);
            continue;
        }
        if (!use_motion) {
            const auto state = coordinator_.temporal_state();
            if (state == PresentationCoordinator::TemporalState::MOTION_COMPENSATING
                    || state == PresentationCoordinator::TemporalState::PRIMING) {
                // A native overflow/gap can revoke Motion while this frame was
                // already outside the mutex. Never direct-swap through an EGL
                // writer while Swappy still owns the window; the queued EXIT
                // transaction has priority and will preserve the newest frame.
                std::lock_guard lock(mutex_);
                if (frame) pending_frame_ = std::move(frame);
                continue;
            }
            const bool buffered = state
                            == PresentationCoordinator::TemporalState::BUFFERED_NATIVE_HOLD
                    || state == PresentationCoordinator::TemporalState::PRIMING_SHADOW;
            if (buffered && frame) {
                if (motion_previous_ && !render_and_present(*motion_previous_, false, true)) {
                    __android_log_print(ANDROID_LOG_ERROR, "FlyNESVideo",
                                        "buffered native present failed");
                }
                motion_previous_ = std::move(frame);
            } else if (frame && !render_and_present(*frame, false, true)) {
                __android_log_print(ANDROID_LOG_ERROR, "FlyNESVideo", "direct present failed");
            }
            continue;
        }
        if (!motion_previous_) {
            motion_previous_ = std::move(frame);
            continue;
        }
        const auto cadence = temporal_scheduler_
                ? temporal_scheduler_->advance_source_frame()
                : SourceFrameCadence{1, false, true, 0.0};
        if (cadence.adjusts_nominal_alternation) metrics_.on_cadence_adjustment();
        bool motion_ok = frame && frame->format == PixelFormat::RGB565
                && frame->width == motion_previous_->width
                && frame->height == motion_previous_->height
                && render_and_present(*motion_previous_, true, true);
        if (motion_ok) metrics_.on_real_slot(motion_previous_->sequence);
        MotionComputePipeline::Output midpoint;
        if (motion_ok && cadence.submit_interpolated
                && monotonic_now_ns() < motion_lease_deadline_ns_) {
            midpoint = motion_pipeline_.interpolate(
                    reinterpret_cast<const std::uint16_t*>(motion_previous_->pixels.data()),
                    reinterpret_cast<const std::uint16_t*>(frame->pixels.data()),
                    frame->width, frame->height);
            motion_ok = midpoint.failure == MotionComputePipeline::Failure::NONE
                    && midpoint.pixels.size() == static_cast<std::size_t>(
                            frame->width * frame->height);
        }
        if (motion_ok && cadence.submit_interpolated) {
            StagedFrame synthesized;
            synthesized.sequence = frame->sequence;
            synthesized.width = frame->width;
            synthesized.height = frame->height;
            synthesized.format = PixelFormat::RGB565;
            synthesized.pixels.resize(midpoint.pixels.size() * sizeof(std::uint16_t));
            std::memcpy(synthesized.pixels.data(), midpoint.pixels.data(),
                        synthesized.pixels.size());
            motion_ok = render_and_present(synthesized, true, false);
            if (motion_ok) metrics_.on_interpolated_slot(
                    motion_previous_->sequence, frame->sequence, midpoint.generated);
            if (motion_ok && coordinator_.temporal_state()
                    == PresentationCoordinator::TemporalState::PRIMING) {
                motion_ok = coordinator_.activate_motion(coordinator_.epoch());
            }
        } else if (motion_ok && coordinator_.temporal_state()
                == PresentationCoordinator::TemporalState::PRIMING) {
            motion_ok = coordinator_.activate_motion(coordinator_.epoch());
        }
        if (!motion_ok) {
            metrics_.on_runtime_failure(103);
            coordinator_.enter_buffered_hold(coordinator_.epoch());
            if (swappy_attached_) SwappyGL_setWindow(nullptr);
            swappy_attached_ = false;
            motion_pipeline_.destroy();
            temporal_scheduler_.reset();
            const bool context_ready = recreate_egl_exact(2);
            {
                std::lock_guard lock(mutex_);
                motion_enabled_ = false;
                motion_shadow_enabled_ = false;
                if (!motion_frames_.empty()) {
                    pending_frame_ = std::move(motion_frames_.back());
                    motion_frames_.clear();
                }
                metrics_.on_motion_queue_depth(0);
            }
            const auto restored = context_ready
                    ? coordinator_.request_frame_rate(coordinator_.epoch(), active_window_,
                                                      motion_source_fps_)
                    : PresentationCoordinator::FrameRateVoteResult::FAILED;
            const bool native_ready = context_ready
                    && (restored == PresentationCoordinator::FrameRateVoteResult::APPLIED
                        || restored == PresentationCoordinator::FrameRateVoteResult::UNSUPPORTED);
            metrics_.on_frame_rate_vote(
                    static_cast<int>(std::lround(motion_source_fps_ * 1000.0f)),
                    static_cast<int>(restored));
            metrics_.on_pacing_owner(native_ready ? 1 : 0);
            if (frame) motion_previous_ = std::move(frame);
            continue;
        }
        motion_previous_ = std::move(frame);
    }
}

bool EglPresenter::create_egl(ANativeWindow* window, int client_version) {
    active_window_ = window;
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY || eglInitialize(display_, nullptr, nullptr) != EGL_TRUE) {
        destroy_egl();
        return false;
    }
    constexpr EGLint kEs3Bit = 0x40;
    const int versions[] = {client_version};
    for (int version : versions) {
        const EGLint attributes[] = {
                EGL_RENDERABLE_TYPE, version == 3 ? kEs3Bit : EGL_OPENGL_ES2_BIT,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                EGL_NONE};
        EGLConfig config = nullptr;
        EGLint count = 0;
        if (eglChooseConfig(display_, attributes, &config, 1, &count) != EGL_TRUE
                || count != 1) continue;
        EGLint visual_id = 0;
        eglGetConfigAttrib(display_, config, EGL_NATIVE_VISUAL_ID, &visual_id);
        ANativeWindow_setBuffersGeometry(active_window_, 0, 0, visual_id);
        surface_ = eglCreateWindowSurface(display_, config, active_window_, nullptr);
        const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, version, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attributes);
        if (surface_ != EGL_NO_SURFACE && context_ != EGL_NO_CONTEXT
                && eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE) {
            break;
        }
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        context_ = EGL_NO_CONTEXT;
        surface_ = EGL_NO_SURFACE;
    }
    if (surface_ == EGL_NO_SURFACE || context_ == EGL_NO_CONTEXT
            || !pipeline_.initialize(assets_)) {
        destroy_egl();
        return false;
    }
    pipeline_.resize(std::max(1, ANativeWindow_getWidth(active_window_)),
                     std::max(1, ANativeWindow_getHeight(active_window_)));
    initialize_presentation_timestamps();
    gpu_timer_.initialize();
    motion_gpu_timer_.initialize();
    metrics_.on_gpu_timing(static_cast<int>(gpu_timer_.status()),
                           gpu_timer_.last_duration_ns());
    return true;
}

bool EglPresenter::recreate_egl_exact(int client_version) {
    if (!active_window_) return false;
    ANativeWindow* target = active_window_;
    ANativeWindow_acquire(target);
    destroy_egl();
    return create_egl(target, client_version);
}

bool EglPresenter::recreate_egl_with_fallback(int primary_version, int fallback_version,
                                              bool* primary_ready) {
    if (primary_ready) *primary_ready = false;
    if (!active_window_) return false;
    ANativeWindow* target = active_window_;
    // One reference is consumed by each create attempt; destroy_egl consumes
    // the presenter's original reference.
    ANativeWindow_acquire(target);
    ANativeWindow_acquire(target);
    destroy_egl();
    if (create_egl(target, primary_version)) {
        ANativeWindow_release(target);
        if (primary_ready) *primary_ready = true;
        return true;
    }
    return create_egl(target, fallback_version);
}

void EglPresenter::initialize_presentation_timestamps() {
    get_next_frame_id_ = nullptr;
    get_frame_timestamps_ = nullptr;
    pending_presentations_.clear();
    const char* extensions = eglQueryString(display_, EGL_EXTENSIONS);
    if (!extensions || !std::strstr(extensions, "EGL_ANDROID_get_frame_timestamps")) return;
    get_next_frame_id_ = reinterpret_cast<PFNEGLGETNEXTFRAMEIDANDROIDPROC>(
            eglGetProcAddress("eglGetNextFrameIdANDROID"));
    get_frame_timestamps_ = reinterpret_cast<PFNEGLGETFRAMETIMESTAMPSANDROIDPROC>(
            eglGetProcAddress("eglGetFrameTimestampsANDROID"));
    if (!get_next_frame_id_ || !get_frame_timestamps_) {
        get_next_frame_id_ = nullptr;
        get_frame_timestamps_ = nullptr;
    }
}

void EglPresenter::poll_presentation_timestamps() {
    if (!get_frame_timestamps_) return;
    while (!pending_presentations_.empty()) {
        const auto pending = pending_presentations_.front();
        constexpr EGLint kTimestamp = EGL_DISPLAY_PRESENT_TIME_ANDROID;
        EGLnsecsANDROID value = EGL_TIMESTAMP_PENDING_ANDROID;
        if (get_frame_timestamps_(display_, surface_, pending.frame_id, 1,
                                  &kTimestamp, &value) != EGL_TRUE) break;
        if (value == EGL_TIMESTAMP_PENDING_ANDROID) break;
        pending_presentations_.pop_front();
        if (value >= 0) {
            metrics_.on_actual_presentation(pending.sequence,
                    static_cast<std::int64_t>(value), pending.real_slot);
            if (pending.real_slot) {
                std::lock_guard lock(actual_presentations_mutex_);
                actual_real_presentations_.emplace_back(
                        pending.sequence, static_cast<std::int64_t>(value));
                while (actual_real_presentations_.size() > 256) {
                    actual_real_presentations_.pop_front();
                }
            }
        }
    }
    while (pending_presentations_.size() > 16) pending_presentations_.pop_front();
}

void EglPresenter::destroy_egl() {
    pending_presentations_.clear();
    get_next_frame_id_ = nullptr;
    get_frame_timestamps_ = nullptr;
    if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT) {
        gpu_timer_.destroy();
        motion_gpu_timer_.destroy();
        pipeline_.destroy();
    }
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        eglTerminate(display_);
    }
    context_ = EGL_NO_CONTEXT;
    surface_ = EGL_NO_SURFACE;
    display_ = EGL_NO_DISPLAY;
    if (active_window_) ANativeWindow_release(active_window_);
    active_window_ = nullptr;
}

int EglPresenter::bytes_per_pixel(PixelFormat format) {
    switch (format) {
        case PixelFormat::RGB565: return 2;
        case PixelFormat::RGB888: return 3;
        case PixelFormat::RGBA8888: return 4;
    }
    return 0;
}

}  // namespace flynes::video
