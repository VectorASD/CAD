// Sources:
//   https://github.com/VectorASD/Magistracy/blob/main/1%20семестр/Современные%20проблемы%20информатики/graphics.py
// Camera wrapper over Matrix<double> factories.

#pragma once

#include "matrix.hpp"
#include "vec3.hpp"

#include <algorithm>
#include <cmath>


namespace geom::render {

class Camera {
    Vec3<double> pos_{0, 0, 2};
    Vec3<double> ypr_{-20, 30, 0};
    double fovy_ = 90, aspect_ = 1, near_ = 0.1, far_ = 100, radius_ = 1.5;
    bool orbital_mode_ = false;

    Matrix<double> proj_, view_, proj_view_, inv_proj_view_;
    Vec3<double> forward_, right_, up_;

public:
    Camera() { update_proj(); }

    void update_proj() {
        proj_ = Matrix<double>::perspective(fovy_, aspect_, near_, far_);
        update_proj_view();
    }

    void update_proj_view() {
        auto v = Matrix<double>::view(pos_.x, pos_.y, pos_.z, ypr_.x, ypr_.y, ypr_.z);
        view_ = v.matrix;
        forward_ = v.forward;
        right_   = v.right;
        up_      = v.up;

        if (orbital_mode_) {
            pos_ = forward_ * (-radius_);
            v = Matrix<double>::view(pos_.x, pos_.y, pos_.z, ypr_.x, ypr_.y, ypr_.z);
            view_ = v.matrix;
            forward_ = v.forward;
            right_   = v.right;
            up_      = v.up;
        }

        proj_view_ = proj_ * view_;
        inv_proj_view_ = Matrix<double>::fast_inv_proj_view(
            fovy_, aspect_, near_, far_,
            pos_.x, pos_.y, pos_.z,
            ypr_.x, ypr_.y, ypr_.z);
    }

    const Vec3<double>& pos() const { return pos_; }
    const Vec3<double>& ypr() const { return ypr_; }
    const Vec3<double>& forward() const { return forward_; }
    const Vec3<double>& right() const { return right_; }
    const Vec3<double>& up() const { return up_; }
    double fovy() const { return fovy_; }
    double aspect() const { return aspect_; }
    double near_plane() const { return near_; }
    double far_plane() const { return far_; }
    double radius() const { return radius_; }
    bool orbital_mode() const { return orbital_mode_; }
    const Matrix<double>& proj() const { return proj_; }
    const Matrix<double>& view() const { return view_; }
    const Matrix<double>& proj_view() const { return proj_view_; }
    const Matrix<double>& inv_proj_view() const { return inv_proj_view_; }

    void move_to(double x, double y, double z) {
        pos_ = {x, y, z};
        update_proj_view();
    }
    void move(double dx, double dy, double dz) {
        pos_ = {pos_.x + dx, pos_.y + dy, pos_.z + dz};
        update_proj_view();
    }
    void rotate(double dY, double dP, double dR) {
        ypr_ = {ypr_.x + dY, std::clamp(ypr_.y + dP, -75.0, 75.0), ypr_.z + dR};
        update_proj_view();
    }
    void move_forward(double dt) {
        if (orbital_mode_) {
            double mul = 1 + std::abs(dt) * 1.001;
            radius_ *= (dt > 0) ? 1 / mul : mul;
            update_proj_view();
        } else {
            move(forward_.x * dt, forward_.y * dt, forward_.z * dt);
        }
    }
    void move_right(double dt) {
        if (orbital_mode_) { rotate(dt * -90, 0, 0); return; }
        move(right_.x * dt, right_.y * dt, right_.z * dt);
    }
    void move_up(double dt) {
        if (orbital_mode_) { rotate(0, dt * 90, 0); return; }
        move(0, dt, 0);  // world-up, as in graphics.py
    }
    void change_orbital_mode() {
        orbital_mode_ = !orbital_mode_;
        radius_ = std::hypot(pos_.x, pos_.y, pos_.z);
        update_proj_view();
    }
    void set_aspect(double a) { aspect_ = a; update_proj(); }
    void set_fov(double f)   { fovy_ = f;   update_proj(); }

    template<typename Tag = int>
    auto project_dots(const std::vector<std::tuple<double, double, double, Tag>>& dots,
                      double dx, double dy, double w, double h) const {
        return proj_view_.project(dots, dx, dy, w, h);
    }
    auto unproject(double x, double y,
                   double dx, double dy, double w, double h) const {
        return inv_proj_view_.unproject(proj_, x, y, dx, dy, w, h);
    }
};

} // namespace geom::render
