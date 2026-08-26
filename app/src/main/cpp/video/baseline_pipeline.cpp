#include "baseline_pipeline.h"

#include <android/log.h>

#include <string>

namespace flynes::video {
namespace {
constexpr char kVertex[] = R"(
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main(){ gl_Position=vec4(aPosition,0.0,1.0); vTexCoord=aTexCoord; }
)";
constexpr char kNearest[] = R"(
precision mediump float; uniform sampler2D uTexture; varying vec2 vTexCoord;
void main(){ gl_FragColor=texture2D(uTexture,vTexCoord); }
)";
constexpr char kSharp[] = R"(
precision mediump float; uniform sampler2D uTexture; varying vec2 vTexCoord;
uniform vec2 uTextureSize;
void main(){ vec2 pixel=vTexCoord*uTextureSize-vec2(0.5); vec2 base=floor(pixel);
 vec2 sharpFraction=clamp((fract(pixel)-vec2(0.5))*2.0+vec2(0.5),0.0,1.0);
 vec2 uv=(base+sharpFraction+vec2(0.5))/uTextureSize;
 gl_FragColor=texture2D(uTexture,uv); }
)";
constexpr char kEdge[] = R"(
precision mediump float; uniform sampler2D uTexture; varying vec2 vTexCoord;
uniform vec2 uTextureSize;
float colorDistance(vec4 a,vec4 b){ return dot(abs(a.rgb-b.rgb),vec3(0.299,0.587,0.114)); }
void main(){ vec2 pixel=vTexCoord*uTextureSize-vec2(0.5);
 vec2 centre=(floor(pixel)+vec2(0.5))/uTextureSize; vec2 f=fract(pixel);
 vec2 t=vec2(1.0)/uTextureSize; vec4 e=texture2D(uTexture,centre);
 vec4 b=texture2D(uTexture,centre-vec2(0.0,t.y));
 vec4 d=texture2D(uTexture,centre-vec2(t.x,0.0));
 vec4 r=texture2D(uTexture,centre+vec2(t.x,0.0));
 vec4 h=texture2D(uTexture,centre+vec2(0.0,t.y));
 vec4 outColor=e; float same=0.075;
 if(colorDistance(d,r)>same && colorDistance(b,h)>same){
  if(f.x<0.5 && f.y<0.5 && colorDistance(d,b)<same) outColor=mix(e,d,0.72);
  else if(f.x>=0.5 && f.y<0.5 && colorDistance(b,r)<same) outColor=mix(e,r,0.72);
  else if(f.x<0.5 && f.y>=0.5 && colorDistance(d,h)<same) outColor=mix(e,d,0.72);
  else if(f.x>=0.5 && f.y>=0.5 && colorDistance(h,r)<same) outColor=mix(e,r,0.72); }
 gl_FragColor=outColor; }
)";
constexpr char kCrt[] = R"(
precision mediump float; uniform sampler2D uTexture; varying vec2 vTexCoord;
uniform vec2 uTextureSize; uniform vec2 uOutputSize;
void main(){ vec4 color=texture2D(uTexture,vTexCoord);
 float scanline=0.88+0.12*sin(vTexCoord.y*uOutputSize.y*3.14159265);
 float mask=0.96+0.04*sin(vTexCoord.x*uOutputSize.x*2.0943951);
 vec2 edge=vTexCoord*(vec2(1.0)-vTexCoord);
 float vignette=clamp(pow(16.0*edge.x*edge.y,0.12),0.78,1.0);
 gl_FragColor=vec4(color.rgb*scanline*mask*vignette,color.a); }
)";
constexpr GLfloat kQuad[] = {
        -1.f,-1.f,0.f,1.f, 1.f,-1.f,1.f,1.f,
        -1.f,1.f,0.f,0.f, 1.f,1.f,1.f,0.f};

const char* fragment(FilterMode mode) {
    switch (mode) {
        case FilterMode::SHARP_BILINEAR: return kSharp;
        case FilterMode::NEAREST: return kNearest;
        case FilterMode::CRT: return kCrt;
        default: return kEdge;
    }
}
}  // namespace

bool BaselinePipeline::initialize(AAssetManager* assets) {
    assets_ = assets;
    glGenTextures(1, &texture_);
    if (!texture_) return false;
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    if (ensure_program()) return true;
    last_failure_ = Failure::SHADER;
    return false;
}

void BaselinePipeline::destroy() {
    if (program_) glDeleteProgram(program_);
    if (texture_) glDeleteTextures(1, &texture_);
    program_ = texture_ = 0;
    texture_width_ = texture_height_ = 0;
    active_filter_ = -1;
    reconstruction_.destroy();
    composite_program_.destroy();
    mmpx_.destroy();
    scalefx_.destroy();
    mmpx_initialized_ = false;
    scalefx_initialized_ = false;
}

void BaselinePipeline::resize(int width, int height) {
    output_width_ = width;
    output_height_ = height;
    glViewport(0, 0, width, height);
}

