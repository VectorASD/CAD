#pragma once

#include "vec3.hpp"

#include <array>
#include <cstddef>
#include <vector>


namespace geom {

// Runtime-degree de Casteljau. This is what you would have to use
// for the whole curve if you did not split it into spans: O(n^2)
// per point, no locality, numerically unstable for large n.
// Prefer bezier<D>() with D = 3 for B-spline spans.
template<Scalar S>
Vec3<S> bezierVector(const std::vector<Vec3<S>>& P, const S& t) {
    if (P.empty()) return {};   // degenerate: no control points, no curve
    const S u = S(1) - t;
    std::vector<Vec3<S>> Q = P;
    for (std::size_t k = 1; k < Q.size(); ++k)
        for (std::size_t i = 0; i + k < Q.size(); ++i)
            Q[i] = Q[i] * u + Q[i+1] * t;
    return Q[0];
}
// Degree-generic de Casteljau.
// D is the degree; the curve has D + 1 control points.
// No heap allocation. For small D (2..5) both loops are typically
// fully unrolled, since D is a compile-time constant.
template<std::size_t D, Scalar S>
    requires (D >= 1)
Vec3<S> bezier(const std::array<Vec3<S>, D + 1>& P, const S& t) {
    const S u = S(1) - t;
    std::array<Vec3<S>, D + 1> Q = P;
    for (std::size_t k = 1; k <= D; ++k)
        for (std::size_t i = 0; i + k <= D; ++i)
            Q[i] = Q[i] * u + Q[i+1] * t;
    return Q[0];
}

} // namespace geom
