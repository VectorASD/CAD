#pragma once

#include <cstddef>
#include <initializer_list>
#include <mutex>
#include <vector>

#include "matrix.hpp"
#include "render_mesh.hpp"


namespace geom::render {

struct VertexAttribute {
    int location;       // glVertexAttribPointer index
    int components;     // 1..4
    int offset_floats;  // float offset from the start of a vertex
};

// One marker instance: the octahedron template placed at `position`,
// scaled uniformly by `scale`, tinted by `tint`.
struct Marker {
    Vec3<double> position;
    Vec3<double> tint{1.0, 1.0, 1.0};
    double       scale = 0.05;
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


enum class FboKind {
    ColorDepth, // owns color texture + depth renderbuffer (default)
    Empty,      // no attachments; caller attaches via attach_cubemap_face etc.
};

class Fbo {
public:
    Fbo() = default;
    Fbo(int width, int height, FboKind kind = FboKind::ColorDepth);
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

    // Attach a face of a cubemap as the color attachment of this framebuffer.
    // face: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z. Requires a current GL context.
    void attach_cubemap_face(unsigned int cube_tex, int face);

    unsigned int color_texture() const { return color_tex_; }
    static unsigned int current();
    static void bind_id(unsigned int id);

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


class SpriteRenderer {
public:
    SpriteRenderer() = default;
    ~SpriteRenderer();

    SpriteRenderer(const SpriteRenderer&) = delete;
    SpriteRenderer& operator=(const SpriteRenderer&) = delete;

    void init();
    void reset();
    bool valid() const;

    void draw_tile(unsigned int tex_id,
                   float u0, float v0, float u1, float v1);

private:
    struct Shared {
        ShaderProgram program;
        int tex_loc;
        int uv_range_loc;
        int refcount;
        Shared() : tex_loc(-1), uv_range_loc(-1), refcount(0) {}
    };
    inline static Shared shared_;
    bool shader_registered_ = false;

    Mesh mesh_;
};


class Markers {
public:
    Markers() = default;
    ~Markers();

    Markers(const Markers&) = delete;
    Markers& operator=(const Markers&) = delete;

    void init();
    void reset();
    bool valid() const;

    void set(const std::vector<Marker>& markers);
    void clear();

    void draw(const Matrix<double>& proj_view,
              const Matrix<double>& model) const;

private:
    struct Shared {
        ShaderProgram program;
        int proj_view_loc;
        int model_loc;
        int tint_loc;
        int refcount;
        Shared()
            : proj_view_loc(-1), model_loc(-1), tint_loc(-1), refcount(0) {}
    };
    inline static Shared shared_;
    bool shader_registered_ = false;

    Mesh mesh_;
    mutable std::mutex mutex_;
    std::vector<Marker> markers_;
};


class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    void create(int width, int height, int filter,
                const std::uint8_t* rgba = nullptr);

    bool load_from_file(const std::string& path, int filter);

    void bind(int unit = 0) const;
    static void unbind();

    unsigned int id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool valid() const { return id_ != 0; }
    void reset();

private:
    unsigned int id_ = 0;
    int width_ = 0;
    int height_ = 0;
};


class CubemapTexture {
public:
    CubemapTexture() = default;
    ~CubemapTexture();
    CubemapTexture(const CubemapTexture&) = delete;
    CubemapTexture& operator=(const CubemapTexture&) = delete;
    CubemapTexture(CubemapTexture&& other) noexcept;
    CubemapTexture& operator=(CubemapTexture&& other) noexcept;

    void create(int face_size, int filter);   // empty cubemap

    // Loads an atlas image, then for each of the 6 cubemap faces renders the
    // specified tile from the atlas into that face (no glReadPixels). Tiles
    // are indexed row-major: tile = ty * tile_cols + tx, ty=0 is the TOP row
    // of the image as loaded by stb_image (top-down).
    // face_tile_indexes[0..5] = tile index for +X,-X,+Y,-Y,+Z,-Z.
    bool load_from_atlas(const std::string& path,
                         int tile_cols, int tile_rows,
                         const int tile_indexes[6]);

    void bind(int unit = 0) const;
    static void unbind();

    unsigned int id() const { return id_; }
    int size() const { return size_; }
    bool valid() const { return id_ != 0; }
    void reset();

private:
    unsigned int id_ = 0;
    int size_ = 0;
};


class SkyBox {
public:
    SkyBox() = default;
    ~SkyBox();

    SkyBox(const SkyBox&) = delete;
    SkyBox& operator=(const SkyBox&) = delete;

    void init();
    void reset();
    bool valid() const;

    void draw(const Matrix<double>& proj_view_rot,
              const CubemapTexture& cube);

private:
    struct Shared {
        ShaderProgram program;
        int proj_view_loc;
        int cube_loc;
        int refcount;
        Shared() : proj_view_loc(-1), cube_loc(-1), refcount(0) {}
    };
    inline static Shared shared_;
    bool shader_registered_ = false;

    Mesh mesh_;
};


// Owns the GL state needed to draw a kernel-produced RenderMesh. No
// geometry knowledge here: vertices arrive pre-tessellated from the
// geometry kernel's tessellate(), we just upload and draw.
class Surfaces {
public:
    Surfaces() = default;
    ~Surfaces();

    Surfaces(const Surfaces&) = delete;
    Surfaces& operator=(const Surfaces&) = delete;

    void init();
    void reset();
    bool valid() const;

    void upload(const geom::RenderMesh& m);
    void draw(const Matrix<double>& proj_view,
              const Matrix<double>& view,
              const Matrix<double>& model,
              const Texture& matcap,
              bool wireframe) const;

private:
    struct Shared {
        ShaderProgram program;
        int proj_view_loc;
        int view_loc;
        int model_loc;
        int matcap_loc;
        int refcount;
        Shared()
            : proj_view_loc(-1), view_loc(-1), model_loc(-1),
              matcap_loc(-1), refcount(0) {}
    };
    inline static Shared shared_;
    bool shader_registered_ = false;

    Mesh mesh_;
};

} // namespace geom::render
