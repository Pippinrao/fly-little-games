#include "harmony_renderer.hpp"
#include "harmony_spatial_pipeline.hpp"
#include "motion_compute_pipeline.h"

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <native_vsync/native_vsync.h>
#include <window_manager/oh_display_manager.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <thread>
#include <utility>

namespace flynes::harmony {
namespace {

constexpr char kVertexShader[] = R"(
#version 300 es
layout(location=0) in vec2 aPosition;
layout(location=1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() { gl_Position=vec4(aPosition,0.0,1.0); vTexCoord=aTexCoord; }
)";

constexpr char kNearestShader[] = R"(
#version 300 es
precision mediump float;
uniform sampler2D uTexture;
in vec2 vTexCoord;
out vec4 fragColor;
void main() { fragColor=texture(uTexture,vTexCoord); }
)";

constexpr char kSharpShader[] = R"(
#version 300 es
precision mediump float;
uniform sampler2D uTexture;
uniform vec2 uTextureSize;
in vec2 vTexCoord;
out vec4 fragColor;
void main() {
 vec2 pixel=vTexCoord*uTextureSize-vec2(0.5); vec2 base=floor(pixel);
 vec2 sharpFraction=clamp((fract(pixel)-vec2(0.5))*2.0+vec2(0.5),0.0,1.0);
 vec2 uv=(base+sharpFraction+vec2(0.5))/uTextureSize;
 fragColor=texture(uTexture,uv);
}
)";

constexpr char kCrtShader[] = R"(
#version 300 es
precision mediump float;
uniform sampler2D uTexture;
uniform vec2 uOutputSize;
in vec2 vTexCoord;
out vec4 fragColor;
void main() {
 vec4 color=texture(uTexture,vTexCoord);
 float scanline=0.88+0.12*sin(vTexCoord.y*uOutputSize.y*3.14159265);
 float mask=0.96+0.04*sin(vTexCoord.x*uOutputSize.x*2.0943951);
 vec2 edge=vTexCoord*(vec2(1.0)-vTexCoord);
 float vignette=clamp(pow(16.0*edge.x*edge.y,0.12),0.78,1.0);
 fragColor=vec4(color.rgb*scanline*mask*vignette,color.a);
}
)";

constexpr GLfloat kQuad[] = {
    -1.0F, -1.0F, 0.0F, 1.0F,
     1.0F, -1.0F, 1.0F, 1.0F,
    -1.0F,  1.0F, 0.0F, 0.0F,
     1.0F,  1.0F, 1.0F, 0.0F,
};

GLuint compile_shader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE)
    {
        return shader;
    }
    glDeleteShader(shader);
    return 0;
}

GLuint link_program(const char* fragment)
{
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint pixel = compile_shader(GL_FRAGMENT_SHADER, fragment);
    if (vertex == 0 || pixel == 0)
    {
        if (vertex != 0) glDeleteShader(vertex);
        if (pixel != 0) glDeleteShader(pixel);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, pixel);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(pixel);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE)
    {
        return program;
    }
    glDeleteProgram(program);
    return 0;
}

} // namespace

struct HarmonyRenderer::Impl final
{
    mutable std::mutex mutex;
    std::condition_variable wake;
    RenderMailbox mailbox;
    DisplayPolicy display_policy;
    MotionFrameScheduler motion_scheduler;
    OH_NativeXComponent* component = nullptr;
    OH_NativeXComponent_Callback callbacks{};
    void* window = nullptr;
    std::thread render_thread;
    std::uint64_t generation = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool stop = false;
    bool vsync_arrived = false;
    bool vsync_pending = false;
    HarmonyRenderStatus runtime_status;
    std::int64_t last_vsync_ns = 0;
    std::uint64_t display_config_version = 1;

    static std::int64_t monotonic_now_ns()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    static Impl*& registered()
    {
        static Impl* value = nullptr;
        return value;
    }

    static void surface_created(OH_NativeXComponent* component_value, void* window_value)
    {
        Impl* self = registered();
        if (self != nullptr) self->create(component_value, window_value);
    }

    static void surface_changed(OH_NativeXComponent* component_value, void* window_value)
    {
        Impl* self = registered();
        if (self != nullptr) self->resize(component_value, window_value);
    }