bool BaselinePipeline::render(const StagedFrame& frame) {
    last_failure_ = Failure::NONE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, output_width_, output_height_);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!upload(frame)) { last_failure_ = Failure::GL; return false; }
    if (requested_filter_ == FilterMode::MMPX || requested_filter_ == FilterMode::SCALEFX) {
        return render_advanced(requested_filter_);
    }
    if (!ensure_program()) { last_failure_ = Failure::SHADER; return false; }
    glUseProgram(program_);
    uniform2f("uTextureSize", static_cast<float>(texture_width_),
              static_cast<float>(texture_height_));
    uniform2f("uOutputSize", static_cast<float>(output_width_),
              static_cast<float>(output_height_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    GLenum filter_mode = requested_filter_ == FilterMode::SHARP_BILINEAR ||
                         requested_filter_ == FilterMode::CRT ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_mode);
    GLint position = glGetAttribLocation(program_, "aPosition");
    GLint texcoord = glGetAttribLocation(program_, "aTexCoord");
    glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), kQuad);
    glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), kQuad + 2);
    glEnableVertexAttribArray(position);
    glEnableVertexAttribArray(texcoord);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(position);
    glDisableVertexAttribArray(texcoord);
    bool ok = glGetError() == GL_NO_ERROR;
    if (!ok) last_failure_ = Failure::GL;
    return ok;
}

bool BaselinePipeline::render_advanced(FilterMode mode) {
    bool reconstructed = false;
    if (mode == FilterMode::MMPX) {
        if (!mmpx_initialized_) {
            std::string error;
            mmpx_initialized_ = mmpx_.initialize(assets_, &error);
            if (!mmpx_initialized_) { last_failure_ = Failure::SHADER; return false; }
        }
        if (!reconstruction_.create_rgba8(texture_width_ * 2, texture_height_ * 2)) {
            last_failure_ = Failure::FRAMEBUFFER;
            return false;
        }
        reconstructed = mmpx_.render(texture_, texture_width_, texture_height_, reconstruction_);
    } else {
        if (!scalefx_initialized_) {
            std::string error;
            scalefx_initialized_ = scalefx_.initialize(assets_, &error);
            if (!scalefx_initialized_) { last_failure_ = Failure::SHADER; return false; }
        }
        reconstructed = scalefx_.render(texture_, texture_width_, texture_height_,
                                        &reconstruction_);
    }
    if (!reconstructed) {
        last_failure_ = scalefx_.failure() == ScaleFxPipeline::Failure::FRAMEBUFFER_FAILURE
                ? Failure::FRAMEBUFFER : Failure::GL;
        return false;
    }
    if (!composite(reconstruction_.texture(), reconstruction_.width(),
                   reconstruction_.height())) {
        last_failure_ = Failure::SHADER;
        return false;
    }
    return true;
}

bool BaselinePipeline::composite(GLuint texture, int width, int height) {
    if (!composite_program_.id()) {
        std::string error;
        if (!composite_program_.build(kVertex, kSharp, &error)) return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, output_width_, output_height_);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(composite_program_.id());
    GLint texture_size = composite_program_.uniform("uTextureSize");
    if (texture_size >= 0) {
        glUniform2f(texture_size, static_cast<float>(width), static_cast<float>(height));
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLint sampler = composite_program_.uniform("uTexture");
    if (sampler >= 0) glUniform1i(sampler, 0);
    GLint position = composite_program_.attribute("aPosition");
    GLint texcoord = composite_program_.attribute("aTexCoord");
    if (position < 0 || texcoord < 0) return false;
    glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), kQuad);
    glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), kQuad + 2);
    glEnableVertexAttribArray(position);
    glEnableVertexAttribArray(texcoord);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(position);
    glDisableVertexAttribArray(texcoord);
    return glGetError() == GL_NO_ERROR;
}

bool BaselinePipeline::upload(const StagedFrame& frame) {
    GLenum format = frame.format == PixelFormat::RGBA8888 ? GL_RGBA : GL_RGB;
    GLenum type = frame.format == PixelFormat::RGB565 ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (texture_width_ != frame.width || texture_height_ != frame.height ||
        texture_format_ != frame.format) {
        glTexImage2D(GL_TEXTURE_2D, 0, format, frame.width, frame.height, 0,
                     format, type, frame.pixels.data());
        texture_width_ = frame.width;
        texture_height_ = frame.height;
        texture_format_ = frame.format;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height,
                        format, type, frame.pixels.data());
    }
    return glGetError() == GL_NO_ERROR;
}

bool BaselinePipeline::ensure_program() {
    int wanted = static_cast<int>(requested_filter_);
    if (program_ && active_filter_ == wanted) return true;
    GLuint replacement = link(kVertex, fragment(requested_filter_));
    if (!replacement) return false;
    if (program_) glDeleteProgram(program_);
    program_ = replacement;
    active_filter_ = wanted;
    return true;
}

GLuint BaselinePipeline::compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "FlyNESVideo", "shader compile: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint BaselinePipeline::link(const char* vertex, const char* fragment_source) {
    GLuint vs = compile(GL_VERTEX_SHADER, vertex);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_source);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint result = glCreateProgram();
    glAttachShader(result, vs);
    glAttachShader(result, fs);
    glLinkProgram(result);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(result, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetProgramInfoLog(result, sizeof(log), nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "FlyNESVideo", "program link: %s", log);
        glDeleteProgram(result);
        return 0;
    }
    return result;
}

void BaselinePipeline::uniform2f(const char* name, float first, float second) {
    GLint location = glGetUniformLocation(program_, name);
    if (location >= 0) glUniform2f(location, first, second);
}

}  // namespace flynes::video
