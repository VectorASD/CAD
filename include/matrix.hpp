// Sources:
//   https://github.com/VectorASD/Magistracy/blob/main/1%20семестр/Современные%20проблемы%20информатики/matrix.py
//   https://github.com/VectorASD/Magistracy/blob/main/1%20семестр/Современные%20проблемы%20информатики/symbolic.py
// Ported to C++. Flat storage, row-major. All factories are static methods.

#pragma once

#include "scalar.hpp"
#include "vec3.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <ostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>


namespace geom {

namespace detail {
    inline constexpr double PI = 3.14159265358979323846;
    inline constexpr double PI_180 = PI / 180.0;
    inline double ctg(double x) {
        return 1.0 / std::tan(x);
    }
}

template<Scalar S>
class Matrix {
    std::size_t n_ = 0;
    std::vector<S> data_; // row-major, n_ * n_
    std::string letter_ = "M";

public:
    Matrix() = default;

    Matrix(std::initializer_list<std::initializer_list<S>> rows) {
        n_ = rows.size();
        data_.reserve(n_ * n_);
        for (const auto& row : rows) {
            if (row.size() != n_)
                throw std::invalid_argument("Matrix: not square");
            for (const auto& e : row)
                data_.push_back(e);
        }
    }

    std::size_t size() const { return n_; }
    const S* data() const { return data_.data(); }
    const S& operator()(std::size_t i, std::size_t j) const { return data_[i*n_ + j]; }
    S& operator()(std::size_t i, std::size_t j) { return data_[i*n_ + j]; }

    Matrix& operator()(std::string letter) { letter_ = std::move(letter); return *this; }
    const std::string& letter() const { return letter_; }

    Matrix operator+() const { return *this; }
    Matrix operator-() const {
        Matrix r = *this;
        for (auto& e : r.data_) e = -e;
        return r;
    }
    Matrix operator~() const { // transpose + conjugate
        Matrix r;
        r.n_ = n_;
        r.data_.resize(n_ * n_);
        for (std::size_t i = 0; i < n_; ++i)
            for (std::size_t j = 0; j < n_; ++j)
                r(i, j) = (*this)(j, i).conjugate();
        return r;
    }

    Matrix operator*(const S& s) const {
        Matrix r = *this;
        for (auto& e : r.data_) e = e * s;
        return r;
    }
    friend Matrix operator*(const S& s, const Matrix& m) {
        return m * s;
    }
    Matrix operator+(const Matrix& o) const {
        Matrix r = *this;
        for (std::size_t i = 0; i < r.data_.size(); ++i)
            r.data_[i] = r.data_[i] + o.data_[i];
        return r;
    }
    Matrix operator-(const Matrix& o) const {
        Matrix r = *this;
        for (std::size_t i = 0; i < r.data_.size(); ++i)
            r.data_[i] = r.data_[i] - o.data_[i];
        return r;
    }
    Matrix operator*(const Matrix& o) const { // matmul
        Matrix r;
        r.n_ = n_;
        r.data_.assign(n_ * n_, S(0));
        for (std::size_t i = 0; i < n_; ++i)
            for (std::size_t j = 0; j < n_; ++j) {
                S sum = S(0);
                for (std::size_t k = 0; k < n_; ++k)
                    sum += (*this)(i, k) * o(k, j);
                r(i, j) = sum;
            }
        return r;
    }

    friend bool operator==(const Matrix& a, const Matrix& b) {
        return a.n_ == b.n_ && a.data_ == b.data_;
    }

    // --- Factories ---

    static Matrix identity(std::size_t n) {
        Matrix r;
        r.n_ = n;
        r.data_.assign(n * n, S(0));
        for (std::size_t i = 0; i < n; ++i) r.data_[i*n + i] = S(1);
        return r;
    }