    static void surface_destroyed(OH_NativeXComponent*, void* window_value)
    {
        Impl* self = registered();
        if (self != nullptr) self->destroy(window_value);
    }

    static void touch_event(OH_NativeXComponent*, void*) {}

    static void vsync_callback(long long timestamp, void* data)
    {
        auto* self = static_cast<Impl*>(data);
        std::lock_guard lock(self->mutex);
        self->vsync_pending = false;
        if (self->stop)
        {
            self->wake.notify_all();
            return;
        }
        if (self->last_vsync_ns > 0 && timestamp > self->last_vsync_ns)
        {
            self->runtime_status.vsync_period_ns = timestamp - self->last_vsync_ns;
            self->runtime_status.timing_valid = true;
        }
        self->last_vsync_ns = timestamp;
        self->vsync_arrived = true;
        self->wake.notify_all();
    }

    void set_failure(const std::string& reason)
    {
        std::lock_guard lock(mutex);
        runtime_status.native_ready = false;
        runtime_status.fallback_active = true;
        runtime_status.fallback_reason = reason;
    }

    void create(OH_NativeXComponent* component_value, void* window_value)
    {
        stop_and_join();
        std::uint64_t next_generation = 0;
        {
            std::lock_guard lock(mutex);
            component = component_value;
            window = window_value;
            stop = false;
            vsync_arrived = false;
            vsync_pending = false;
            last_vsync_ns = 0;
            ++generation;
            next_generation = generation;
            uint64_t next_width = 0;
            uint64_t next_height = 0;
            if (OH_NativeXComponent_GetXComponentSize(component, window, &next_width, &next_height) == 0)
            {
                width = static_cast<std::uint32_t>(next_width);
                height = static_cast<std::uint32_t>(next_height);
            }
            runtime_status.fallback_active = false;
            runtime_status.fallback_reason.clear();
            runtime_status.timing_valid = false;
            runtime_status.vsync_period_ns = 0;
        }
        std::uint32_t created_width = 0;
        std::uint32_t created_height = 0;
        {
            std::lock_guard lock(mutex);
            created_width = width;
            created_height = height;
        }
        if (created_width == 0 || created_height == 0 ||
            !mailbox.create_surface(next_generation, created_width, created_height))
        {
            set_failure("invalid XComponent surface size");
            return;
        }
        display_policy.begin_surface(next_generation);
        motion_scheduler.create_surface(next_generation);
        try
        {
            render_thread = std::thread([this, next_generation, window_value] {
                render_loop(next_generation, window_value);
            });
        }
        catch (...)
        {
            mailbox.destroy_surface(next_generation);
            set_failure("render thread creation failed");
        }
    }

    void resize(OH_NativeXComponent* component_value, void* window_value)
    {
        uint64_t next_width = 0;
        uint64_t next_height = 0;
        if (OH_NativeXComponent_GetXComponentSize(
                component_value, window_value, &next_width, &next_height) != 0)
        {
            return;
        }
        std::uint64_t current_generation = 0;
        {
            std::lock_guard lock(mutex);
            if (window != window_value) return;
            width = static_cast<std::uint32_t>(next_width);
            height = static_cast<std::uint32_t>(next_height);
            current_generation = generation;
        }
        mailbox.resize_surface(current_generation, width, height);
    }

    void destroy(void* window_value)
    {
        {
            std::lock_guard lock(mutex);
            if (window != window_value) return;
        }
        stop_and_join();
        std::uint64_t old_generation = 0;
        {
            std::lock_guard lock(mutex);
            old_generation = generation;
            window = nullptr;
            width = 0;
            height = 0;
            runtime_status.native_ready = false;
        }
        mailbox.destroy_surface(old_generation);
        display_policy.end_surface(old_generation);
        motion_scheduler.destroy_surface(old_generation);
    }

    void stop_and_join()
    {
        {
            std::lock_guard lock(mutex);
            stop = true;
            wake.notify_all();
        }
        if (render_thread.joinable()) render_thread.join();
    }

