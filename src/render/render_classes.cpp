#include "render_classes.hpp"
#include "image/image_loader.hpp"

#include <glad/glad.h>

#include <algorithm>
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

void set_mat4(GLint loc, const Matrix<double>& m) {
    const auto f = m.to_floats();
    glUniformMatrix4fv(loc, 1, GL_TRUE, f.data());
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

Fbo::Fbo(int width, int height, FboKind kind)
    : width_(width), height_(height) {
    glGenFramebuffers(1, &fbo_);
    if (kind == FboKind::Empty)
        return;

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

void Fbo::attach_cubemap_face(unsigned int cube_tex, int face) {
    if (face < 0 || face > 5)
        return;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cube_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "cubemap face " << face << " attach incomplete" << std::endl;
}

unsigned int Fbo::current() {
    GLint id = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &id);
    return static_cast<unsigned int>(id);
}
void Fbo::bind_id(unsigned int id) {
    glBindFramebuffer(GL_FRAMEBUFFER, id);
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
    if (!valid())
        return;

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


// --- SpriteRenderer ---

const char* kSpriteVertexSrc = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

out vec2 vUV;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)";

const char* kSpriteFragmentSrc = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTexture;
uniform vec4 uUVRange;   // u0, v0, u1, v1

out vec4 FragColor;

void main() {
    vec2 uv = mix(uUVRange.xy, uUVRange.zw, vUV);
    FragColor = texture(uTexture, uv);
}
)";

SpriteRenderer::~SpriteRenderer() { reset(); }

void SpriteRenderer::init() {
    if (shader_registered_)
        return;

    if (shared_.refcount++ == 0) {
        shared_.program = ShaderProgram(kSpriteVertexSrc, kSpriteFragmentSrc);
        shared_.tex_loc      = shared_.program.uniform_location("uTexture");
        shared_.uv_range_loc = shared_.program.uniform_location("uUVRange");
    }
    shader_registered_ = true;

    float vertices[] = {
        -1, -1,    0, 0,
         1, -1,    1, 0,
         1,  1,    1, 1,
        -1,  1,    0, 1,
    };
    unsigned int indices[] = { 0, 1, 2,  0, 2, 3 };
    mesh_.upload(vertices, 4, 4, indices, 6, {
        {0, 2, 0},
        {1, 2, 2},
    });
}

void SpriteRenderer::reset() {
    mesh_.reset();

    if (shader_registered_) {
        if (--shared_.refcount == 0)
            shared_.program.reset();
        shader_registered_ = false;
    }
}

bool SpriteRenderer::valid() const {
    return shader_registered_ && shared_.program.valid() && mesh_.valid();
}

void SpriteRenderer::draw_tile(unsigned int tex_id,
                               float u0, float v0, float u1, float v1) {
    if (!valid())
        return;

    const GLboolean was_depth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean was_cull  = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    {
        shared_.program.use();
        glUniform1i(shared_.tex_loc, 0);
        glUniform4f(shared_.uv_range_loc, u0, v0, u1, v1);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_id);
        mesh_.draw();
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (was_depth) glEnable(GL_DEPTH_TEST);
    if (was_cull)  glEnable(GL_CULL_FACE);
}


// --- Markers ---

namespace {

const char* kMarkerVertexSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4 uProjView;
uniform mat4 uModel;

out vec3 vColor;

void main() {
    gl_Position = uProjView * uModel * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

const char* kMarkerFragmentSrc = R"(
#version 330 core
in vec3 vColor;
uniform vec3 uTint;

out vec4 FragColor;

void main() {
    // Bias the tint slightly toward the per-vertex color so faces read
    // as shaded, while the marker keeps its identity color.
    vec3 c = mix(uTint, vColor, 0.3);
    FragColor = vec4(c, 1.0);
}
)";

} // anonymous namespace

Markers::~Markers() { reset(); }

