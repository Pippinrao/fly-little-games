#include "presentation_coordinator.h"

#include <dlfcn.h>
#include <sys/system_properties.h>

#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace flynes::video {

struct FrameRatePlatformCall {
    ANativeWindow* window = nullptr;
    float frame_rate = 0.0f;
    std::mutex mutex;
    std::condition_variable finished;
    bool complete = false;
    bool timed_out = false;
    // A late positive vote is not complete until its same-worker compensation
    // clear has also completed. This is therefore safe to consume as a clear result.
    bool clear_succeeded = false;
    std::int32_t platform_result = -1;
    ~FrameRatePlatformCall() {
        if (window) ANativeWindow_release(window);
    }
};

bool PresentationCoordinator::begin_create(std::uint64_t epoch) {
    if (epoch == 0 || epoch <= epoch_) return false;
    epoch_ = epoch;
    requested_source_fps_ = 0.0f;
    state_ = State::CREATING;
    return true;
}

void PresentationCoordinator::complete_create(std::uint64_t epoch, bool ready) {
    if (epoch == epoch_ && state_ == State::CREATING) {
        state_ = ready ? State::READY : State::EMPTY;
    }
}

bool PresentationCoordinator::begin_destroy(std::uint64_t epoch) {
    if (epoch != epoch_ || (state_ != State::READY && state_ != State::CREATING)) return false;
    state_ = State::DESTROYING;
    return true;
}

void PresentationCoordinator::complete_destroy(std::uint64_t epoch) {
    if (epoch == epoch_ && state_ == State::DESTROYING) {
        requested_source_fps_ = 0.0f;
        state_ = State::EMPTY;
    }
}

bool PresentationCoordinator::is_active(std::uint64_t epoch) const {
    return epoch == epoch_ && state_ == State::READY;
}

bool PresentationCoordinator::owns_epoch(std::uint64_t epoch) const {
    return epoch == epoch_ && state_ != State::EMPTY;
}

