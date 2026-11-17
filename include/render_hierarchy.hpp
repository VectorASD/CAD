#pragma once

#include "matrix.hpp"
#include "render_classes.hpp"
#include "render_mesh.hpp"

#include <glad/glad.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>


namespace geom::render {

struct RenderContext {
    const Matrix<double>& proj;
    const Matrix<double>& view;
    const Matrix<double>& proj_view;
    bool wireframe = false;
};


class Node {
public:
    virtual ~Node() = default;

    void set_tag(std::string tag) {
        tag_ = std::move(tag);
    }

    const std::string& tag() const { return tag_; }

    template <typename T>
    T* find_by_tag(const std::string& tag) {
        auto it = tag_map_.find(tag);
        if (it != tag_map_.end())
            return dynamic_cast<T*>(it->second);
        return nullptr;
    }

    void add(std::unique_ptr<Node> child) {
        if (!child->tag_.empty())
            tag_map_[child->tag_] = child.get();
        for (const auto& [k, v] : child->tag_map_)
            tag_map_[k] = v;
        children_.push_back(std::move(child));
        children_.back()->recalc(matrix_);
    }

    void draw(const RenderContext& ctx) {
        ensure_init();
        do_draw(ctx);
        for (auto& c : children_)
            c->draw(ctx);
        do_post_draw(ctx);
    }

    void recalc(const Matrix<double>& matrix) {
        parent_matrix_ = matrix;
        matrix_ = do_recalc(matrix);
        for (auto& c : children_)
            c->recalc(matrix_);
    }

    void reset() {
        if (initialized_) {
            for (auto& c : children_)
                c->reset();
            do_reset();
            initialized_ = false;
        }
    }

    template<typename T, typename F>
    void visit(F&& f) {
        if (auto* p = dynamic_cast<T*>(this)) f(*p);
        for (auto& c : children_) c->visit<T>(f);
    }

protected:
    virtual void do_init() {}
    virtual Matrix<double> do_recalc(const Matrix<double>& matrix) { return matrix; }
    virtual void do_draw(const RenderContext& ctx) = 0;
    virtual void do_post_draw(const RenderContext&) {}
    virtual void do_reset() {}
    void refresh() { recalc(parent_matrix_); }
    Matrix<double>& world() { return matrix_; }

private:
    std::vector<std::unique_ptr<Node>> children_;
    Matrix<double> parent_matrix_ = Matrix<double>::identity(4);
    Matrix<double> matrix_        = Matrix<double>::identity(4);
    bool initialized_ = false;
    std::string tag_;
    std::unordered_map<std::string, Node*> tag_map_;

    void ensure_init() {
        if (!initialized_) {
            do_init();
            initialized_ = true;
        }
    }
};


class TransformNode : public Node {
public:
    void set_local(const Matrix<double>& local) {
        local_ = local;
        refresh();
    }

protected:
    Matrix<double> do_recalc(const Matrix<double>& matrix) override {
        return matrix * local_;
    }
    void do_draw(const RenderContext&) override {}   // transform-only: no own rendering

private:
    Matrix<double> local_ = Matrix<double>::identity(4);
};


// Resource holder for a 2D texture. Loads in do_init (needs a current
// GL context), releases in do_reset, reloads on the next init after a
// reset. get() returns a stable reference: the underlying unique_ptr is
// created in the constructor, so the pointer does not change across
// init/reset cycles.
class TextureNode : public Node {
public:
    TextureNode(std::string path, int filter = GL_LINEAR)
        : path_(std::move(path)), filter_(filter),
          texture_(std::make_unique<Texture>()) {}

    const Texture& get() const { return *texture_; }

protected:
    void do_init() override {
        if (!texture_->load_from_file(path_, filter_)) {
            std::cerr << "TextureNode: failed to load " << path_ << std::endl;
            const std::uint8_t gray[4] = {180, 180, 180, 255};
            texture_->create(1, 1, GL_NEAREST, gray);
        }
    }
    void do_reset() override { texture_->reset(); }
    void do_draw(const RenderContext&) override {}

private:
    std::string path_;
    int filter_;
    std::unique_ptr<Texture> texture_;
};


class MarkersNode : public Node {
public:
    explicit MarkersNode(std::unique_ptr<Markers> markers)
        : markers_(std::move(markers)) {}

protected:
    void do_init() override { markers_->init(); }
    void do_reset() override { markers_->reset(); }
    void do_draw(const RenderContext& ctx) override {
        markers_->draw(ctx.proj_view, world());
    }

private:
    std::unique_ptr<Markers> markers_;
};


class SurfacesNode : public Node {
public:
    SurfacesNode(std::unique_ptr<Surfaces> surfaces,
                 std::unique_ptr<geom::RenderMesh> mesh)
        : surfaces_(std::move(surfaces)), mesh_(std::move(mesh)) {}