    bool request_vsync(OH_NativeVSync* vsync)
    {
        {
            std::lock_guard lock(mutex);
            if (stop) return false;
            vsync_pending = true;
        }
        if (OH_NativeVSync_RequestFrame(vsync, &Impl::vsync_callback, this) == 0)
        {
            return true;
        }
        std::lock_guard lock(mutex);
        vsync_pending = false;
        return false;
    }

    void render_loop(std::uint64_t owned_generation, void* owned_window)
    {
        EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLConfig config = nullptr;
        EGLContext context = EGL_NO_CONTEXT;
        EGLSurface surface = EGL_NO_SURFACE;
        GLuint texture = 0;
        GLuint program = 0;
        HarmonySpatialPipeline spatial_pipeline;
        flynes::video::MotionComputePipeline motion_pipeline;
        bool motion_initialized = false;
        OH_NativeVSync* vsync = nullptr;
        std::int32_t active_spatial = -1;
        std::int32_t active_post = -1;
        std::uint64_t applied_display_config_version = 0;
        std::uint64_t presented_attempts = 0;

        auto cleanup = [&] {
            if (display != EGL_NO_DISPLAY)
            {
                spatial_pipeline.destroy();
                motion_pipeline.destroy();
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                if (program != 0) glDeleteProgram(program);
                if (texture != 0) glDeleteTextures(1, &texture);
                if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
                if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
                eglTerminate(display);
            }
            if (vsync != nullptr) OH_NativeVSync_Destroy(vsync);
        };

        EGLint major = 0;
        EGLint minor = 0;
        if (display == EGL_NO_DISPLAY || eglInitialize(display, &major, &minor) != EGL_TRUE)
        {
            set_failure("EGL display initialization failed");
            cleanup();
            return;
        }
        constexpr EGLint attributes[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_NONE,
        };
        EGLint count = 0;
        if (eglChooseConfig(display, attributes, &config, 1, &count) != EGL_TRUE || count < 1)
        {
            set_failure("EGL ES 3 configuration unavailable");
            cleanup();
            return;
        }
        constexpr EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
        surface = eglCreateWindowSurface(display, config,
                                         reinterpret_cast<EGLNativeWindowType>(owned_window), nullptr);
        if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
            eglMakeCurrent(display, surface, surface, context) != EGL_TRUE)
        {
            set_failure("EGL context or window surface creation failed");
            cleanup();
            return;
        }
        eglSwapInterval(display, 1);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        auto apply_display_request = [&] {
            std::int32_t refresh_policy = 1;
            OH_NativeXComponent* active_component = nullptr;
            {
                std::lock_guard lock(mutex);
                refresh_policy = runtime_status.display.refresh_policy;
                active_component = component;
                applied_display_config_version = display_config_version;
            }
            std::int32_t target_hz = 0;
            if (refresh_policy == 3) target_hz = 60;
            else if (refresh_policy == 4) target_hz = 90;
            else if (refresh_policy == 5) target_hz = 120;
            bool accepted = true;
            if (target_hz > 0)
            {
                OH_NativeXComponent_ExpectedRateRange component_range{
                    std::max(30, target_hz / 2), target_hz, target_hz};
                accepted = active_component != nullptr &&
                    OH_NativeXComponent_SetExpectedFrameRateRange(
                        active_component, &component_range) == 0;
            }
            display_policy.record_request(owned_generation, accepted);
        };
        apply_display_request();
        const char name[] = "FlyNESHarmonyRenderer";
        vsync = OH_NativeVSync_Create(name, sizeof(name) - 1);
        if (texture == 0 || vsync == nullptr)
        {
            set_failure("GPU texture or NativeVSync creation failed");
            cleanup();
            return;
        }
        {
            std::lock_guard lock(mutex);
            runtime_status.native_ready = true;
            runtime_status.fallback_active = false;
            runtime_status.fallback_reason.clear();
        }

        if (!request_vsync(vsync))
        {
            set_failure("NativeVSync request failed");
            cleanup();
            return;
        }

