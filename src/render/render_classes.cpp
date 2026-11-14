#include "render_classes.hpp"

#include <glad/glad.h>

#include <cstdint>
#include <iostream>


namespace geom::render {

namespace {

GLuint compile_stage(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader compilation failed: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // anonymous namespace


// --- ShaderProgram ---

ShaderProgram::ShaderProgram(const char* vertex_src, const char* fragment_src) {
    GLuint vs = compile_stage(GL_VERTEX_SHADER,   vertex_src);
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, fragment_src);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    // Shaders are only needed until link; the program keeps its own copy.
    glDetachShader(program_, vs);
    glDetachShader(program_, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        std::cerr << "Program link failed: " << log << std::endl;
        glDeleteProgram(program_);
        program_ = 0;
    }
}

ShaderProgram::~ShaderProgram() { reset(); }

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : program_(other.program_) {
    other.program_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        reset();
        program_ = other.program_;
        other.program_ = 0;
    }
    return *this;
}

void ShaderProgram::use() const { glUseProgram(program_); }

int ShaderProgram::uniform_location(const char* name) const {
    return glGetUniformLocation(program_, name);
}

void ShaderProgram::reset() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}


// --- Fbo ---

Fbo::Fbo(int width, int height) : width_(width), height_(height) {
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &color_tex_);
    glBindTexture(GL_TEXTURE_2D, color_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color_tex_, 0);

    glGenRenderbuffers(1, &depth_rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_rb_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "FBO incomplete" << std::endl;
        reset();
    }
}

Fbo::~Fbo() { reset(); }

Fbo::Fbo(Fbo&& other) noexcept
    : fbo_(other.fbo_), color_tex_(other.color_tex_), depth_rb_(other.depth_rb_),
      width_(other.width_), height_(other.height_) {
    other.fbo_ = other.color_tex_ = other.depth_rb_ = 0;
    other.width_ = other.height_ = 0;
}

Fbo& Fbo::operator=(Fbo&& other) noexcept {
    if (this != &other) {
        reset();
        fbo_ = other.fbo_;
        color_tex_ = other.color_tex_;
        depth_rb_ = other.depth_rb_;
        width_ = other.width_;
        height_ = other.height_;
        other.fbo_ = other.color_tex_ = other.depth_rb_ = 0;
        other.width_ = other.height_ = 0;
    }
    return *this;
}

void Fbo::bind() const { glBindFramebuffer(GL_FRAMEBUFFER, fbo_); }
void Fbo::unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

void Fbo::reset() {
    if (fbo_)       glDeleteFramebuffers(1, &fbo_);
    if (color_tex_) glDeleteTextures(1, &color_tex_);
    if (depth_rb_)  glDeleteRenderbuffers(1, &depth_rb_);
    fbo_ = color_tex_ = depth_rb_ = 0;
    width_ = height_ = 0;
}


// --- Mesh ---

Mesh::~Mesh() { reset(); }

Mesh::Mesh(Mesh&& other) noexcept
    : vao_(other.vao_), vbo_(other.vbo_), ibo_(other.ibo_),
      index_count_(other.index_count_) {
    other.vao_ = other.vbo_ = other.ibo_ = 0;
    other.index_count_ = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        reset();
        vao_ = other.vao_;
        vbo_ = other.vbo_;
        ibo_ = other.ibo_;
        index_count_ = other.index_count_;
        other.vao_ = other.vbo_ = other.ibo_ = 0;
        other.index_count_ = 0;
    }
    return *this;
}

void Mesh::upload(const float* vertices, std::size_t vertex_count,
                  std::size_t floats_per_vertex,
                  const unsigned int* indices, std::size_t index_count,
                  std::initializer_list<VertexAttribute> attributes) {
    reset();

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertex_count * floats_per_vertex * sizeof(float)),
                 vertices, GL_STATIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(floats_per_vertex * sizeof(float));
    for (const auto& a : attributes) {
        glVertexAttribPointer(
            a.location, a.components, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(a.offset_floats * sizeof(float))));
        glEnableVertexAttribArray(a.location);
    }

    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);   // captured by the VAO
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(index_count * sizeof(unsigned int)),
                 indices, GL_STATIC_DRAW);

    index_count_ = index_count;

    glBindVertexArray(0);
}

void Mesh::draw() const {
    if (!valid()) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count_),
                   GL_UNSIGNED_INT, nullptr);
}

void Mesh::reset() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    vao_ = vbo_ = ibo_ = 0;
    index_count_ = 0;
}

} // namespace geom::render
