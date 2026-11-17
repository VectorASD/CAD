#pragma once

#include "render_mesh.hpp"
#include "vec3.hpp"

#include <memory>
#include <string>
#include <vector>


namespace geom {

// Polymorphic wrapper so a .bpt file can hold patches of mixed degrees.
class BezierSurfaceAny {
public:
    virtual ~BezierSurfaceAny() = default;
    virtual int degree_u() const = 0;
    virtual int degree_v() const = 0;
    virtual RenderMesh tessellate(std::size_t nu, std::size_t nv) const = 0;
};

// Factory: (du+1)*(dv+1) control points, u-major. Supported (du, dv)
// is [1..5] x [1..5]; nullptr outside that range or on size mismatch.
std::unique_ptr<BezierSurfaceAny> make_bezier_surface(
        int du, int dv, const std::vector<Vec3<double>>& points);


struct Model {
    std::vector<std::unique_ptr<BezierSurfaceAny>> patches;
};

// Load a .bpt file. Header line: "numPatches [degree_u [degree_v]]" --
// degree_u defaults to 3 if absent, degree_v defaults to degree_u.
// Then numPatches blocks of (du+1)*(dv+1) "x y z" triples.
Model load_bpt(const std::string& path, bool flip = false);

// Tessellate every patch and concatenate into one RenderMesh; indices
// are offset so the combined result is drawable in a single call.
RenderMesh tessellate_model(const Model& model, std::size_t nu, std::size_t nv);

} // namespace geom