void Markers::init() {
    if (shader_registered_) return;

    if (shared_.refcount++ == 0) {
        shared_.program = ShaderProgram(kMarkerVertexSrc, kMarkerFragmentSrc);
        shared_.proj_view_loc = shared_.program.uniform_location("uProjView");
        shared_.model_loc     = shared_.program.uniform_location("uModel");
        shared_.tint_loc      = shared_.program.uniform_location("uTint");
    }
    shader_registered_ = true;

    float vertices[] = {
        0,  1,  0,     1, 0, 0,
        1,  0,  0,     0, 1, 0,
        0,  0,  1,     0, 0, 1,
       -1,  0,  0,     1, 1, 0,
        0,  0, -1,     1, 0, 1,
        0, -1,  0,     0, 1, 1,
    };
    unsigned int indices[] = {
        0, 2, 1,  0, 3, 2,  0, 4, 3,  0, 1, 4,
        5, 1, 2,  5, 2, 3,  5, 3, 4,  5, 4, 1,
    };
    mesh_.upload(vertices, 6, 6, indices, 24, {
        {0, 3, 0},
        {1, 3, 3},
    });
}

void Markers::reset() {
    mesh_.reset();

    if (shader_registered_) {
        if (--shared_.refcount == 0) {
            shared_.program.reset();
        }
        shader_registered_ = false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    markers_.clear();
}

bool Markers::valid() const {
    return shader_registered_ && shared_.program.valid() && mesh_.valid();
}

void Markers::draw(const Matrix<double>& proj_view,
                   const Matrix<double>& model) const {
    if (!valid()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (markers_.empty()) return;

    shared_.program.use();
    set_mat4(shared_.proj_view_loc, proj_view);

    for (const auto& m : markers_) {
        const Matrix<double> local =
            Matrix<double>::translation(m.position.x, m.position.y, m.position.z)
            * Matrix<double>::scale(m.scale);

        set_mat4(shared_.model_loc, model * local);
        glUniform3f(shared_.tint_loc,
                    static_cast<float>(m.tint.x),
                    static_cast<float>(m.tint.y),
                    static_cast<float>(m.tint.z));

        mesh_.draw();
    }
}

void Markers::set(const std::vector<Marker>& markers) {
    std::lock_guard<std::mutex> lock(mutex_);
    markers_ = markers;
}

void Markers::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    markers_.clear();
}


// --- Texture ---

Texture::~Texture() { reset(); }

Texture::Texture(Texture&& other) noexcept
    : id_(other.id_), width_(other.width_), height_(other.height_) {
    other.id_ = 0; other.width_ = other.height_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        reset();
        id_ = other.id_; width_ = other.width_; height_ = other.height_;
        other.id_ = 0; other.width_ = other.height_ = 0;
    }
    return *this;
}

void Texture::create(int width, int height, int filter,
                     const std::uint8_t* rgba) {
    reset();
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    width_ = width;
    height_ = height;
}

bool Texture::load_from_file(const std::string& path, int filter) {
    const ImageData img = load_image(path);
    if (img.width == 0)
        return false;
    create(img.width, img.height, filter, img.rgba.data());
    return true;
}

void Texture::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, id_);
}

void Texture::unbind() { glBindTexture(GL_TEXTURE_2D, 0); }

void Texture::reset() {
    if (id_)
        glDeleteTextures(1, &id_);
    id_ = 0; width_ = height_ = 0;
}


// --- CubemapTexture ---

CubemapTexture::~CubemapTexture() { reset(); }

CubemapTexture::CubemapTexture(CubemapTexture&& other) noexcept
    : id_(other.id_), size_(other.size_) {
    other.id_ = 0;
    other.size_ = 0;
}

CubemapTexture& CubemapTexture::operator=(CubemapTexture&& other) noexcept {
    if (this != &other) {
        reset();
        id_ = other.id_;
        size_ = other.size_;
        other.id_ = 0;
        other.size_ = 0;
    }
    return *this;
}

