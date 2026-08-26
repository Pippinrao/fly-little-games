#include "spatial_pipeline.h"

#include <GLES2/gl2.h>

#include <limits>
#include <string>
#include <vector>

#include "framebuffer_target.h"
#include "mmpx_pass.h"
#include "scalefx_pipeline.h"
#include "shader_program.h"

namespace flynes::video {
namespace {
constexpr char kVertex[] = R"(
attribute vec2 aPosition;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = (aPosition + vec2(1.0)) * 0.5;
}
)";
constexpr char kNearest[] = R"(
precision highp float;
uniform sampler2D uTexture;
varying vec2 vTexCoord;
void main() { gl_FragColor = texture2D(uTexture, vTexCoord); }
)";
constexpr GLfloat kQuad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};

bool draw_nearest(GLuint texture, const FramebufferTarget& target) {
    ShaderProgram program;
    std::string error;
    if (!program.build(kVertex, kNearest, &error)) return false;
    target.bind();
    glUseProgram(program.id());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(program.uniform("uTexture"), 0);
    GLint position = program.attribute("aPosition");
    if (position < 0) return false;
    glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, kQuad);
    glEnableVertexAttribArray(position);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(position);
    return glGetError() == GL_NO_ERROR;
}
}  // namespace

SpatialPipeline::Output SpatialPipeline::render(AAssetManager* assets, Algorithm algorithm,
                                                const std::uint32_t* source,
                                                int width, int height) {
    Output output;
    if (!source || width <= 0 || height <= 0
            || width > 4096 || height > 4096
            || static_cast<long long>(width) * height > std::numeric_limits<int>::max()) {
        output.failure = Failure::INVALID_INPUT;
        return output;
    }
    int scale = algorithm == Algorithm::SCALEFX_3X ? 3 : 2;
    output.width = width * scale;
    output.height = height * scale;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    for (int index = 0; index < width * height; index++) {
        std::uint32_t pixel = source[index];
        rgba[index * 4] = static_cast<std::uint8_t>(pixel);
        rgba[index * 4 + 1] = static_cast<std::uint8_t>(pixel >> 8);
        rgba[index * 4 + 2] = static_cast<std::uint8_t>(pixel >> 16);
        rgba[index * 4 + 3] = static_cast<std::uint8_t>(pixel >> 24);
    }

    GLuint source_texture = 0;
    glGenTextures(1, &source_texture);
    glBindTexture(GL_TEXTURE_2D, source_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    if (!source_texture || glGetError() != GL_NO_ERROR) {
        if (source_texture) glDeleteTextures(1, &source_texture);
        output.failure = Failure::GL_OPERATION_FAILED;
        return output;
    }

    FramebufferTarget target;
    bool rendered = false;
    if (algorithm == Algorithm::SCALEFX_3X) {
        ScaleFxPipeline pipeline;
        std::string error;
        if (!pipeline.initialize(assets, &error)) {
            output.failure = pipeline.failure() == ScaleFxPipeline::Failure::ASSET_MISSING
                    ? Failure::SHADER_ASSET_MISSING : Failure::SHADER_COMPILE_OR_LINK_FAILED;
            glDeleteTextures(1, &source_texture);
            return output;
        }
        rendered = pipeline.render(source_texture, width, height, &target);
        if (!rendered && pipeline.failure() == ScaleFxPipeline::Failure::FRAMEBUFFER_FAILURE) {
            output.failure = Failure::FRAMEBUFFER_UNSUPPORTED;
        }
    } else if (!target.create_rgba8(output.width, output.height)) {
        output.failure = Failure::FRAMEBUFFER_UNSUPPORTED;
    } else if (algorithm == Algorithm::NEAREST_2X) {
        rendered = draw_nearest(source_texture, target);
    } else {
        MmpxPass pass;
        std::string error;
        if (!pass.initialize(assets, &error)) {
            output.failure = pass.init_failure() == MmpxPass::InitFailure::ASSET_MISSING
                    ? Failure::SHADER_ASSET_MISSING : Failure::SHADER_COMPILE_OR_LINK_FAILED;
            glDeleteTextures(1, &source_texture);
            return output;
        }
        rendered = pass.render(source_texture, width, height, target);
    }
    glDeleteTextures(1, &source_texture);
    if (!rendered) {
        if (output.failure != Failure::NONE) return output;
        output.failure = Failure::GL_OPERATION_FAILED;
        return output;
    }

    std::vector<std::uint8_t> readback(static_cast<std::size_t>(output.width)
                                       * output.height * 4);
    target.bind();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, output.width, output.height, GL_RGBA,
                 GL_UNSIGNED_BYTE, readback.data());
    if (glGetError() != GL_NO_ERROR) {
        output.failure = Failure::GL_OPERATION_FAILED;
        return output;
    }
    output.pixels.resize(static_cast<std::size_t>(output.width) * output.height);
    for (std::size_t index = 0; index < output.pixels.size(); index++) {
        output.pixels[index] = static_cast<std::uint32_t>(readback[index * 4])
                | (static_cast<std::uint32_t>(readback[index * 4 + 1]) << 8)
                | (static_cast<std::uint32_t>(readback[index * 4 + 2]) << 16)
                | (static_cast<std::uint32_t>(readback[index * 4 + 3]) << 24);
    }
    return output;
}

}  // namespace flynes::video
