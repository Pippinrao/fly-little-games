#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/native_window.h>
#include <android/asset_manager.h>

#include <condition_variable>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <deque>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "baseline_pipeline.h"
#include "gpu_timer_query.h"
#include "motion_compute_pipeline.h"
#include "presentation_coordinator.h"
#include "video_metrics.h"
#include "video/temporal_scheduler.h"

namespace flynes::video {

class EglPresenter {
public:
    explicit EglPresenter(AAssetManager* assets, bool swappy_initialized = false);
    ~EglPresenter();
    EglPresenter(const EglPresenter&) = delete;
    EglPresenter& operator=(const EglPresenter&) = delete;

    // Takes ownership of the ANativeWindow reference even on failure.
    bool surface_created(ANativeWindow* window, std::uint64_t epoch);
    void surface_changed(int width, int height, std::uint64_t epoch);
    bool surface_destroyed(std::uint64_t epoch);
    bool enqueue(const void* pixels, std::size_t capacity, std::uint64_t sequence,
                 int width, int height, int pitch, int format, int bytes);
    void set_filter(int filter);
    void set_active(bool active);
    void reset_sequence();
    bool request_frame_rate(std::uint64_t epoch, float source_fps);
    bool clear_frame_rate(std::uint64_t epoch);
    bool configure_motion(std::uint64_t epoch, std::uint64_t display_generation,
                          float display_hz, float source_fps,
                          std::int64_t lease_deadline_ns, bool force_pacer_disabled);
    bool update_motion_lease(std::uint64_t epoch, std::uint64_t display_generation,
                             std::int64_t lease_deadline_ns);
    std::int64_t begin_motion_shadow(std::uint64_t epoch, float source_fps);
    std::int64_t exit_motion(std::uint64_t epoch, bool drain_to_immediate);
    std::int64_t actual_real_presentation_ns(std::uint64_t sequence) const;
    /** Returns false after a bounded wait; caller must intentionally leak this instance. */
    bool shutdown();
    VideoMetricsSnapshot stats() const { return metrics_.snapshot(); }

private:
    enum class Command {
        NONE, CREATE, RESIZE, VOTE_FRAME_RATE, CLEAR_FRAME_RATE, ENTER_MOTION,
        ENTER_MOTION_SHADOW,
        EXIT_MOTION_HOLD, EXIT_MOTION_DRAIN, DESTROY, STOP
    };
    struct PendingCommand {
        Command type;
        std::uint64_t id;
        std::uint64_t epoch;
        ANativeWindow* window;
        int width;
        int height;
        float frame_rate;
        std::uint64_t display_generation = 0;
        float display_hz = 0.0f;
        std::int64_t lease_deadline_ns = 0;
    bool force_pacer_disabled = false;
    };
    void thread_main();
    bool create_egl(ANativeWindow* window, int client_version);
    bool recreate_egl_exact(int client_version);
    bool recreate_egl_with_fallback(int primary_version, int fallback_version,
                                    bool* primary_ready);
    void initialize_presentation_timestamps();
    void poll_presentation_timestamps();
    void destroy_egl();
    static int bytes_per_pixel(PixelFormat format);

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable acknowledged_;
    AAssetManager* assets_ = nullptr;
    std::thread thread_;
    std::uint64_t command_id_ = 0;
    std::uint64_t acknowledged_id_ = 0;
    std::unordered_map<std::uint64_t, bool> command_results_;
    std::unordered_set<std::uint64_t> pending_result_waiters_;
    std::unordered_set<std::uint64_t> cancelled_commands_;
    std::deque<PendingCommand> commands_;
    bool stopping_ = false;
    bool thread_exited_ = false;
    enum class UiSurfaceState { EMPTY, ACTIVE, DESTROYING };
    UiSurfaceState ui_surface_state_ = UiSurfaceState::EMPTY;
    std::uint64_t ui_surface_epoch_ = 0;
    bool destroy_result_available_ = false;
    std::uint64_t destroy_result_epoch_ = 0;
    bool destroy_clear_succeeded_ = false;
    bool active_ = true;
    bool surface_ready_ = false;
    bool accepting_frames_ = false;
    int requested_filter_ = static_cast<int>(FilterMode::EDGE_ENHANCED);
    int failed_filter_ = -1;
    std::unique_ptr<StagedFrame> pending_frame_;
    std::deque<std::unique_ptr<StagedFrame>> motion_frames_;
    std::unique_ptr<StagedFrame> motion_previous_;
    std::uint64_t last_enqueued_sequence_ = 0;
    bool has_enqueued_sequence_ = false;

    PresentationCoordinator coordinator_;
    VideoMetrics metrics_;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    ANativeWindow* active_window_ = nullptr;
    BaselinePipeline pipeline_;
    GpuTimerQuery gpu_timer_;
    GpuTimerQuery motion_gpu_timer_;
    MotionComputePipeline motion_pipeline_;
    std::unique_ptr<TemporalScheduler> temporal_scheduler_;
    bool swappy_initialized_ = false;
    bool swappy_attached_ = false;
    bool motion_enabled_ = false;
    bool motion_shadow_enabled_ = false;
    bool motion_exit_queued_ = false;
    bool motion_drain_queued_ = false;
    std::uint64_t active_exit_id_ = 0;
    bool motion_reset_requested_ = false;
    float motion_source_fps_ = 0.0f;
    std::atomic<std::uint64_t> motion_display_generation_{0};
    std::atomic<std::int64_t> motion_lease_deadline_ns_{0};
    std::uint64_t motion_swaps_ = 0;
    std::uint64_t last_swappy_total_frames_ = 0;
    std::int64_t last_swappy_progress_ns_ = 0;
    struct PendingPresentation {
        EGLuint64KHR frame_id;
        std::uint64_t sequence;
        bool real_slot;
    };
    PFNEGLGETNEXTFRAMEIDANDROIDPROC get_next_frame_id_ = nullptr;
    PFNEGLGETFRAMETIMESTAMPSANDROIDPROC get_frame_timestamps_ = nullptr;
    std::deque<PendingPresentation> pending_presentations_;
    mutable std::mutex actual_presentations_mutex_;
    std::deque<std::pair<std::uint64_t, std::int64_t>> actual_real_presentations_;
};

}  // namespace flynes::video