void CubemapTexture::create(int face_size, int filter) {
    reset();
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
    for (int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
                     GL_RGBA8, face_size, face_size, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    size_ = face_size;
}

bool CubemapTexture::load_from_atlas(const std::string& path,
                                     int tile_cols, int tile_rows,
                                     const int tile_indexes[6]) {
    Texture atlas;
    if (!atlas.load_from_file(path, GL_LINEAR)) {
        std::cerr << "CubemapTexture: failed to load " << path << std::endl;
        return false;
    }
    if (tile_cols <= 0 || tile_rows <= 0) {
        std::cerr << "tile_cols <= 0 || tile_rows <= 0" << std::endl;
        return false;
    }

    const int tile_w = atlas.width()  / tile_cols;
    const int tile_h = atlas.height() / tile_rows;
    if (tile_w <= 0 || tile_h <= 0) {
        std::cerr << "tile_w <= 0 || tile_h <= 0" << std::endl;
        return false;
    }

    const int face_size = std::min(tile_w, tile_h);
    create(face_size, GL_LINEAR);

    Fbo fbo(face_size, face_size, FboKind::Empty);
    SpriteRenderer blitter;
    blitter.init();

    GLint saved_vp[4];
    glGetIntegerv(GL_VIEWPORT, saved_vp);

    for (int face = 0; face < 6; ++face) {
        const int tile = tile_indexes[face];
        const int tx = tile % tile_cols;
        const int ty = tile / tile_cols;

        const float u0 = float(tx * tile_w) / atlas.width();
        const float v0 = float(ty * tile_h) / atlas.height();
        const float u1 = float((tx + 1) * tile_w) / atlas.width();
        const float v1 = float((ty + 1) * tile_h) / atlas.height();

        // GL-spec flips per face, XORed with the global user flips
        const bool is_top_bottom = (face == 2 || face == 3);
        const bool flip_u = !is_top_bottom;
        const bool flip_v = is_top_bottom;

        const float ua = flip_u ? u1 : u0;
        const float ub = flip_u ? u0 : u1;
        const float va = flip_v ? v1 : v0;
        const float vb = flip_v ? v0 : v1;

        fbo.attach_cubemap_face(id_, face);
        fbo.bind();
        glViewport(0, 0, face_size, face_size);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        blitter.draw_tile(atlas.id(), ua, va, ub, vb);
    }

    Fbo::unbind();
    blitter.reset();
    glViewport(saved_vp[0], saved_vp[1], saved_vp[2], saved_vp[3]);
    return true;
}

void CubemapTexture::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
}

void CubemapTexture::unbind() { glBindTexture(GL_TEXTURE_CUBE_MAP, 0); }

void CubemapTexture::reset() {
    if (id_) glDeleteTextures(1, &id_);
    id_ = 0;
    size_ = 0;
}


// --- SkyBox ---

const char* kSkyBoxVertexSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uProjViewRot;   // projection * view (rotation only)
out vec3 vDir;

void main() {
    vDir = aPos;
    vec4 pos = uProjViewRot * vec4(aPos, 1.0);
    gl_Position = pos.xyww;   // force depth = w, so z/w = 1.0 (far plane)
}
)";

const char* kSkyBoxFragmentSrc = R"(
#version 330 core
in vec3 vDir;
uniform samplerCube uSkybox;
out vec4 FragColor;

void main() {
    FragColor = texture(uSkybox, vDir);
}
)";

SkyBox::~SkyBox() { reset(); }

void SkyBox::init() {
    if (shader_registered_)
        return;

    if (shared_.refcount++ == 0) {
        shared_.program = ShaderProgram(kSkyBoxVertexSrc, kSkyBoxFragmentSrc);
        shared_.proj_view_loc = shared_.program.uniform_location("uProjViewRot");
        shared_.cube_loc      = shared_.program.uniform_location("uSkybox");
    }
    shader_registered_ = true;

    float vertices[] = {
        -1, -1, -1,
         1, -1, -1,
         1, -1,  1,
        -1, -1,  1,
        -1,  1, -1,
         1,  1, -1,
         1,  1,  1,
        -1,  1,  1,
    };
    unsigned int indices[] = {
        0, 2, 1,  0, 3, 2,
        0, 1, 4,  1, 5, 4,
        1, 2, 5,  2, 6, 5,
        2, 3, 7,  2, 7, 6,
        3, 0, 7,  0, 4, 7,
        4, 5, 7,  5, 6, 7,
    };
    mesh_.upload(vertices, 8, 3, indices, 36, {
        {0, 3, 0},
    });
}

