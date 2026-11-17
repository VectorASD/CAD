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
    bool flip_;

public:
    BezierSurface() = default;
    explicit BezierSurface(Grid grid, bool flip = false)
        : grid_(std::move(grid)), flip_(flip) {}

    const Grid& grid() const { return grid_; }
    bool flipped() const { return flip_; }

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

        // Pre-evaluate the position grid once; normals reuse neighbors
        std::vector<Vec3<double>> pos((nu + 1) * (nv + 1));
        for (std::size_t j = 0; j <= nv; ++j) {
            const double v = static_cast<double>(j) / static_cast<double>(nv);

            std::array<Vec3<double>, D + 1> row;
            for (std::size_t ii = 0; ii <= D; ++ii)
                row[ii] = bezier<E>(gd[ii], v);

            for (std::size_t i = 0; i <= nu; ++i) {
                const double u = static_cast<double>(i) / static_cast<double>(nu);
                pos[j * (nu + 1) + i] = bezier<D>(row, u);
            }
        }

        RenderMesh m;
        m.vertices.reserve((nu + 1) * (nv + 1) * RenderMesh::floats_per_vertex);
        m.indices.reserve(nu * nv * 6);

        for (std::size_t j = 0; j <= nv; ++j) {
            for (std::size_t i = 0; i <= nu; ++i) {
                const std::size_t idx = j * (nu + 1) + i;
                const Vec3<double> p = pos[idx];

                const std::size_t i_next = (i < nu) ? i + 1 : i - 1;
                const Vec3<double> p_u = pos[j * (nu + 1) + i_next];
                const Vec3<double> du = (i < nu) ? (p_u - p) : (p - p_u);

                const std::size_t j_next = (j < nv) ? j + 1 : j - 1;
                const Vec3<double> p_v = pos[j_next * (nu + 1) + i];
                const Vec3<double> dv = (j < nv) ? (p_v - p) : (p - p_v);

                const Vec3<double> cross = flip_ ? dv.cross(du) : du.cross(dv);
                const Vec3<double> n = normalizeApprox(cross);

                const float uf = static_cast<float>(i) / static_cast<float>(nu);
                const float vf = static_cast<float>(j) / static_cast<float>(nv);

                // [pos, color, normal]
                m.vertices.push_back(static_cast<float>(p.x));
                m.vertices.push_back(static_cast<float>(p.y));
                m.vertices.push_back(static_cast<float>(p.z));
                m.vertices.push_back(uf);
                m.vertices.push_back(vf);
                m.vertices.push_back(0.2f);
                m.vertices.push_back(static_cast<float>(n.x));
                m.vertices.push_back(static_cast<float>(n.y));
                m.vertices.push_back(static_cast<float>(n.z));
            }
        }

        // Vertex order: v-major, index = j*(nu+1) + i
        for (std::size_t j = 0; j < nv; ++j) {
            for (std::size_t i = 0; i < nu; ++i) {
                const std::uint32_t a = static_cast<std::uint32_t>(j * (nu + 1) + i);
                const std::uint32_t b = static_cast<std::uint32_t>(j * (nu + 1) + i + 1);
                const std::uint32_t c = static_cast<std::uint32_t>((j + 1) * (nu + 1) + i + 1);
                const std::uint32_t d = static_cast<std::uint32_t>((j + 1) * (nu + 1) + i);
                if (flip_)
                    m.indices.insert(m.indices.end(), { a, c, b, a, d, c });
                else
                    m.indices.insert(m.indices.end(), { a, b, c, a, c, d });
            }
        }
        return m;
    }
};

} // namespace geom
