#pragma once

#include "scalar.hpp"

#include <cmath>
#include <ostream>


namespace geom {

template<Scalar S>
struct Vec3 {
    S x{}, y{}, z{};

    Vec3() = default;
    Vec3(S x_, S y_, S z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const {
        return {x + o.x, y + o.y, z + o.z};
    }
    Vec3 operator-(const Vec3& o) const {
        return {x - o.x, y - o.y, z - o.z};
    }
    Vec3 operator*(const S& t) const {
        return {x * t, y * t, z * t};
    }
    Vec3 operator-() const {
        return {-x, -y, -z};
    }

    S dot(const Vec3& o) const {
        return x*o.x + y*o.y + z*o.z;
    }
    S squaredLength() const { // self dot
        return x*x + y*y + z*z;
    }
    Vec3 cross(const Vec3& o) const {
        return {y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x};
    }

    friend Vec3 operator*(const S& t, const Vec3& v) {
        return v * t;
    }
    friend bool operator==(const Vec3& a, const Vec3& b) = default;
};

template<Scalar S>
std::ostream& operator<<(std::ostream& os, const Vec3<S>& v) {
    return os << '(' << v.x << ", " << v.y << ", " << v.z << ')';
}


// Approximate length. Kept free (not a Vec3 member) because:
//   - Vec3 is pure algebra (field ops, any Scalar); length needs sqrt,
//     which leaves the Scalar domain (sqrt(Fraction) is not a Fraction);
//   - "Approx" is the contract: returns double, not an exact value.
// For exact distance comparisons use squaredLength() instead.
template<DoubleConvertible S>
inline double lengthApprox(const Vec3<S>& v) {
    return std::sqrt(static_cast<double>(v.squaredLength()));
}

// Approximate conversion to a double-based Vec3. Lossy by design:
// Fraction<int>, mpq_class, etc. do not fit into double, but rendering,
// tessellation and SVG output do. This is the explicit boundary between
// exact kernel arithmetic and the render layer.
template<DoubleConvertible S>
inline Vec3<double> to_double(const Vec3<S>& v) {
    return {
        static_cast<double>(v.x),
        static_cast<double>(v.y),
        static_cast<double>(v.z),
    };
}

// Generic point: use this inside the kernel, where S is whatever the
// model uses (double, Fraction<int>, mpq_class, ...).
template<Scalar S>
using Point3 = Vec3<S>;

// Boundary type: only for rendering, tessellation output, SVG.
// Not for exact geometry - use Point3<S> there.
using Point3d = Point3<double>;

} // namespace geom
