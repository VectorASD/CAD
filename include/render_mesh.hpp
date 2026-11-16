#pragma once

#include <cstdint>
#include <vector>


namespace geom {

// Flattened mesh produced by the geometry kernel and consumed verbatim
// by the render layer. Vertex layout is fixed: [pos.xyz, color.rgb] per
// vertex. The renderer never sees exact types -- conversion happened
// inside the kernel's tessellate().
struct RenderMesh {
    std::vector<float>         vertices;
    std::vector<std::uint32_t> indices;

    static constexpr std::size_t floats_per_vertex = 6;
};

} // namespace geom