void SkyBox::reset() {
    mesh_.reset();

    if (shader_registered_) {
        if (--shared_.refcount == 0) {
            shared_.program.reset();
        }
        shader_registered_ = false;
    }
}

bool SkyBox::valid() const {
    return shader_registered_ && shared_.program.valid() && mesh_.valid();
}

void SkyBox::draw(const Matrix<double>& proj_view_rot,
                  const CubemapTexture& cube) {
    if (!valid() || !cube.valid())
        return;

    glDepthFunc(GL_LEQUAL);
    {
        shared_.program.use();
        set_mat4(shared_.proj_view_loc, proj_view_rot);
        glUniform1i(shared_.cube_loc, 0);

        cube.bind(0);
        mesh_.draw();
        CubemapTexture::unbind();
    }
    glDepthFunc(GL_LESS);
}


// --- Surfaces ---

namespace {

const char* kSurfaceVertexSrc = R"(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec3 aNormal;

uniform mat4 uProjView;
uniform mat4 uView;
uniform mat4 uModel;

out vec3 vNormalView;
out vec3 vColor;

void main() {
    gl_Position = uProjView * uModel * vec4(aPos, 1.0);
    vNormalView = mat3(uView) * mat3(uModel) * aNormal;
    vColor = aColor;
}
)";

const char* kSurfaceFragmentSrc = R"(
#version 330 core

in vec3 vNormalView;
in vec3 vColor;

uniform sampler2D uMatCap;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormalView);
    vec2 uv = N.xy * 0.5 + vec2(0.5);
    uv.y = 1.0 - uv.y;
    FragColor = vec4(texture(uMatCap, uv).rgb, 1.0);
}
)";

} // anonymous namespace

Surfaces::~Surfaces() { reset(); }

void Surfaces::init() {
    if (shader_registered_) return;

    if (shared_.refcount++ == 0) {
        shared_.program = ShaderProgram(kSurfaceVertexSrc, kSurfaceFragmentSrc);
        shared_.proj_view_loc = shared_.program.uniform_location("uProjView");
        shared_.view_loc      = shared_.program.uniform_location("uView");
        shared_.model_loc     = shared_.program.uniform_location("uModel");
        shared_.matcap_loc    = shared_.program.uniform_location("uMatCap");
    }
    shader_registered_ = true;
}

void Surfaces::reset() {
    mesh_.reset();

    if (shader_registered_) {
        if (--shared_.refcount == 0)
            shared_.program.reset();
        shader_registered_ = false;
    }
}

bool Surfaces::valid() const {
    return shader_registered_ && shared_.program.valid() && mesh_.valid();
}

void Surfaces::upload(const geom::RenderMesh& m) {
    if (m.vertices.empty() || m.indices.empty()) {
        mesh_.reset();
        return;
    }
    mesh_.upload(m.vertices.data(),
                 m.vertices.size() / RenderMesh::floats_per_vertex,
                 RenderMesh::floats_per_vertex,
                 m.indices.data(), m.indices.size(),
                 {
                     {0, 3, 0},
                     {1, 3, 3},
                     {2, 3, 6},
                 });
}

void Surfaces::draw(const Matrix<double>& proj_view,
                    const Matrix<double>& view,
                    const Matrix<double>& model,
                    const Texture& matcap) const {
    if (!valid())
        return;

    shared_.program.use();
    set_mat4(shared_.proj_view_loc, proj_view);
    set_mat4(shared_.view_loc,      view);
    set_mat4(shared_.model_loc,     model);

    matcap.bind(0);
    glUniform1i(shared_.matcap_loc, 0);

    mesh_.draw();
}

} // namespace geom::render
