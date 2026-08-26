#include "mmpx_pass.h"

#include <array>
#include <string>

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

constexpr GLfloat kQuad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};

std::string read_asset(AAssetManager* assets, const char* path) {
    if (!assets) return {};
    AAsset* asset = AAssetManager_open(assets, path, AASSET_MODE_BUFFER);
    if (!asset) return {};
    const auto length = static_cast<std::size_t>(AAsset_getLength(asset));
    const auto* data = static_cast<const char*>(AAsset_getBuffer(asset));
    std::string result;
    if (data && length > 0) result.assign(data, length);
    AAsset_close(asset);
    return result;
}
}  // namespace

bool MmpxPass::initialize(AAssetManager* assets, std::string* error) {
    std::string fragment = read_asset(assets, "shaders/mmpx/mmpx_2x.frag");
    if (fragment.empty()) {
        init_failure_ = InitFailure::ASSET_MISSING;
        if (error) *error = "MMPX shader asset is missing";
        return false;
    }
    if (!program_.build(kVertex, fragment.c_str(), error)) {
        init_failure_ = InitFailure::SHADER_FAILURE;
        return false;
    }
    init_failure_ = InitFailure::NONE;
    return true;
}

bool MmpxPass::render(GLuint source_texture, int source_width, int source_height,
                      const FramebufferTarget& destination) {
    if (!program_.id() || !source_texture || source_width <= 0 || source_height <= 0) {
        return false;
    }
    destination.bind();
    glUseProgram(program_.id());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_texture);
    glUniform1i(program_.uniform("uTexture"), 0);
    glUniform2f(program_.uniform("uTextureSize"),
                static_cast<float>(source_width), static_cast<float>(source_height));
    GLint position = program_.attribute("aPosition");
    if (position < 0) return false;
    glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, kQuad);
    glEnableVertexAttribArray(position);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(position);
    return glGetError() == GL_NO_ERROR;
}

}  // namespace flynes::video