        for (;;)
        {
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stop || vsync_arrived; });
                if (stop) break;
                vsync_arrived = false;
            }

            std::uint64_t wanted_display_config_version = 0;
            {
                std::lock_guard lock(mutex);
                wanted_display_config_version = display_config_version;
            }
            if (wanted_display_config_version != applied_display_config_version)
            {
                apply_display_request();
            }

            std::uint32_t actual_hz = 0;
            if (OH_NativeDisplayManager_GetDefaultDisplayRefreshRate(&actual_hz) ==
                    DISPLAY_MANAGER_OK && actual_hz > 0)
            {
                display_policy.observe(owned_generation,
                                       static_cast<std::int32_t>(actual_hz * 1000U),
                                       monotonic_now_ns());
            }

            const DisplayPolicyStatus display_status = display_policy.status(monotonic_now_ns());
            const bool motion_path = display_status.motion_qualified;
            const RenderDecision decision = motion_path ? RenderDecision{} :
                mailbox.next_decision(owned_generation);
            const MotionDecision motion_decision = motion_path
                ? motion_scheduler.next_slot(owned_generation, true) : MotionDecision{};
            MotionFrame render_frame;
            bool should_present = false;
            bool synth_decision = false;
            bool synth_generated = false;
            std::string temporal_state = "NATIVE";
            std::string temporal_fallback;
            if (motion_path)
            {
                if (motion_decision.kind == MotionDecisionKind::PRIMING)
                {
                    temporal_state = "PRIMING";
                }
                else if (motion_decision.kind == MotionDecisionKind::SOURCE)
                {
                    temporal_state = "MOTION";
                    render_frame = motion_decision.a;
                    should_present = true;
                }
                else if (motion_decision.kind == MotionDecisionKind::SYNTHESIZE)
                {
                    temporal_state = "MOTION";
                    synth_decision = true;
                    render_frame = motion_decision.a;
                    if (!motion_initialized) motion_initialized = motion_pipeline.initialize();
                    if (motion_initialized)
                    {
                        const auto synthesized = motion_pipeline.interpolate(
                            reinterpret_cast<const std::uint16_t*>(motion_decision.a.rgb565.data()),
                            reinterpret_cast<const std::uint16_t*>(motion_decision.b.rgb565.data()),
                            static_cast<int>(motion_decision.a.width),
                            static_cast<int>(motion_decision.a.height));
                        if (synthesized.failure ==
                                flynes::video::MotionComputePipeline::Failure::NONE &&
                            synthesized.generated)
                        {
                            render_frame.rgb565.resize(
                                synthesized.pixels.size() * sizeof(std::uint16_t));
                            std::memcpy(render_frame.rgb565.data(), synthesized.pixels.data(),
                                        render_frame.rgb565.size());
                            synth_generated = true;
                        }
                        else if (synthesized.failure !=
                                 flynes::video::MotionComputePipeline::Failure::NONE)
                        {
                            temporal_fallback = "motion compute unavailable";
                        }
                    }
                    else
                    {
                        temporal_fallback = "ES3.1 motion compute unavailable";
                    }
                    if (!synth_generated) temporal_state = "HOLD";
                    should_present = true;
                }
            }
            else if (decision.kind == RenderDecisionKind::UPLOAD_AND_PRESENT)
            {
                render_frame.frame_index = decision.frame.frame_index;
                render_frame.width = decision.frame.width;
                render_frame.height = decision.frame.height;
                render_frame.rgb565 = decision.frame.rgb565;
                should_present = true;
                if (display_status.temporal_mode == 2)
                {
                    temporal_fallback = display_status.fallback_reason;
                }
                (void)motion_scheduler.next_slot(owned_generation, false);
            }
            {
                std::lock_guard lock(mutex);
                runtime_status.temporal_state = temporal_state;
                runtime_status.temporal_fallback_reason = temporal_fallback;
            }
            bool presented = false;
            if (should_present)
            {
                ++presented_attempts;
                const bool sample_gpu = presented_attempts % 60 == 0;
                const std::int64_t gpu_started_ns = sample_gpu ? monotonic_now_ns() : 0;
                std::uint32_t output_width = 0;
                std::uint32_t output_height = 0;
                std::int32_t wanted_spatial = 1;
                std::int32_t wanted_post = 1;
                {
                    std::lock_guard lock(mutex);
                    output_width = width;
                    output_height = height;
                    wanted_spatial = runtime_status.requested_spatial;
                    wanted_post = runtime_status.requested_post;
                }
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                             static_cast<GLsizei>(render_frame.width),
                             static_cast<GLsizei>(render_frame.height), 0, GL_RGB,
                             GL_UNSIGNED_SHORT_5_6_5, render_frame.rgb565.data());

                std::int32_t effective_spatial = wanted_spatial;
                GLuint draw_texture = texture;
                int draw_width = static_cast<int>(render_frame.width);
                int draw_height = static_cast<int>(render_frame.height);
                std::string fallback_reason;
                if (wanted_spatial >= 3)
                {
                    if (spatial_pipeline.render(wanted_spatial, texture, draw_width, draw_height))
                    {
                        draw_texture = spatial_pipeline.output_texture();
                        draw_width = spatial_pipeline.output_width();
                        draw_height = spatial_pipeline.output_height();
                    }
                    else
                    {
                        effective_spatial = 1;
                        fallback_reason = spatial_pipeline.failure() ==
                            HarmonySpatialPipeline::Failure::FRAMEBUFFER
                            ? "advanced spatial framebuffer unsupported"
                            : "advanced spatial shader or draw failed";
                    }
                }
                const std::int32_t program_key = wanted_post == 2 ? 100
                    : (effective_spatial == 2 || effective_spatial >= 3 ? 2 : 1);
                if (active_spatial != program_key || active_post != wanted_post)
                {
                    if (program != 0) glDeleteProgram(program);
                    const char* fragment = wanted_post == 2 ? kCrtShader
                        : (program_key == 2 ? kSharpShader : kNearestShader);
                    program = link_program(fragment);
                    active_spatial = program_key;
                    active_post = wanted_post;
                }
                {
                    std::lock_guard lock(mutex);
                    runtime_status.effective_spatial = effective_spatial;
                    runtime_status.fallback_active = !fallback_reason.empty();
                    runtime_status.fallback_reason = fallback_reason;
                }
                if (program != 0 && glGetError() == GL_NO_ERROR)
                {
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glViewport(0, 0, static_cast<GLsizei>(output_width),
                               static_cast<GLsizei>(output_height));
                    glClear(GL_COLOR_BUFFER_BIT);
                    glUseProgram(program);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, draw_texture);
                    const GLint filter = effective_spatial == 2 || effective_spatial >= 3 ||
                        wanted_post == 2
                        ? GL_LINEAR : GL_NEAREST;
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
                    const GLint sampler = glGetUniformLocation(program, "uTexture");
                    if (sampler >= 0) glUniform1i(sampler, 0);
                    const GLint texture_size = glGetUniformLocation(program, "uTextureSize");
                    if (texture_size >= 0)
                    {
                        glUniform2f(texture_size, static_cast<GLfloat>(draw_width),
                                    static_cast<GLfloat>(draw_height));
                    }
                    const GLint output_size = glGetUniformLocation(program, "uOutputSize");
                    if (output_size >= 0)
                    {
                        glUniform2f(output_size, static_cast<GLfloat>(output_width),
                                    static_cast<GLfloat>(output_height));
                    }
                    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), kQuad);
                    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), kQuad + 2);
                    glEnableVertexAttribArray(0);
                    glEnableVertexAttribArray(1);
                    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                    glDisableVertexAttribArray(0);
                    glDisableVertexAttribArray(1);
                    const bool draw_ok = glGetError() == GL_NO_ERROR;
                    if (sample_gpu && draw_ok)
                    {
                        glFinish();
                        const std::int64_t elapsed = monotonic_now_ns() - gpu_started_ns;
                        std::lock_guard lock(mutex);
                        runtime_status.gpu_timing_valid = elapsed > 0;
                        runtime_status.gpu_time_ns = elapsed > 0 ? elapsed : 0;
                        runtime_status.gpu_time_max_ns = std::max(
                            runtime_status.gpu_time_max_ns, runtime_status.gpu_time_ns);
                        ++runtime_status.gpu_timing_samples;
                    }
                    presented = draw_ok && eglSwapBuffers(display, surface) == EGL_TRUE;
                }
                if (motion_path)
                {
                    mailbox.record_external_present(owned_generation, presented);
                    if (synth_decision)
                    {
                        motion_scheduler.complete(motion_decision, synth_generated, presented);
                    }
                }
                else
                {
                    mailbox.complete(decision, presented);
                }
                if (!presented) set_failure("GPU upload, draw, or swap failed");
            }
            if (!request_vsync(vsync))
            {
                set_failure("NativeVSync request failed");
                break;
            }
        }

        cleanup();
    }
};

