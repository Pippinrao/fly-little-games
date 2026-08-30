#pragma once

#include <GLES2/gl2.h>

#include <string>

namespace flynes::video {

class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram() { destroy(); }
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    bool build(const char* vertex_source, const char* fragment_source, std::string* error);
    void destroy();
    GLuint id() const { return program_; }
    GLint uniform(const char* name) const { return glGetUniformLocation(program_, name); }
    GLint attribute(const char* name) const { return glGetAttribLocation(program_, name); }

private:
    static GLuint compile(GLenum type, const char* source, std::string* error);
    GLuint program_ = 0;
};

}  // namespace flynes::video
