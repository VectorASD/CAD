#pragma once

#include "bezier.hpp"
#include "render_mesh.hpp"
#include "scalar.hpp"
#include "vec3.hpp"

#include <array>
#include <cstddef>


namespace geom {

// Tensor-product Bezier surface, degree D along u, degree E along v,
// evaluated in exact arithmetic S. tessellate() is the only place where
// S leaves the kernel and float appears.
template<std::size_t D, std::size_t E, Scalar S>
    requires (D >= 1 && E >= 1)
class BezierSurface {
public:
    using Grid = std::array<std::array<Vec3<S>, E + 1>, D + 1>;

private:
    Grid grid_{};

public:
    BezierSurface() = default;
    explicit BezierSurface(Grid grid) : grid_(std::move(grid)) {}

    const Grid& grid() const { return grid_; }

    // Exact evaluation in S
    Vec3<S> eval(const S& u, const S& v) const {
        std::array<Vec3<S>, E + 1> column;
        for (std::size_t j = 0; j <= E; ++j) {
            std::array<Vec3<S>, D + 1> row;
            for (std::size_t i = 0; i <= D; ++i) row[i] = grid_[i][j];
            column[j] = bezier<D>(row, u);
        }
        return bezier<E>(column, v);
    }

    // Produce a render-side mesh. nu * nv cells, 2 triangles each
    RenderMesh tessellate(std::size_t nu, std::size_t nv) const
        requires DoubleConvertible<S>
    {
        // Convert once, keep the layout: gd[ii] is a v-direction slice
        // (E+1 contiguous points), directly usable with bezier<E>
        std::array<std::array<Vec3<double>, E + 1>, D + 1> gd;
        for (std::size_t i = 0; i <= D; ++i)
            for (std::size_t j = 0; j <= E; ++j)
                gd[i][j] = to_double(grid_[i][j]);

        RenderMesh m;
        m.vertices.reserve((nu + 1) * (nv + 1) * RenderMesh::floats_per_vertex);
        m.indices.reserve(nu * nv * 6);

        for (std::size_t j = 0; j <= nv; ++j) {
            const double v = static_cast<double>(j) / static_cast<double>(nv);

            // Collapse along v first: gd[ii] is a v-slice, no copy needed
            std::array<Vec3<double>, D + 1> row;
            for (std::size_t ii = 0; ii <= D; ++ii)
                row[ii] = bezier<E>(gd[ii], v);

            for (std::size_t i = 0; i <= nu; ++i) {
                const double u = static_cast<double>(i) / static_cast<double>(nu);
                const Vec3<double> p = bezier<D>(row, u);
                // Computing p                          - tessellation
                // Connecting the points into triangles - triangulation

                m.vertices.push_back(static_cast<float>(p.x));
                m.vertices.push_back(static_cast<float>(p.y));
                m.vertices.push_back(static_cast<float>(p.z));
                m.vertices.push_back(static_cast<float>(u));
                m.vertices.push_back(static_cast<float>(v));
                m.vertices.push_back(0.2f);
            }
        }

        // Vertex order is now v-major: index = j*(nu+1) + i
        for (std::size_t j = 0; j < nv; ++j) {
            for (std::size_t i = 0; i < nu; ++i) {
                const std::uint32_t a = static_cast<std::uint32_t>(j * (nu + 1) + i);
                const std::uint32_t b = static_cast<std::uint32_t>(j * (nu + 1) + i + 1);
                const std::uint32_t c = static_cast<std::uint32_t>((j + 1) * (nu + 1) + i + 1);
                const std::uint32_t d = static_cast<std::uint32_t>((j + 1) * (nu + 1) + i);
                m.indices.insert(m.indices.end(), { a, b, c, a, c, d });
            }
        }
        return m;
    }
};

} // namespace geom
