#include "harmony_spatial_pipeline.hpp"

#include "shader_assets.hpp"

#include <array>
#include <cstring>

namespace flynes::harmony {
namespace {

constexpr char kMmpxVertex[] = R"(
attribute vec2 aPosition;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = (aPosition + vec2(1.0)) * 0.5;
}
)";
constexpr GLfloat kPositions[] = {-1.F, -1.F, 1.F, -1.F, -1.F, 1.F, 1.F, 1.F};
constexpr GLfloat kTexcoords[] = {0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 1.F, 1.F};
constexpr GLfloat kIdentity[] = {
    1.F, 0.F, 0.F, 0.F, 0.F, 1.F, 0.F, 0.F,
    0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F, 1.F,
};

GLuint compile(GLenum type, const char* source, std::string* error)
{
    const GLuint shader = glCreateShader(type);
    if (shader == 0)
    {
        if (error != nullptr) *error = "glCreateShader failed";
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return shader;
    std::array<char, 2048> log{};
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    if (error != nullptr) *error = log.data();
    glDeleteShader(shader);
    return 0;
}

std::string stage_source(const char* upstream, const char* stage)
{
    constexpr char version[] = "#version 130";
    const std::string source(upstream);
    if (source.rfind(version, 0) != 0) return {};
    const std::size_t newline = source.find('\n');
    if (newline == std::string::npos) return {};
    return std::string("#define ") + stage + "\n" + source.substr(newline + 1);
}

void uniform2f(GLuint program, const char* name, float x, float y)
{
    const GLint location = glGetUniformLocation(program, name);
    if (location >= 0) glUniform2f(location, x, y);
}

void bind_sampler(GLuint program, const char* name, GLuint texture, int unit)
{
    const GLint location = glGetUniformLocation(program, name);
    if (location < 0) return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(location, unit);
}

} // namespace

GlProgram::~GlProgram() { destroy(); }

bool GlProgram::build(const char* vertex_source, const char* fragment_source, std::string* error)
{
    destroy();
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source, error);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source, error);
    if (vertex == 0 || fragment == 0)
    {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        return false;
    }
    id_ = glCreateProgram();
    glAttachShader(id_, vertex);
    glAttachShader(id_, fragment);
    glLinkProgram(id_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = GL_FALSE;
    glGetProgramiv(id_, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) return true;
    std::array<char, 2048> log{};
    glGetProgramInfoLog(id_, static_cast<GLsizei>(log.size()), nullptr, log.data());
    if (error != nullptr) *error = log.data();
    destroy();
    return false;
}

void GlProgram::destroy()
{
    if (id_ != 0) glDeleteProgram(id_);
    id_ = 0;
}

GlTarget::~GlTarget() { destroy(); }

bool GlTarget::create(int width, int height, GLenum component_type)
{
    if (texture_ != 0 && framebuffer_ != 0 && width_ == width && height_ == height &&
        component_type_ == component_type)
    {
        return true;
    }
    destroy();
    if (width <= 0 || height <= 0) return false;
    while (glGetError() != GL_NO_ERROR) {}
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint internal = GL_RGBA;
    GLenum upload_type = component_type;
    if (component_type == GL_FLOAT) internal = GL_RGBA32F;
    if (component_type == GL_HALF_FLOAT)
    {
        internal = GL_RGBA16F;
        upload_type = GL_HALF_FLOAT;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, GL_RGBA, upload_type, nullptr);
    glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
    if (texture_ == 0 || framebuffer_ == 0 ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        destroy();
        return false;
    }
    width_ = width;
    height_ = height;
    component_type_ = component_type;
    return glGetError() == GL_NO_ERROR;
}

void GlTarget::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
}

void GlTarget::destroy()
{
    if (framebuffer_ != 0) glDeleteFramebuffers(1, &framebuffer_);
    if (texture_ != 0) glDeleteTextures(1, &texture_);
    framebuffer_ = 0;
    texture_ = 0;
    width_ = 0;
    height_ = 0;
    component_type_ = 0;
}

HarmonySpatialPipeline::~HarmonySpatialPipeline() { destroy(); }

bool HarmonySpatialPipeline::ensure_mmpx()
{
    if (mmpx_.id() != 0) return true;
    if (mmpx_.build(kMmpxVertex, shader_assets::kMmpx, &error_)) return true;
    failure_ = Failure::SHADER;
    return false;
}

bool HarmonySpatialPipeline::ensure_scalefx()
{
    if (scalefx_[0].id() != 0) return true;
    const char* sources[] = {shader_assets::kScaleFx0, shader_assets::kScaleFx1,
                             shader_assets::kScaleFx2, shader_assets::kScaleFx3,
                             shader_assets::kScaleFx4};
    for (std::size_t index = 0; index < scalefx_.size(); ++index)
    {
        const std::string vertex = stage_source(sources[index], "VERTEX");
        const std::string fragment = stage_source(sources[index], "FRAGMENT");
        if (vertex.empty() || fragment.empty() ||
            !scalefx_[index].build(vertex.c_str(), fragment.c_str(), &error_))
        {
            failure_ = Failure::SHADER;
            return false;
        }
    }
    return true;
}