    void set_matcap(const Texture* matcap) { matcap_ = matcap; }

protected:
    void do_init() override {
        surfaces_->init();
        surfaces_->upload(*mesh_);
    }
    void do_reset() override { surfaces_->reset(); }
    void do_draw(const RenderContext& ctx) override {
        if (!matcap_) {
            std::cerr << "SurfacesNode: matcap not set" << std::endl;
            return;
        }
        surfaces_->draw(ctx.proj_view, ctx.view, world(), *matcap_, ctx.wireframe);
    }

private:
    std::unique_ptr<Surfaces> surfaces_;
    std::unique_ptr<geom::RenderMesh> mesh_;
    const Texture* matcap_ = nullptr;
};


// Owns its cubemaps and cycles through them on request. Model transforms
// are ignored: the skybox is always positioned at the camera. Draw order
// in the tree matters -- add last so GL_LEQUAL only fills untouched pixels.
class SkyBoxNode : public Node {
public:
    explicit SkyBoxNode(std::unique_ptr<SkyBox> skybox)
        : skybox_(std::move(skybox)) {}

    void add_atlas(std::string path, int cols, int rows,
                   std::array<int, 6> indexes) {
        atlases_.push_back({std::move(path), cols, rows, indexes});
    }

    void cycle() {
        if (!textures_.empty())
            current_ = (current_ + 1) % textures_.size();
    }

protected:
    void do_init() override {
        skybox_->init();
        textures_.reserve(atlases_.size());
        for (const auto& spec : atlases_) {
            CubemapTexture tex;
            if (tex.load_from_atlas(spec.path, spec.cols, spec.rows,
                                    spec.indexes.data()))
                textures_.push_back(std::move(tex));
        }
    }
    void do_reset() override {
        skybox_->reset();
        textures_.clear();
        current_ = 0;
    }
    void do_draw(const RenderContext& ctx) override {
        if (textures_.empty()) return;
        Matrix<double> view_rot = ctx.view;
        view_rot(0, 3) = 0.0;
        view_rot(1, 3) = 0.0;
        view_rot(2, 3) = 0.0;
        const Matrix<double> pv = ctx.proj * view_rot;
        skybox_->draw(pv, textures_[current_]);
    }

private:
    struct AtlasSpec {
        std::string path;
        int cols, rows;
        std::array<int, 6> indexes;
    };

    std::unique_ptr<SkyBox> skybox_;
    std::vector<AtlasSpec> atlases_;
    std::vector<CubemapTexture> textures_;
    std::size_t current_ = 0;
};


// Renders itself + all children into its own FBO. Saves and restores the
// previously-bound FBO so nested FboNodes work. After draw(), read_pixels
// pulls the RGBA buffer.
class FboNode : public Node {
public:
    FboNode(int width, int height)
        : width_(width), height_(height) {}

    void resize(int width, int height) {
        width_ = width;
        height_ = height;
        if (fbo_.valid()) fbo_ = Fbo(width, height);
    }

    int width() const { return width_; }
    int height() const { return height_; }

    void read_pixels(std::uint8_t* dst) const {
        const unsigned int prev = Fbo::current();
        fbo_.bind();
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, dst);
        Fbo::bind_id(prev);
    }

protected:
    void do_init() override { fbo_ = Fbo(width_, height_); }
    void do_reset() override { fbo_.reset(); }
    void do_draw(const RenderContext&) override {
        saved_fbo_ = Fbo::current();
        fbo_.bind();
        glViewport(0, 0, fbo_.width(), fbo_.height());
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    void do_post_draw(const RenderContext&) override {
        Fbo::bind_id(saved_fbo_);
    }

private:
    int width_ = 0;
    int height_ = 0;
    Fbo fbo_;
    unsigned int saved_fbo_ = 0;
};

} // namespace geom::render