HarmonyRenderer::HarmonyRenderer() : impl_(new Impl)
{
    Impl::registered() = impl_;
}

HarmonyRenderer::~HarmonyRenderer()
{
    impl_->stop_and_join();
    Impl::registered() = nullptr;
    delete impl_;
}

bool HarmonyRenderer::bind_component(napi_env env, napi_value exports)
{
    bool has_component = false;
    if (napi_has_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &has_component) != napi_ok ||
        !has_component)
    {
        return false;
    }
    napi_value wrapped = nullptr;
    if (napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &wrapped) != napi_ok)
    {
        return false;
    }
    OH_NativeXComponent* component = nullptr;
    if (napi_unwrap(env, wrapped, reinterpret_cast<void**>(&component)) != napi_ok ||
        component == nullptr)
    {
        return false;
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->component = component;
    }
    impl_->callbacks.OnSurfaceCreated = &Impl::surface_created;
    impl_->callbacks.OnSurfaceChanged = &Impl::surface_changed;
    impl_->callbacks.OnSurfaceDestroyed = &Impl::surface_destroyed;
    impl_->callbacks.DispatchTouchEvent = &Impl::touch_event;
    const bool registered = OH_NativeXComponent_RegisterCallback(component, &impl_->callbacks) == 0;
    std::lock_guard lock(impl_->mutex);
    impl_->runtime_status.component_bound = registered;
    return registered;
}