bool HarmonySpatialPipeline::render(std::int32_t spatial_mode,
                                    GLuint source,
                                    int width,
                                    int height)
{
    failure_ = Failure::NONE;
    error_.clear();
    if (source == 0 || width <= 0 || height <= 0)
    {
        failure_ = Failure::GL;
        return false;
    }
    if (spatial_mode == 3) return render_mmpx(source, width, height);
    if (spatial_mode == 4) return render_scalefx(source, width, height);
    failure_ = Failure::GL;
    return false;
}

bool HarmonySpatialPipeline::render_mmpx(GLuint source, int width, int height)
{
    if (!ensure_mmpx()) return false;
    if (!output_.create_rgba8(width * 2, height * 2))
    {
        failure_ = Failure::FRAMEBUFFER;
        return false;
    }
    output_.bind();
    glUseProgram(mmpx_.id());
    bind_sampler(mmpx_.id(), "uTexture", source, 0);
    uniform2f(mmpx_.id(), "uTextureSize", static_cast<float>(width), static_cast<float>(height));
    const GLint position = glGetAttribLocation(mmpx_.id(), "aPosition");
    if (position < 0)
    {
        failure_ = Failure::SHADER;
        return false;
    }
    glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, kPositions);
    glEnableVertexAttribArray(position);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(position);
    if (glGetError() == GL_NO_ERROR) return true;
    failure_ = Failure::GL;
    return false;
}

bool HarmonySpatialPipeline::render_scalefx(GLuint source, int width, int height)
{
    if (!ensure_scalefx()) return false;
    GLenum float_type = GL_FLOAT;
    if (!pass0_.create(width, height, float_type) || !pass1_.create(width, height, float_type))
    {
        pass0_.destroy();
        pass1_.destroy();
        float_type = GL_HALF_FLOAT;
        if (!pass0_.create(width, height, float_type) || !pass1_.create(width, height, float_type))
        {
            failure_ = Failure::FRAMEBUFFER;
            return false;
        }
    }
    if (!pass2_.create_rgba8(width, height) || !pass3_.create_rgba8(width, height) ||
        !output_.create_rgba8(width * 3, height * 3))
    {
        failure_ = Failure::FRAMEBUFFER;
        return false;
    }
    const bool ok = draw_scale_pass(0, source, 0, width, height, pass0_) &&
                    draw_scale_pass(1, pass0_.texture(), 0, width, height, pass1_) &&
                    draw_scale_pass(2, pass1_.texture(), pass0_.texture(), width, height, pass2_) &&
                    draw_scale_pass(3, pass2_.texture(), 0, width, height, pass3_) &&
                    draw_scale_pass(4, pass3_.texture(), source, width, height, output_);
    if (ok) return true;
    failure_ = Failure::GL;
    return false;
}

bool HarmonySpatialPipeline::draw_scale_pass(int pass,
                                             GLuint primary,
                                             GLuint secondary,
                                             int width,
                                             int height,
                                             const GlTarget& destination)
{
    const GLuint program = scalefx_[static_cast<std::size_t>(pass)].id();
    destination.bind();
    glUseProgram(program);
    const GLint matrix = glGetUniformLocation(program, "MVPMatrix");
    if (matrix >= 0) glUniformMatrix4fv(matrix, 1, GL_FALSE, kIdentity);
    uniform2f(program, "TextureSize", static_cast<float>(width), static_cast<float>(height));
    uniform2f(program, "InputSize", static_cast<float>(width), static_cast<float>(height));
    uniform2f(program, "OutputSize", static_cast<float>(destination.width()),
              static_cast<float>(destination.height()));
    uniform2f(program, "PassPrev5TextureSize", static_cast<float>(width), static_cast<float>(height));
    uniform2f(program, "PassPrev5InputSize", static_cast<float>(width), static_cast<float>(height));
    bind_sampler(program, "Texture", primary, 0);
    if (pass == 2) bind_sampler(program, "PassPrev2Texture", secondary, 1);
    if (pass == 4) bind_sampler(program, "PassPrev5Texture", secondary, 1);
    const GLint vertex = glGetAttribLocation(program, "VertexCoord");
    const GLint texcoord = glGetAttribLocation(program, "TexCoord");
    const GLint color = glGetAttribLocation(program, "COLOR");
    if (vertex >= 0)
    {
        glVertexAttribPointer(vertex, 2, GL_FLOAT, GL_FALSE, 0, kPositions);
        glEnableVertexAttribArray(vertex);
    }
    if (texcoord >= 0)
    {
        glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 0, kTexcoords);
        glEnableVertexAttribArray(texcoord);
    }
    if (color >= 0) glVertexAttrib4f(color, 1.F, 1.F, 1.F, 1.F);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (vertex >= 0) glDisableVertexAttribArray(vertex);
    if (texcoord >= 0) glDisableVertexAttribArray(texcoord);
    return glGetError() == GL_NO_ERROR;
}

void HarmonySpatialPipeline::destroy()
{
    mmpx_.destroy();
    for (auto& program : scalefx_) program.destroy();
    pass0_.destroy();
    pass1_.destroy();
    pass2_.destroy();
    pass3_.destroy();
    output_.destroy();
    failure_ = Failure::NONE;
    error_.clear();
}

} // namespace flynes::harmony
