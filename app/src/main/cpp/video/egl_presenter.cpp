#include "egl_presenter.h"

#include <android/log.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace flynes::video {

EglPresenter::EglPresenter(AAssetManager* assets)
        : assets_(assets), thread_(&EglPresenter::thread_main, this) {}

EglPresenter::~EglPresenter() {
    {
        std::unique_lock lock(mutex_);
        accepting_frames_ = false;
        pending_frame_.reset();
        stopping_ = true;
        const auto id = ++command_id_;
        commands_.push_back({Command::STOP, id, 0, nullptr, 0, 0});
        wake_.notify_one();
        acknowledged_.wait(lock, [&] { return acknowledged_id_ >= id; });
    }
    if (thread_.joinable()) thread_.join();
}

bool EglPresenter::surface_created(ANativeWindow* window, std::uint64_t epoch) {
    if (!window) return false;
    std::unique_lock lock(mutex_);
    if (stopping_ || !coordinator_.begin_create(epoch)) {
        ANativeWindow_release(window);
        return false;
    }
    accepting_frames_ = false;
    pending_frame_.reset();
    const auto id = ++command_id_;
    commands_.push_back({Command::CREATE, id, epoch, window, 0, 0});
    wake_.notify_one();
    // EGL window creation must not block SurfaceView.surfaceCreated() on the UI thread.
    return true;
}

void EglPresenter::surface_changed(int width, int height, std::uint64_t epoch) {
    if (width <= 0 || height <= 0) return;
    std::unique_lock lock(mutex_);
    if (stopping_ || !coordinator_.is_active(epoch)) return;
    const auto id = ++command_id_;
    commands_.push_back({Command::RESIZE, id, epoch, nullptr, width, height});
    wake_.notify_one();
}

void EglPresenter::surface_destroyed(std::uint64_t epoch) {
    std::unique_lock lock(mutex_);
    if (stopping_ || !coordinator_.begin_destroy(epoch)) return;
    accepting_frames_ = false;
    pending_frame_.reset();
    const auto id = ++command_id_;
    commands_.push_back({Command::DESTROY, id, epoch, nullptr, 0, 0});
    wake_.notify_one();
    acknowledged_.wait(lock, [&] { return acknowledged_id_ >= id || stopping_; });
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
    has_enqueued_sequence_ = false;
    last_enqueued_sequence_ = 0;
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
        int filter = 0;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return !commands_.empty() || pending_frame_ || stopping_; });
            if (!commands_.empty()) {
                PendingCommand pending = commands_.front();
                commands_.pop_front();
                command = pending.type;
                command_id = pending.id;
                epoch = pending.epoch;
                window = pending.window;
                width = pending.width;
                height = pending.height;
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
            if (command == Command::CREATE) {
                destroy_egl();
                created = create_egl(window);
                if (created) {
                    int native_width = ANativeWindow_getWidth(active_window_);
                    int native_height = ANativeWindow_getHeight(active_window_);
                    pipeline_.resize(std::max(1, native_width), std::max(1, native_height));
                }
            } else if (command == Command::RESIZE) {
                if (surface_ != EGL_NO_SURFACE) pipeline_.resize(width, height);
            } else if (command == Command::DESTROY || command == Command::STOP) {
                destroy_egl();
            }
            {
                std::lock_guard lock(mutex_);
                if (command == Command::CREATE) {
                    coordinator_.complete_create(epoch, created);
                    surface_ready_ = created && coordinator_.is_active(epoch);
                    accepting_frames_ = surface_ready_ && active_;
                    if (surface_ready_) metrics_.on_surface_epoch(epoch);
                } else if (command == Command::DESTROY) {
                    surface_ready_ = false;
                    accepting_frames_ = false;
                }
                acknowledged_id_ = std::max(acknowledged_id_, command_id);
                acknowledged_.notify_all();
            }
            if (command == Command::STOP) return;
            continue;
        }

        pipeline_.set_filter(static_cast<FilterMode>(filter));
        gpu_timer_.begin();
        bool rendered = frame && pipeline_.render(*frame);
        gpu_timer_.end();
        gpu_timer_.poll();
        metrics_.on_gpu_timing(static_cast<int>(gpu_timer_.status()),
                               gpu_timer_.last_duration_ns());
        if (!rendered && frame) {
            metrics_.on_runtime_failure(static_cast<int>(pipeline_.last_failure()));
            {
                std::lock_guard lock(mutex_);
                failed_filter_ = requested_filter_;
            }
            pipeline_.set_filter(FilterMode::NEAREST);
            rendered = pipeline_.render(*frame);
        }
        if (rendered) {
            metrics_.on_upload();
            if (eglSwapBuffers(display_, surface_) == EGL_TRUE) {
                metrics_.on_submit(frame->sequence);
            } else {
                __android_log_print(ANDROID_LOG_ERROR, "FlyNESVideo",
                                    "eglSwapBuffers failed: 0x%x", eglGetError());
            }
        }
    }
}

bool EglPresenter::create_egl(ANativeWindow* window) {
    active_window_ = window;
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY || eglInitialize(display_, nullptr, nullptr) != EGL_TRUE) {
        destroy_egl();
        return false;
    }
    constexpr EGLint kEs3Bit = 0x40;
    const int versions[] = {3, 2};
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
    gpu_timer_.initialize();
    metrics_.on_gpu_timing(static_cast<int>(gpu_timer_.status()),
                           gpu_timer_.last_duration_ns());
    return true;
}

void EglPresenter::destroy_egl() {
    if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT) {
        gpu_timer_.destroy();
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