    static Matrix rotation_z(S angle) requires DoubleConvertible<S> {
        double a = static_cast<double>(angle);
        double c = std::cos(a), s = std::sin(a);
        return Matrix({
            {S(c), S(-s), S(0), S(0)},
            {S(s), S(c),  S(0), S(0)},
            {S(0), S(0),  S(1), S(0)},
            {S(0), S(0),  S(0), S(1)},
        });
    }

    static Matrix perspective(S fovy, S aspect, S near, S far) requires DoubleConvertible<S> {
        double f  = detail::ctg(static_cast<double>(fovy) * detail::PI_180 / 2);
        double a  = static_cast<double>(aspect);
        double n  = static_cast<double>(near);
        double fa = static_cast<double>(far);
        return Matrix({
            {S(f / a), S(0),   S(0),                          S(0)},
            {S(0),     S(f),   S(0),                          S(0)},
            {S(0),     S(0),   S(-(fa + n) / (fa - n)),       S(-2 * fa * n / (fa - n))},
            {S(0),     S(0),   S(-1),                         S(0)},
        });
    }

    struct ViewResult {
        Matrix matrix;
        Vec3<S> forward, right, up;
    };
    static ViewResult view(S x, S y, S z, S yaw, S pitch, S roll)
        requires DoubleConvertible<S>
    {
        double y_ = static_cast<double>(yaw)   * detail::PI_180;
        double p_ = static_cast<double>(pitch) * detail::PI_180;
        double r_ = static_cast<double>(roll)  * detail::PI_180;
        double sY = std::sin(y_), cY = std::cos(y_);
        double sP = std::sin(p_), cP = std::cos(p_);
        double sR = std::sin(r_), cR = std::cos(r_);

        double r00 = cY*cR,             r01 = -cY*sR,            r02 = sY;
        double r10 = sP*sY*cR + cP*sR,  r11 = -sP*sY*sR + cP*cR, r12 = -sP*cY;
        double r20 = -cP*sY*cR + sP*sR, r21 = cP*sY*sR + sP*cR,  r22 = cP*cY;

        double nx = -static_cast<double>(x);
        double ny = -static_cast<double>(y);
        double nz = -static_cast<double>(z);

        ViewResult v;
        v.matrix = Matrix({
            {S(r00), S(r01), S(r02), S(r00*nx + r01*ny + r02*nz)},
            {S(r10), S(r11), S(r12), S(r10*nx + r11*ny + r12*nz)},
            {S(r20), S(r21), S(r22), S(r20*nx + r21*ny + r22*nz)},
            {S(0),   S(0),   S(0),   S(1)},
        });
        v.forward = Vec3<S>{S(-r20), S(-r21), S(-r22)};
        v.right   = Vec3<S>{S(r00),  S(r01),  S(r02)};
        v.up      = Vec3<S>{S(r10),  S(r11),  S(r12)};
        return v;
    }

    static Matrix fast_inv_proj_view(S fovy, S aspect, S near, S far,
                                     S X, S Y, S Z,
                                     S yaw, S pitch, S roll)
        requires DoubleConvertible<S>
    {
        double y_ = static_cast<double>(yaw)   * detail::PI_180;
        double p_ = static_cast<double>(pitch) * detail::PI_180;
        double r_ = static_cast<double>(roll)  * detail::PI_180;
        double sY = std::sin(y_), cY = std::cos(y_);
        double sP = std::sin(p_), cP = std::cos(p_);
        double sR = std::sin(r_), cR = std::cos(r_);

        double r00 = cY*cR,             r01 = -cY*sR,            r02 = sY;
        double r10 = sP*sY*cR + cP*sR,  r11 = -sP*sY*sR + cP*cR, r12 = -sP*cY;
        double r20 = -cP*sY*cR + sP*sR, r21 = cP*sY*sR + sP*cR,  r22 = cP*cY;

        double F     = detail::ctg(static_cast<double>(fovy) * detail::PI_180 / 2);
        double F_A   = F / static_cast<double>(aspect);
        double n     = static_cast<double>(near);
        double fa    = static_cast<double>(far);
        double FN    = -(fa + n) / (fa - n);
        double _2fn  = -2 * fa * n / (fa - n);
        double FN_2fn = FN / _2fn;
        double m2fn  = -_2fn;
        double dx    = static_cast<double>(X);
        double dy    = static_cast<double>(Y);
        double dz    = static_cast<double>(Z);

        return Matrix({
            {S(r00/F_A), S(r10/F), S(dx/m2fn), S(dx*FN_2fn - r20)},
            {S(r01/F_A), S(r11/F), S(dy/m2fn), S(dy*FN_2fn - r21)},
            {S(r02/F_A), S(r12/F), S(dz/m2fn), S(dz*FN_2fn - r22)},
            {S(0),       S(0),     S(1/m2fn),  S(FN_2fn)},
        });
    }

