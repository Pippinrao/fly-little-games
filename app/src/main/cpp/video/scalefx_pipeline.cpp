#include "scalefx_pipeline.h"

#include <array>
#include <string>

namespace flynes::video {
namespace {
constexpr GLenum kHalfFloatOes = 0x8D61;
constexpr GLfloat kPositions[] = {-1.f,-1.f, 1.f,-1.f, -1.f,1.f, 1.f,1.f};
constexpr GLfloat kTexcoords[] = {0.f,0.f, 1.f,0.f, 0.f,1.f, 1.f,1.f};
constexpr GLfloat kIdentity[] = {
        1.f,0.f,0.f,0.f, 0.f,1.f,0.f,0.f,
        0.f,0.f,1.f,0.f, 0.f,0.f,0.f,1.f};

std::string read_asset(AAssetManager* assets, const std::string& path) {
    if (!assets) return {};
    AAsset* asset = AAssetManager_open(assets, path.c_str(), AASSET_MODE_BUFFER);
    if (!asset) return {};
    const auto length = static_cast<std::size_t>(AAsset_getLength(asset));
    const auto* bytes = static_cast<const char*>(AAsset_getBuffer(asset));
    std::string result;
    if (bytes && length > 0) result.assign(bytes, length);
    AAsset_close(asset);
    return result;
}

std::string stage_source(const std::string& upstream, const char* stage) {
    constexpr char version[] = "#version 130";
    if (upstream.rfind(version, 0) != 0) return {};
    std::size_t newline = upstream.find('\n');
    if (newline == std::string::npos) return {};
    return std::string("#define ") + stage + "\n" + upstream.substr(newline + 1);
}

void uniform2f(GLuint program, const char* name, float x, float y) {
    GLint location = glGetUniformLocation(program, name);
    if (location >= 0) glUniform2f(location, x, y);
}

void bind_sampler(GLuint program, const char* name, GLuint texture, int unit) {
    GLint location = glGetUniformLocation(program, name);
    if (location < 0) return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(location, unit);
}
}  // namespace

bool ScaleFxPipeline::initialize(AAssetManager* assets, std::string* error) {
    for (int pass = 0; pass < 5; pass++) {
        std::string source = read_asset(assets, "shaders/scalefx/scalefx-pass"
                + std::to_string(pass) + ".glsl");
        if (source.empty()) {
            failure_ = Failure::ASSET_MISSING;
            if (error) *error = "ScaleFX shader asset is missing";
            return false;
        }
        std::string vertex = stage_source(source, "VERTEX");
        std::string fragment = stage_source(source, "FRAGMENT");
        if (vertex.empty() || fragment.empty() || !programs_[pass].build(
                vertex.c_str(), fragment.c_str(), error)) {
            failure_ = Failure::SHADER_FAILURE;
            return false;
        }
    }
    failure_ = Failure::NONE;
    return true;
}

void ScaleFxPipeline::destroy() {
    for (auto& program : programs_) program.destroy();
    pass0_.destroy();
    pass1_.destroy();
    pass2_.destroy();
    pass3_.destroy();
    failure_ = Failure::NONE;
}

bool ScaleFxPipeline::render(GLuint original_texture, int width, int height,
                             FramebufferTarget* destination) {
    if (!original_texture || width <= 0 || height <= 0 || !destination) {
        failure_ = Failure::GL_FAILURE;
        return false;
    }
    GLenum float_type = GL_FLOAT;
    if (!pass0_.create_rgba(width, height, float_type)
            || !pass1_.create_rgba(width, height, float_type)) {
        pass0_.destroy();
        pass1_.destroy();
        float_type = kHalfFloatOes;
        if (!pass0_.create_rgba(width, height, float_type)
                || !pass1_.create_rgba(width, height, float_type)) {
            failure_ = Failure::FRAMEBUFFER_FAILURE;
            return false;
        }
    }
    if (!pass2_.create_rgba8(width, height) || !pass3_.create_rgba8(width, height)
            || !destination->create_rgba8(width * 3, height * 3)) {
        failure_ = Failure::FRAMEBUFFER_FAILURE;
        return false;
    }

    bool ok = draw_pass(0, original_texture, 0, width, height, pass0_)
            && draw_pass(1, pass0_.texture(), 0, width, height, pass1_)
            && draw_pass(2, pass1_.texture(), pass0_.texture(), width, height, pass2_)
            && draw_pass(3, pass2_.texture(), 0, width, height, pass3_)
            && draw_pass(4, pass3_.texture(), original_texture, width, height, *destination);
    if (!ok) failure_ = Failure::GL_FAILURE;
    return ok;
}

bool ScaleFxPipeline::draw_pass(int pass, GLuint primary, GLuint secondary,
                                int width, int height,
                                const FramebufferTarget& destination) {
    GLuint program = programs_[pass].id();
    destination.bind();
    glUseProgram(program);
    GLint matrix = glGetUniformLocation(program, "MVPMatrix");
    if (matrix >= 0) glUniformMatrix4fv(matrix, 1, GL_FALSE, kIdentity);
    uniform2f(program, "TextureSize", static_cast<float>(width), static_cast<float>(height));
    uniform2f(program, "InputSize", static_cast<float>(width), static_cast<float>(height));
    uniform2f(program, "OutputSize", static_cast<float>(destination.width()),
              static_cast<float>(destination.height()));
    uniform2f(program, "PassPrev5TextureSize", static_cast<float>(width),
              static_cast<float>(height));
    uniform2f(program, "PassPrev5InputSize", static_cast<float>(width),
              static_cast<float>(height));
    bind_sampler(program, "Texture", primary, 0);
    if (pass == 2) bind_sampler(program, "PassPrev2Texture", secondary, 1);
    if (pass == 4) bind_sampler(program, "PassPrev5Texture", secondary, 1);

    GLint vertex = glGetAttribLocation(program, "VertexCoord");
    GLint texcoord = glGetAttribLocation(program, "TexCoord");
    GLint color = glGetAttribLocation(program, "COLOR");
    if (vertex >= 0) {
        glVertexAttribPointer(vertex, 2, GL_FLOAT, GL_FALSE, 0, kPositions);
        glEnableVertexAttribArray(vertex);
    }
    if (texcoord >= 0) {
        glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 0, kTexcoords);
        glEnableVertexAttribArray(texcoord);
    }
    if (color >= 0) glVertexAttrib4f(color, 1.f, 1.f, 1.f, 1.f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (vertex >= 0) glDisableVertexAttribArray(vertex);
    if (texcoord >= 0) glDisableVertexAttribArray(texcoord);
    return glGetError() == GL_NO_ERROR;
}

}  // namespace flynes::video
