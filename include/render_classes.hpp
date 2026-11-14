#pragma once

#include <cstddef>
#include <initializer_list>


namespace geom::render {

struct VertexAttribute {
    int location;       // glVertexAttribPointer index
    int components;     // 1..4
    int offset_floats;  // float offset from the start of a vertex
};


class ShaderProgram {
public:
    ShaderProgram() = default;
    ShaderProgram(const char* vertex_src, const char* fragment_src);
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    void use() const;
    int uniform_location(const char* name) const;
    unsigned int id() const { return program_; }
    bool valid() const { return program_ != 0; }
    void reset();

private:
    unsigned int program_ = 0;
};


class Fbo {
public:
    Fbo() = default;
    Fbo(int width, int height);
    ~Fbo();

    Fbo(const Fbo&) = delete;
    Fbo& operator=(const Fbo&) = delete;
    Fbo(Fbo&& other) noexcept;
    Fbo& operator=(Fbo&& other) noexcept;

    void bind() const;
    static void unbind();
    int width() const { return width_; }
    int height() const { return height_; }
    bool valid() const { return fbo_ != 0; }
    void reset();

private:
    unsigned int fbo_ = 0;
    unsigned int color_tex_ = 0;
    unsigned int depth_rb_ = 0;
    int width_ = 0;
    int height_ = 0;
};


class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    void upload(const float* vertices, std::size_t vertex_count,
                std::size_t floats_per_vertex,
                const unsigned int* indices, std::size_t index_count,
                std::initializer_list<VertexAttribute> attributes);

    void draw() const;   // uses the index count stored at upload time
    std::size_t index_count() const { return index_count_; }
    bool valid() const { return vao_ != 0 && index_count_ > 0; }
    void reset();

private:
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ibo_ = 0;
    std::size_t index_count_ = 0;
};

} // namespace geom::render
