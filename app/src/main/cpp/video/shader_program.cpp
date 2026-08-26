#include "shader_program.h"

#include <array>

namespace flynes::video {

GLuint ShaderProgram::compile(GLenum type, const char* source, std::string* error) {
    GLuint shader = glCreateShader(type);
    if (!shader) {
        if (error) *error = "glCreateShader failed";
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::array<char, 2048> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        if (error) *error = log.data();
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool ShaderProgram::build(const char* vertex_source, const char* fragment_source,
                          std::string* error) {
    destroy();
    GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source, error);
    if (!vertex) return false;
    GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source, error);
    if (!fragment) {
        glDeleteShader(vertex);
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::array<char, 2048> log{};
        glGetProgramInfoLog(program_, static_cast<GLsizei>(log.size()), nullptr, log.data());
        if (error) *error = log.data();
        destroy();
        return false;
    }
    return true;
}

void ShaderProgram::destroy() {
    if (program_) glDeleteProgram(program_);
    program_ = 0;
}

}  // namespace flynes::video
