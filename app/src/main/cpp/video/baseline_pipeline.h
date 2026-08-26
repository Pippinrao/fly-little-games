#pragma once

#include <GLES2/gl2.h>
#include <android/asset_manager.h>

#include "framebuffer_target.h"
#include "mmpx_pass.h"
#include "presenter_types.h"
#include "scalefx_pipeline.h"
#include "shader_program.h"

namespace flynes::video {

/** Pixel-identical native port of the legacy Java GLES2 filters. */
class BaselinePipeline {
public:
    enum class Failure : int { NONE = 0, SHADER = 1, FRAMEBUFFER = 2, GL = 3 };
    bool initialize(AAssetManager* assets = nullptr);
    void destroy();
    void resize(int width, int height);
    void set_filter(FilterMode mode) { requested_filter_ = mode; }
    bool render(const StagedFrame& frame);
    Failure last_failure() const { return last_failure_; }

private:
    bool ensure_program();
    bool upload(const StagedFrame& frame);
    bool render_advanced(FilterMode mode);
    bool composite(GLuint texture, int width, int height);
    static GLuint compile(GLenum type, const char* source);
    static GLuint link(const char* vertex, const char* fragment);
    void uniform2f(const char* name, float first, float second);

    GLuint program_ = 0;
    GLuint texture_ = 0;
    int texture_width_ = 0;
    int texture_height_ = 0;
    PixelFormat texture_format_ = PixelFormat::RGB565;
    int output_width_ = 0;
    int output_height_ = 0;
    FilterMode requested_filter_ = FilterMode::EDGE_ENHANCED;
    int active_filter_ = -1;
    AAssetManager* assets_ = nullptr;
    bool mmpx_initialized_ = false;
    bool scalefx_initialized_ = false;
    MmpxPass mmpx_;
    ScaleFxPipeline scalefx_;
    FramebufferTarget reconstruction_;
    ShaderProgram composite_program_;
    Failure last_failure_ = Failure::NONE;
};

}  // namespace flynes::video
