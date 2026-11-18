#include "render.hpp"

// kernel
#include "bezier.hpp"
#include "bezier_surface.hpp"
#include "fraction.hpp"

// render
#include "camera.hpp"
#include "matrix.hpp"
#include "render_classes.hpp"
#include "render_hierarchy.hpp"

// files
#include "bpt.hpp"
#include "step/token_stream.hpp"

#include <glad/glad.h>
#include <GL/osmesa.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>


namespace geom::render {

namespace {

std::mutex camera_mutex;
Camera camera;

std::array<bool, 65536> key_state{};

constexpr double MOVE_SPEED  = 3.0; // world units per second
constexpr double SPRINT_MULT = 3.0;
std::chrono::steady_clock::time_point last_frame_tp =
    std::chrono::steady_clock::now();

struct DragState {
    bool dragging = false;
    std::int32_t last_x = 0;
    std::int32_t last_y = 0;
};
DragState drag_;

OSMesaContext ctx = nullptr;
unsigned char* osmesa_buffer = nullptr;

std::unique_ptr<Node> g_scene;
FboNode*              g_root_fbo = nullptr;
std::atomic<bool>     g_wireframe{false};
std::atomic<bool>     g_teapot_rotor{true};

int g_width  = DEFAULT_WIDTH;
int g_height = DEFAULT_HEIGHT;

std::vector<std::uint8_t> pixels(DEFAULT_WIDTH * DEFAULT_HEIGHT * 4);
std::vector<std::uint8_t> flipped(DEFAULT_WIDTH * DEFAULT_HEIGHT * 4);

std::atomic<int> g_pending_width{0};
std::atomic<int> g_pending_height{0};
constexpr int MIN_DIM = 64;
constexpr int MAX_DIM = 4096;

} // anonymous namespace


void init() {
    const int attribs[] = {
        OSMESA_FORMAT, OSMESA_RGBA,
        OSMESA_PROFILE, OSMESA_CORE_PROFILE,
        OSMESA_DEPTH_BITS, 24,
        0
    };
    ctx = OSMesaCreateContextAttribs(attribs, nullptr);
    if (!ctx) {
        std::cerr << "OSMesaCreateContextAttribs failed" << std::endl;
        std::exit(1);
    }

    osmesa_buffer = new unsigned char[g_width * g_height * 4];
    if (!OSMesaMakeCurrent(ctx, osmesa_buffer, GL_UNSIGNED_BYTE, g_width, g_height)) {
        std::cerr << "OSMesaMakeCurrent failed" << std::endl;
        std::exit(1);
    }

    if (!gladLoadGLLoader((GLADloadproc) OSMesaGetProcAddress)) {
        std::cerr << "Failed to initialize glad with OSMesa" << std::endl;
        std::exit(1);
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    pixels.resize(g_width * g_height * 4);
    flipped.resize(g_width * g_height * 4);

    init_scene();
}

void init_scene() {
    auto root = std::make_unique<FboNode>(g_width, g_height);
    g_root_fbo = root.get();

    // Matcap goes first so SurfacesNode sees a valid Texture* by the
    // time its do_draw runs (init happens in insertion order).
    auto matcap_node = std::make_unique<TextureNode>(
        "assets/matcaps/17395A_7EBCC7_4D8B9F_65A1B5.png", GL_LINEAR);
    const Texture* matcap_ptr = &matcap_node->get();
    root->add(std::move(matcap_node));

    using S = Fraction<int>;
    using V = Vec3<S>;

    // 1. Curve markers, identity transform
    {
        const std::array<V, 4> ctrl = {
            V(S(-1), S( 0), S(-1)),
            V(S(-1), S( 1), S( 1)),
            V(S( 1), S(-1), S(-1)),
            V(S( 1), S( 0), S( 1)),
        };
        std::vector<Marker> pts;
        for (int i = 0; i <= 32; ++i) {
            const S t(i, 32);
            const V p = bezier<3>(ctrl, t);
            const double td = static_cast<double>(t);
            pts.push_back({ .position = to_double(p), .tint = {1.0 - td, td, 0.0}, .scale = 0.05 });
        }
        auto m = std::make_unique<Markers>();
        m->set(pts);
        root->add(std::make_unique<MarkersNode>(std::move(m)));
    }

    // 2. Sample Bezier surface + its control markers, both under surf_mat
    static const Matrix<double> surf_mat =
        Matrix<double>::translation(1.5, 0, -1.5);
    {
        BezierSurface<3, 3, S>::Grid grid = {{
            {{V(S(0), S(0), S(0)), V(S(1), S(0), S(0)), V(S(2), S(0), S(0)), V(S(3), S(0), S(0))}},
            {{V(S(0), S(0), S(1)), V(S(1), S(2), S(1)), V(S(2), S(2), S(1)), V(S(3), S(0), S(1))}},
            {{V(S(0), S(0), S(2)), V(S(1), S(2), S(2)), V(S(2), S(2), S(2)), V(S(3), S(0), S(2))}},
            {{V(S(0), S(0), S(3)), V(S(1), S(0), S(3)), V(S(2), S(0), S(3)), V(S(3), S(-2), S(3))}},
        }};

        std::vector<Marker> pts;
        for (const auto& row : grid)
            for (const auto& p : row)
                pts.push_back({ .position = to_double(p), .tint = {1.0, 1.0, 0.0}, .scale = 0.08 });

        auto markers_t = std::make_unique<TransformNode>();
        markers_t->set_local(surf_mat);
        auto m = std::make_unique<Markers>();
        m->set(pts);
        markers_t->add(std::make_unique<MarkersNode>(std::move(m)));
        root->add(std::move(markers_t));

        BezierSurface<3, 3, S> surface(grid);
        auto s = std::make_unique<SurfacesNode>(
            std::make_unique<Surfaces>(),
            std::make_unique<geom::RenderMesh>(surface.tessellate(20, 20)));
        s->set_matcap(matcap_ptr);

        auto surf_t = std::make_unique<TransformNode>();
        surf_t->set_local(surf_mat);
        surf_t->add(std::move(s));
        root->add(std::move(surf_t));
    }

    // 3. Teapot with separated transforms and tag
    {
        Model model = geom::load_bpt("assets/models/teapotCGA.bpt", true);
        if (!model.patches.empty()) {
            auto surf = std::make_unique<SurfacesNode>(
                std::make_unique<Surfaces>(),
                std::make_unique<geom::RenderMesh>(
                    geom::tessellate_model(model, 8, 8)));
            surf->set_matcap(matcap_ptr);

            // 1. Fix initial orientation (bottom from -Z to -Y)
            auto fix_x_node = std::make_unique<TransformNode>();
            fix_x_node->set_local(Matrix<double>::rotation_x(-90));
            fix_x_node->add(std::move(surf));

            // 2. Spin in place (this node gets the tag for animation)
            auto spin_y_node = std::make_unique<TransformNode>();
            spin_y_node->set_local(Matrix<double>::rotation_y(0));
            spin_y_node->set_tag("teapot_spin");
            spin_y_node->add(std::move(fix_x_node));

            // 3. Move to final world position
            auto translate_node = std::make_unique<TransformNode>();
            translate_node->set_local(Matrix<double>::translation(8, 0, 0));
            translate_node->add(std::move(spin_y_node));

            root->add(std::move(translate_node));
        }
    }

    // 4. Skybox (last: LEQUAL trick needs untouched depth buffer)
    {
        auto sb = std::make_unique<SkyBoxNode>(std::make_unique<SkyBox>());
        sb->add_atlas("assets/skybox_space.webp",  4, 3, {4, 6, 3, 11, 5, 7});
        sb->add_atlas("assets/skybox_labeled.png", 1, 6, {0, 1, 2, 3, 4, 5});
        root->add(std::move(sb));
    }

    step::step_dump("assets/models/DSA 1.5u.step");

    g_scene = std::move(root);
    g_scene->recalc(Matrix<double>::identity(4)); // initial flush before first draw

    camera.set_aspect(static_cast<double>(g_width) / g_height);
}

void shutdown() {
    g_scene.reset(); // recursive reset + destructors; still in GL context
    g_root_fbo = nullptr;

    if (ctx)
        OSMesaDestroyContext(ctx);
    delete[] osmesa_buffer;
    ctx = nullptr;
    osmesa_buffer = nullptr;
}


void handle_mouse(MouseEvent ev, std::int32_t x, std::int32_t y) {
    std::lock_guard<std::mutex> lock(camera_mutex);

    switch (ev) {
        case MouseEvent::Down:
            drag_.dragging = true;
            drag_.last_x = x;
            drag_.last_y = y;
            break;

        case MouseEvent::Move:
            if (drag_.dragging) {
                double dx = x - drag_.last_x;
                double dy = y - drag_.last_y;
                drag_.last_x = x;
                drag_.last_y = y;

                constexpr double SENSITIVITY = 0.4;
                camera.rotate(dx * SENSITIVITY, dy * SENSITIVITY, 0.0);
            }
            break;

        case MouseEvent::Up:
            drag_.dragging = false;
            break;
    }
}

void handle_key(KeyEvent ev, std::uint16_t scancode) {
    std::lock_guard<std::mutex> lock(camera_mutex);
    key_state[scancode] = (ev == KeyEvent::Down);
}

void handle_resize(int width, int height) {
    // H.264 4:2:0 encoder rejects odd dimensions, so snap to even here.
    // Rounding down loses at most 1 pixel; the client is expected to
    // round its own window size to match, but do not trust that.
    width  &= ~1;
    height &= ~1;

    if (width  < MIN_DIM || width  > MAX_DIM) return;
    if (height < MIN_DIM || height > MAX_DIM) return;
    g_pending_width.store(width);
    g_pending_height.store(height);
}

namespace {

void apply_camera_input(double td) {
    const bool shift = key_state[SC_SHIFT_L] || key_state[SC_SHIFT_R];
    const bool ctrl  = key_state[SC_CTRL_L]  || key_state[SC_CTRL_R];
    // const bool alt   = key_state[SC_ALT_L]   || key_state[SC_ALT_R];

    if (key_state['e']) {
        camera.change_orbital_mode();
        key_state['e'] = false;
    }
    if (key_state['q']) {
        key_state['q'] = false;
        if (g_scene)
            g_scene->visit<SkyBoxNode>([](SkyBoxNode& sb) { sb.cycle(); });
    }

    if (key_state['z']) {
        key_state['z'] = false;
        g_wireframe = !g_wireframe;
    }
    if (key_state['x']) {
        key_state['x'] = false;
        g_teapot_rotor = !g_teapot_rotor;
    }

    double ax_fwd = 0.0, ax_right = 0.0, ax_up = 0.0;

    if (key_state['w'] || key_state[SC_UP])    ax_fwd   += 1.0;
    if (key_state['s'] || key_state[SC_DOWN])  ax_fwd   -= 1.0;
    if (key_state['d'] || key_state[SC_RIGHT]) ax_right += 1.0;
    if (key_state['a'] || key_state[SC_LEFT])  ax_right -= 1.0;
    if (key_state[' ']) ax_up += 1.0;
    if (ctrl)           ax_up -= 1.0;

    if (ax_fwd == 0.0 && ax_right == 0.0 && ax_up == 0.0)
        return;

    const Vec3<double> world_up{0.0, 1.0, 0.0};
    const Vec3<double> dir =
          camera.forward() * ax_fwd
        + camera.right()   * ax_right
        + world_up         * ax_up;

    const double len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (len < 1e-12)
        return;

    const double speed = MOVE_SPEED * (shift ? SPRINT_MULT : 1.0);
    const double dist  = speed * td;

    const Vec3<double> d = dir * (dist / len);

    camera.move_forward(d.dot(camera.forward()));
    camera.move_right  (d.dot(camera.right()));
    camera.move_up     (d.dot(world_up));
}

} // anonymous namespace


const std::uint8_t* render_frame() {
    std::lock_guard<std::mutex> lock(camera_mutex);

    const int pw = g_pending_width.exchange(0);
    const int ph = g_pending_height.exchange(0);
    if (pw > 0 && ph > 0 && (pw != g_width || ph != g_height)) {
        g_width  = pw;
        g_height = ph;
        if (g_root_fbo) g_root_fbo->resize(g_width, g_height);
        pixels.resize(g_width * g_height * 4);
        flipped.resize(g_width * g_height * 4);
        camera.set_aspect(static_cast<double>(g_width) / g_height);
    }

    const auto now = std::chrono::steady_clock::now();
    double td = std::chrono::duration<double>(now - last_frame_tp).count();
    last_frame_tp = now;
    if (td > 0.1) td = 0.1;

    // Animate tagged transform nodes
    if (g_teapot_rotor) {
        static double teapot_angle = 0.0;
        teapot_angle = std::fmod(teapot_angle + td * -30.0, 360.0);
        if (g_scene)
            if (auto* teapot_spin = g_scene->find_by_tag<TransformNode>("teapot_spin"))
                teapot_spin->set_local(Matrix<double>::rotation_y(teapot_angle));
    }

    apply_camera_input(td);

    if (g_scene) {
        RenderContext rctx { camera.proj(), camera.view(), camera.proj_view(), g_wireframe };
        g_scene->draw(rctx);
    }
    if (g_root_fbo)
        g_root_fbo->read_pixels(pixels.data());

    for (int y = 0; y < g_height; ++y)
        std::memcpy(&flipped[y * g_width * 4],
                    &pixels[(g_height - 1 - y) * g_width * 4],
                    g_width * 4);
    return flipped.data();
}

int current_width()  { return g_width; }
int current_height() { return g_height; }

} // namespace geom::render