bool HarmonyRenderer::submit_frame(std::uint64_t frame_index,
                                   std::uint32_t width,
                                   std::uint32_t height,
                                   const std::vector<std::uint8_t>& rgb565)
{
    std::uint64_t generation = 0;
    {
        std::lock_guard lock(impl_->mutex);
        generation = impl_->generation;
    }
    const bool native_accepted = impl_->mailbox.submit_source_frame(
        generation, frame_index, width, height, rgb565);
    const bool motion_accepted = impl_->motion_scheduler.submit(
        generation, frame_index, width, height, rgb565);
    return native_accepted && motion_accepted;
}

void HarmonyRenderer::configure(std::int32_t refresh_policy,
                                std::int32_t temporal_mode,
                                std::int32_t spatial,
                                std::int32_t post,
                                bool adaptive_protection)
{
    impl_->display_policy.configure(refresh_policy, temporal_mode, adaptive_protection);
    std::lock_guard lock(impl_->mutex);
    impl_->runtime_status.requested_spatial = std::clamp(spatial, 1, 4);
    impl_->runtime_status.requested_post = std::clamp(post, 1, 2);
    impl_->runtime_status.display.refresh_policy = std::clamp(refresh_policy, 1, 5);
    impl_->runtime_status.display.temporal_mode = std::clamp(temporal_mode, 1, 2);
    ++impl_->display_config_version;
}

void HarmonyRenderer::set_paused(bool paused)
{
    impl_->mailbox.set_paused(paused);
    impl_->motion_scheduler.set_paused(paused);
}

void HarmonyRenderer::set_protection(bool thermal_limited, bool low_battery,
                                     bool observation_valid)
{
    impl_->display_policy.set_protection(
        thermal_limited, low_battery, observation_valid);
}

HarmonyRenderStatus HarmonyRenderer::status() const
{
    HarmonyRenderStatus result;
    {
        std::lock_guard lock(impl_->mutex);
        result = impl_->runtime_status;
    }
    result.mailbox = impl_->mailbox.status();
    result.display = impl_->display_policy.status(Impl::monotonic_now_ns());
    result.motion = impl_->motion_scheduler.status();
    return result;
}

HarmonyRenderer& harmony_renderer()
{
    static HarmonyRenderer renderer;
    return renderer;
}

} // namespace flynes::harmony
