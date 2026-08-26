#pragma once

#include <GLES2/gl2.h>
#include <android/asset_manager.h>

#include <string>

#include "framebuffer_target.h"
#include "shader_program.h"

namespace flynes::video {

class MmpxPass {
public:
    enum class InitFailure { NONE, ASSET_MISSING, SHADER_FAILURE };

    bool initialize(AAssetManager* assets, std::string* error);
    void destroy() { program_.destroy(); init_failure_ = InitFailure::NONE; }
    bool render(GLuint source_texture, int source_width, int source_height,
                const FramebufferTarget& destination);
    InitFailure init_failure() const { return init_failure_; }

private:
    ShaderProgram program_;
    InitFailure init_failure_ = InitFailure::NONE;
};

}  // namespace flynes::video
