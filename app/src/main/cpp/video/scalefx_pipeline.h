#pragma once

#include <GLES2/gl2.h>
#include <android/asset_manager.h>

#include <array>
#include <string>

#include "framebuffer_target.h"
#include "shader_program.h"

namespace flynes::video {

class ScaleFxPipeline {
public:
    enum class Failure { NONE, ASSET_MISSING, SHADER_FAILURE, FRAMEBUFFER_FAILURE, GL_FAILURE };

    bool initialize(AAssetManager* assets, std::string* error);
    void destroy();
    bool render(GLuint original_texture, int width, int height,
                FramebufferTarget* destination);
    Failure failure() const { return failure_; }

private:
    bool draw_pass(int pass, GLuint primary, GLuint secondary, int width, int height,
                   const FramebufferTarget& destination);
    std::array<ShaderProgram, 5> programs_;
    FramebufferTarget pass0_;
    FramebufferTarget pass1_;
    FramebufferTarget pass2_;
    FramebufferTarget pass3_;
    Failure failure_ = Failure::NONE;
};

}  // namespace flynes::video