namespace {
using SetFrameRate = std::int32_t (*)(ANativeWindow*, float, std::int8_t);

bool is_virtual_device() {
    char value[PROP_VALUE_MAX] = {};
    return (__system_property_get("ro.kernel.qemu", value) > 0
                    && std::strcmp(value, "1") == 0)
            || (__system_property_get("ro.boot.qemu", value) > 0
                    && std::strcmp(value, "1") == 0);
}

SetFrameRate frame_rate_api() {
    // Goldfish/ranchu advertises the API but does not model a physical panel's
    // frame-rate negotiation reliably; invoking it can stall its compositor.
    // AVD contracts therefore report an explicit capability fallback.
    if (is_virtual_device()) return nullptr;
    static void* native_window = dlopen("libnativewindow.so", RTLD_NOW | RTLD_LOCAL);
    static SetFrameRate api = native_window == nullptr ? nullptr
            : reinterpret_cast<SetFrameRate>(
                    dlsym(native_window, "ANativeWindow_setFrameRate"));
    return api;
}

struct FrameRateCallGate {
    std::mutex mutex;
    std::shared_ptr<FrameRatePlatformCall> in_flight;
};

std::shared_ptr<FrameRateCallGate> frame_rate_gate() {
    static auto gate = std::make_shared<FrameRateCallGate>();
    return gate;
}

void run_frame_rate_call(const std::shared_ptr<FrameRateCallGate>& gate,
                         const std::shared_ptr<FrameRatePlatformCall>& call,
                         SetFrameRate api) {
    const std::int32_t result = api(call->window, call->frame_rate,
            ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
    bool timed_out = false;
    {
        std::unique_lock lock(call->mutex);
        timed_out = call->timed_out;
        call->platform_result = result;
        if (!timed_out) {
            call->clear_succeeded = call->frame_rate == 0.0f && result == 0;
            // Keep the call lock across gate retirement and completion publish.
            // The waiter therefore either sets timed_out first or observes complete;
            // there is no state in which the worker saw false but the waiter can
            // subsequently turn it true.
            {
                std::lock_guard gate_lock(gate->mutex);
                if (gate->in_flight == call) gate->in_flight.reset();
            }
            call->complete = true;
            lock.unlock();
            call->finished.notify_all();
            return;
        }
    }
    std::int32_t compensation_result = -1;
    if (timed_out && call->frame_rate > 0.0f && result == 0) {
        compensation_result = api(call->window, 0.0f,
                ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
    }
    {
        std::lock_guard lock(call->mutex);
        call->clear_succeeded = call->frame_rate == 0.0f
                ? result == 0
                : (timed_out ? (result != 0 || compensation_result == 0) : false);
    }
    // Clear the global gate before publishing completion. A woken clear/request
    // can therefore never observe a completed call as a false BUSY.
    {
        std::lock_guard lock(gate->mutex);
        if (gate->in_flight == call) gate->in_flight.reset();
    }
    {
        std::lock_guard lock(call->mutex);
        call->complete = true;
    }
    call->finished.notify_all();
}

enum class StartCallResult { COMPLETED, PENDING, BUSY, START_FAILED };

std::shared_ptr<FrameRatePlatformCall> retain_clear_retry_window(ANativeWindow* window) {
    if (!window) return nullptr;
    auto retry = std::make_shared<FrameRatePlatformCall>();
    retry->window = window;
    retry->frame_rate = 0.0f;
    retry->complete = true;
    retry->clear_succeeded = false;
    ANativeWindow_acquire(window);
    return retry;
}

StartCallResult start_frame_rate_call(SetFrameRate api, ANativeWindow* window,
                                      float frame_rate,
                                      std::shared_ptr<FrameRatePlatformCall>* operation,
                                      std::int32_t* immediate_result) {
    constexpr auto timeout = std::chrono::milliseconds(400);
    auto gate = frame_rate_gate();
    auto call = std::make_shared<FrameRatePlatformCall>();
    call->frame_rate = frame_rate;
    {
        std::lock_guard lock(gate->mutex);
        // Never queue behind a platform API that may be permanently blocked.
        // Callers retry the one bounded operation instead of accumulating windows.
        if (gate->in_flight) return StartCallResult::BUSY;
        ANativeWindow_acquire(window);
        // Publish ownership only after acquire. A BUSY return must leave the
        // caller's active-window reference untouched.
        call->window = window;
        gate->in_flight = call;
    }
    *operation = call;
    try {
        std::thread([gate, call, api] { run_frame_rate_call(gate, call, api); }).detach();
    } catch (...) {
        std::lock_guard lock(gate->mutex);
        if (gate->in_flight == call) gate->in_flight.reset();
        operation->reset();
        return StartCallResult::START_FAILED;
    }
    std::unique_lock lock(call->mutex);
    if (!call->finished.wait_for(lock, timeout, [&] { return call->complete; })) {
        call->timed_out = true;
        return StartCallResult::PENDING;
    }
    *immediate_result = call->platform_result;
    return StartCallResult::COMPLETED;
}

}  // namespace

PresentationCoordinator::FrameRateVoteResult
PresentationCoordinator::request_frame_rate(std::uint64_t epoch,
                                             ANativeWindow* window,
                                             float source_fps) {
    if (!is_active(epoch) || !window || !std::isfinite(source_fps) || source_fps <= 0.0f) {
        return FrameRateVoteResult::STALE_EPOCH;
    }
    SetFrameRate api = frame_rate_api();
    if (!api) return FrameRateVoteResult::UNSUPPORTED;
    if (pending_platform_call_
            && clear_frame_rate(epoch, window) != FrameRateVoteResult::CLEARED) {
        return FrameRateVoteResult::FAILED;
    }
    std::int32_t result = -1;
    std::shared_ptr<FrameRatePlatformCall> operation;
    auto started = start_frame_rate_call(api, window, source_fps, &operation, &result);
    if (operation) pending_platform_call_ = operation;
    if (started != StartCallResult::COMPLETED || result != 0) {
        return FrameRateVoteResult::FAILED;
    }
    pending_platform_call_.reset();
    requested_source_fps_ = source_fps;
    return FrameRateVoteResult::APPLIED;
}

PresentationCoordinator::FrameRateVoteResult
PresentationCoordinator::clear_frame_rate(std::uint64_t epoch,
                                          ANativeWindow* window) {
    ANativeWindow* target_window = window;
    std::shared_ptr<FrameRatePlatformCall> prior = pending_platform_call_;
    if (prior) {
        std::unique_lock lock(prior->mutex);
        if (!prior->finished.wait_for(lock, std::chrono::milliseconds(400),
                                      [&] { return prior->complete; })) {
            return FrameRateVoteResult::FAILED;
        }
        if (prior->clear_succeeded) {
            lock.unlock();
            pending_platform_call_.reset();
            requested_source_fps_ = 0.0f;
            return FrameRateVoteResult::CLEARED;
        }
        // The completed failed call owns a reference even after EGL destroy.
        // Keep it alive until the retry has acquired its own reference.
        target_window = prior->window;
        lock.unlock();
    }
    if (!target_window || (!prior && !owns_epoch(epoch))) {
        return FrameRateVoteResult::STALE_EPOCH;
    }
    SetFrameRate api = frame_rate_api();
    if (!api) {
        requested_source_fps_ = 0.0f;
        // There cannot be an active platform vote when the API is unavailable.
        return FrameRateVoteResult::CLEARED;
    }
    std::int32_t result = -1;
    std::shared_ptr<FrameRatePlatformCall> operation;
    auto started = start_frame_rate_call(api, target_window, 0.0f,
                                         &operation, &result);
    if (operation) pending_platform_call_ = operation;
    if (started != StartCallResult::COMPLETED) {
        if (!operation && !prior) {
            pending_platform_call_ = retain_clear_retry_window(target_window);
        }
        return FrameRateVoteResult::FAILED;
    }
    requested_source_fps_ = 0.0f;
    if (result == 0) {
        pending_platform_call_.reset();
        return FrameRateVoteResult::CLEARED;
    }
    return FrameRateVoteResult::FAILED;
}

}  // namespace flynes::video
