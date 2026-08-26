#pragma once

#include <EGL/egl.h>
#include <android/native_window.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <deque>
#include <thread>

#include "baseline_pipeline.h"
#include "presentation_coordinator.h"
#include "video_metrics.h"

namespace flynes::video {

class EglPresenter {
public:
    EglPresenter();
    ~EglPresenter();
    EglPresenter(const EglPresenter&) = delete;
    EglPresenter& operator=(const EglPresenter&) = delete;

    // Takes ownership of the ANativeWindow reference even on failure.
    bool surface_created(ANativeWindow* window, std::uint64_t epoch);
    void surface_changed(int width, int height, std::uint64_t epoch);
    void surface_destroyed(std::uint64_t epoch);
    bool enqueue(const void* pixels, std::size_t capacity, std::uint64_t sequence,
                 int width, int height, int pitch, int format, int bytes);
    void set_filter(int filter);
    void set_active(bool active);
    void reset_sequence();
    VideoMetricsSnapshot stats() const { return metrics_.snapshot(); }

private:
    enum class Command { NONE, CREATE, RESIZE, DESTROY, STOP };
    struct PendingCommand {
        Command type;
        std::uint64_t id;
        std::uint64_t epoch;
        ANativeWindow* window;
        int width;
        int height;
    };
    void thread_main();
    bool create_egl(ANativeWindow* window);
    void destroy_egl();
    static int bytes_per_pixel(PixelFormat format);

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable acknowledged_;
    std::thread thread_;
    std::uint64_t command_id_ = 0;
    std::uint64_t acknowledged_id_ = 0;
    std::deque<PendingCommand> commands_;
    bool stopping_ = false;
    bool active_ = true;
    bool surface_ready_ = false;
    bool accepting_frames_ = false;
    int requested_filter_ = static_cast<int>(FilterMode::EDGE_ENHANCED);
    std::unique_ptr<StagedFrame> pending_frame_;
    std::uint64_t last_enqueued_sequence_ = 0;
    bool has_enqueued_sequence_ = false;

    PresentationCoordinator coordinator_;
    VideoMetrics metrics_;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    ANativeWindow* active_window_ = nullptr;
    BaselinePipeline pipeline_;
};

}  // namespace flynes::video