    // --- Instance operations ---

    template<typename Tag = int>
    std::vector<std::tuple<S, S, S, Tag>> project(
        const std::vector<std::tuple<S, S, S, Tag>>& dots,
        S dx, S dy, S width, S height) const
    {
        std::vector<std::tuple<S, S, S, Tag>> out;
        out.reserve(dots.size());

        const S half = S(1) / S(2);
        const S zero = S(0), one = S(1), two = S(2);

        S hdx = dx * half, hdy = dy * half;
        S hw  = width * half, hh = height * half;

        for (const auto& [x, y, z, tag] : dots) {
            S tx = (*this)(0,0)*x + (*this)(0,1)*y + (*this)(0,2)*z + (*this)(0,3);
            S ty = (*this)(1,0)*x + (*this)(1,1)*y + (*this)(1,2)*z + (*this)(1,3);
            S tz = (*this)(2,0)*x + (*this)(2,1)*y + (*this)(2,2)*z + (*this)(2,3);
            S tw = (*this)(3,0)*x + (*this)(3,1)*y + (*this)(3,2)*z + (*this)(3,3);
            if (tw == zero) continue;
            tz = tz / tw;
            if (tz > one) continue;
            S px = (tx/tw + one) * hw + hdx;
            S py = (one - ty/tw) * hh + hdy;
            out.emplace_back(px, py, tz, tag);
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
            return std::get<2>(a) > std::get<2>(b);  // far -> near
        });
        return out;
    }

    std::tuple<S, S, S> unproject(
        const Matrix& proj,
        S x, S y,
        S dx, S dy, S width, S height) const
    {
        const S half = S(1) / S(2);
        const S zero = S(0), one = S(1), two = S(2);

        S nx = (x - dx * half) * (two / width) - one;
        S ny = one - (y - dy * half) * (two / height);
        S nz = proj(2,2) - proj(2,3);  // comment in matrix.py explains the built-in negation

        S wx = (*this)(0,0)*nx + (*this)(0,1)*ny + (*this)(0,2)*nz + (*this)(0,3);
        S wy = (*this)(1,0)*nx + (*this)(1,1)*ny + (*this)(1,2)*nz + (*this)(1,3);
        S wz = (*this)(2,0)*nx + (*this)(2,1)*ny + (*this)(2,2)*nz + (*this)(2,3);
        S ww = (*this)(3,0)*nx + (*this)(3,1)*ny + (*this)(3,2)*nz + (*this)(3,3);

        if (ww != zero) return {wx/ww, wy/ww, wz/ww};
        return {zero, zero, two};
    }

    friend std::ostream& operator<<(std::ostream& os, const Matrix& m) {
        os << m.letter_ << " = ";
        for (std::size_t i = 0; i < m.n_; ++i) {
            if (i) os << "\n" << std::string(m.letter_.size() + 3, ' ');
            os << "[ ";
            for (std::size_t j = 0; j < m.n_; ++j) {
                if (j) os << ", ";
                os << m(i, j);
            }
            os << " ]";
        }
        return os;
    }
};

} // namespace geom
