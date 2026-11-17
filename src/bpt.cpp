#include "bpt.hpp"

#include "bezier_surface.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <utility>


namespace geom {

namespace {

template<std::size_t D, std::size_t E>
class BezierSurfaceImpl : public BezierSurfaceAny {
public:
    explicit BezierSurfaceImpl(typename BezierSurface<D, E, double>::Grid grid,
                               bool flip)
        : surface_(std::move(grid), flip) {}

    int degree_u() const override { return static_cast<int>(D); }
    int degree_v() const override { return static_cast<int>(E); }

    RenderMesh tessellate(std::size_t nu, std::size_t nv) const override {
        return surface_.tessellate(nu, nv);
    }
private:
    BezierSurface<D, E, double> surface_;
};

template<std::size_t D, std::size_t E>
std::unique_ptr<BezierSurfaceAny> make_impl(
        const std::vector<Vec3<double>>& pts, bool flip) {
    typename BezierSurface<D, E, double>::Grid grid{};
    std::size_t k = 0;
    for (std::size_t i = 0; i <= D; ++i)
        for (std::size_t j = 0; j <= E; ++j)
            grid[i][j] = pts[k++];
    return std::make_unique<BezierSurfaceImpl<D, E>>(std::move(grid), flip);
}

template<std::size_t D>
std::unique_ptr<BezierSurfaceAny> dispatch_e(
        int dv, const std::vector<Vec3<double>>& pts, bool flip) {
    switch (dv) {
        case 1: return make_impl<D, 1>(pts, flip);
        case 2: return make_impl<D, 2>(pts, flip);
        case 3: return make_impl<D, 3>(pts, flip);
        case 4: return make_impl<D, 4>(pts, flip);
        case 5: return make_impl<D, 5>(pts, flip);
    }
    return nullptr;
}

} // anonymous namespace


std::unique_ptr<BezierSurfaceAny> make_bezier_surface(
        int du, int dv, const std::vector<Vec3<double>>& points, bool flip) {
    if (du < 1 || du > 5 || dv < 1 || dv > 5) return nullptr;

    const std::size_t expected = static_cast<std::size_t>(du + 1) * static_cast<std::size_t>(dv + 1);
    if (points.size() != expected)
        return nullptr;

    switch (du) {
        case 1: return dispatch_e<1>(dv, points, flip);
        case 2: return dispatch_e<2>(dv, points, flip);
        case 3: return dispatch_e<3>(dv, points, flip);
        case 4: return dispatch_e<4>(dv, points, flip);
        case 5: return dispatch_e<5>(dv, points, flip);
    }
    return nullptr;
}


Model load_bpt(const std::string& path, bool flip) {
    Model model;

    std::ifstream in(path);
    if (!in) {
        std::cerr << "load_bpt: cannot open " << path << std::endl;
        return model;
    }

    // Format: numPatches, then for each patch "du dv" followed by
    // (du+1)*(dv+1) "x y z" triples.
    int num_patches = 0;
    if (!(in >> num_patches) || num_patches <= 0) {
        std::cerr << "load_bpt: bad patch count in " << path << std::endl;
        return model;
    }

    model.patches.reserve(static_cast<std::size_t>(num_patches));

    for (int p = 0; p < num_patches; ++p) {
        int du = 0, dv = 0;
        if (!(in >> du >> dv)) {
            std::cerr << "load_bpt: missing degrees at patch " << p
                      << " in " << path << std::endl;
            model.patches.clear();
            return model;
        }

        const std::size_t per_patch =
            static_cast<std::size_t>(du + 1) * static_cast<std::size_t>(dv + 1);
        std::vector<Vec3<double>> buf(per_patch);

        for (std::size_t k = 0; k < per_patch; ++k) {
            double x, y, z;
            if (!(in >> x >> y >> z)) {
                std::cerr << "load_bpt: truncated data in " << path
                          << " at patch " << p << std::endl;
                model.patches.clear();
                return model;
            }
            buf[k] = {x, y, z};
        }

        auto patch = make_bezier_surface(du, dv, buf, flip);
        if (!patch) {
            std::cerr << "load_bpt: unsupported degrees "
                      << du << "x" << dv << " at patch " << p << std::endl;
            model.patches.clear();
            return model;
        }
        model.patches.push_back(std::move(patch));
    }

    std::cout << "load_bpt: " << path << " -> "
              << model.patches.size() << " patches" << std::endl;
    return model;
}


RenderMesh tessellate_model(const Model& model,
                            std::size_t nu, std::size_t nv) {
    RenderMesh merged;
    std::uint32_t base = 0;

    for (const auto& patch : model.patches) {
        RenderMesh part = patch->tessellate(nu, nv);
        merged.vertices.insert(merged.vertices.end(),
                               part.vertices.begin(), part.vertices.end());
        merged.indices.reserve(merged.indices.size() + part.indices.size());
        for (std::uint32_t idx : part.indices) {
            merged.indices.push_back(base + idx);
        }
        base += static_cast<std::uint32_t>(
            part.vertices.size() / RenderMesh::floats_per_vertex);
    }

    return merged;
}

} // namespace geom