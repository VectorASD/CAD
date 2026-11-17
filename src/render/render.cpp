#include "render.hpp"

// kernel
#include "bezier.hpp"
#include "bezier_surface.hpp"
#include "fraction.hpp"

// render
#include "bpt.hpp"
#include "camera.hpp"
#include "matrix.hpp"
#include "render_classes.hpp"

#include <glad/glad.h>
#include <GL/osmesa.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
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

Fbo      g_fbo;
Markers  g_markers;
Markers  g_markers2;
Surfaces g_surfaces;
Surfaces g_surfaces_teapot;
Texture  g_matcap;
SkyBox   g_skybox;
std::vector<CubemapTexture> g_skybox_textures;
std::atomic<int>            g_skybox_current{0};

int g_width = DEFAULT_WIDTH, g_height = DEFAULT_HEIGHT;

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

    g_fbo = Fbo(g_width, g_height);

    using S = Fraction<int>;
    using V = Vec3<S>;
    {
        std::vector<Marker> markers;

        // --- 33 points on a cubic Bezier curve (degree 3) in 3D ---
        const std::array<V, 4> curve_ctrl = {
            V(S(-1), S( 0), S(-1)),
            V(S(-1), S( 1), S( 1)),
            V(S( 1), S(-1), S(-1)),
            V(S( 1), S( 0), S( 1)),
        };
        for (int i = 0; i <= 32; ++i) {
            const S t(i, 32);
            const V p = bezier<3>(curve_ctrl, t);
            const double td = static_cast<double>(t);
            markers.push_back({
                .position = to_double(p),
                .tint     = {1.0 - td, td, 0.0},
                .scale    = 0.05,
            });
        }
        g_markers.init();
        g_markers.set(markers);
    }
    {
        std::vector<Marker> markers;

        // --- 4x4 control points of the Bezier surface ---
        BezierSurface<3, 3, S>::Grid grid = {{
            {{V(S(0), S(0), S(0)), V(S(1), S(0), S(0)), V(S(2), S(0), S(0)), V(S(3), S(0), S(0))}},
            {{V(S(0), S(0), S(1)), V(S(1), S(2), S(1)), V(S(2), S(2), S(1)), V(S(3), S(0), S(1))}},
            {{V(S(0), S(0), S(2)), V(S(1), S(2), S(2)), V(S(2), S(2), S(2)), V(S(3), S(0), S(2))}},
            {{V(S(0), S(0), S(3)), V(S(1), S(0), S(3)), V(S(2), S(0), S(3)), V(S(3), S(-2), S(3))}},
        }};
        for (const auto& row : grid) {
            for (const auto& p : row) {
                markers.push_back({
                    .position = to_double(p),
                    .tint     = {1.0, 1.0, 0.0},   // yellow
                    .scale    = 0.08,               // slightly larger than curve points
                });
            }
        }

        BezierSurface<3, 3, S> surface(grid);

        g_markers2.init();
        g_markers2.set(markers);
        g_surfaces.init();
        g_surfaces.upload(surface.tessellate(20, 20));
    }
    {
        auto model = geom::load_bpt("assets/models/teapotCGA.bpt", true);
        if (!model.patches.empty()) {
            g_surfaces_teapot.init();
            g_surfaces_teapot.upload(geom::tessellate_model(model, 8, 8));
        }
    }

    if (!g_matcap.load_from_file("assets/matcaps/17395A_7EBCC7_4D8B9F_65A1B5.png", GL_LINEAR)) {
        std::cerr << "matcap not loaded, using gray fallback" << std::endl;
        const std::uint8_t gray[4] = {180, 180, 180, 255};
        g_matcap.create(1, 1, GL_NEAREST, gray);
    }

    std::string SCRIPT_DIR = "/home/vectorasd/CAD/";
    {
        const int space_faces[] = { 4, 6, 3, 11, 5, 7 };
        CubemapTexture space;
        if (space.load_from_atlas(SCRIPT_DIR + "assets/skybox_space.webp", 4, 3, space_faces))
            g_skybox_textures.push_back(std::move(space));

        const int labeled_faces[] = { 0, 1, 2, 3, 4, 5 };
        CubemapTexture labeled;
        if (labeled.load_from_atlas(SCRIPT_DIR + "assets/skybox_labeled.png", 1, 6, labeled_faces))
            g_skybox_textures.push_back(std::move(labeled));
    }
    g_skybox.init();

    camera.set_aspect(static_cast<double>(g_width) / g_height);
}

void shutdown() {
    // Destroy GL objects while the OSMesa context is still current.
    g_skybox.reset();
    for (CubemapTexture& skybox : g_skybox_textures)
        skybox.reset();
    g_skybox_textures.clear();
    g_skybox_current.store(0);

    g_markers.reset();
    g_markers2.reset();
    g_surfaces.reset();
    g_surfaces_teapot.reset();
    g_matcap.reset();
    g_fbo.reset();

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
        const int n = static_cast<int>(g_skybox_textures.size());
        if (n) {
            int next = (g_skybox_current.load() + 1) % n;
            g_skybox_current.store(next);
        }
    }

    double ax_fwd = 0.0, ax_right = 0.0, ax_up = 0.0;

    if (key_state['w'] || key_state[SC_UP])    ax_fwd   += 1.0;
    if (key_state['s'] || key_state[SC_DOWN])  ax_fwd   -= 1.0;
    if (key_state['d'] || key_state[SC_RIGHT]) ax_right += 1.0;
    if (key_state['a'] || key_state[SC_LEFT])  ax_right -= 1.0;
    if (key_state[' ']) ax_up += 1.0;
    if (ctrl)           ax_up -= 1.0;

    if (ax_fwd == 0.0 && ax_right == 0.0 && ax_up == 0.0) return;

    const Vec3<double> world_up{0.0, 1.0, 0.0};
    const Vec3<double> dir =
          camera.forward() * ax_fwd
        + camera.right()   * ax_right
        + world_up         * ax_up;

    const double len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (len < 1e-12) return;

    const double speed  = MOVE_SPEED * (shift ? SPRINT_MULT : 1.0);
    const double dist   = speed * td;
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

        g_fbo = Fbo(g_width, g_height);
        pixels.resize(g_width * g_height * 4);
        flipped.resize(g_width * g_height * 4);
        camera.set_aspect(static_cast<double>(g_width) / g_height);
    }


    const auto now = std::chrono::steady_clock::now();
    double td = std::chrono::duration<double>(now - last_frame_tp).count();
    last_frame_tp = now;
    if (td > 0.1) td = 0.1;

    apply_camera_input(td);


    g_fbo.bind();
    glViewport(0, 0, g_fbo.width(), g_fbo.height());
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Opaque geometry first
    static const Matrix<double> I = Matrix<double>::identity(4);
    static const Matrix<double> surf_mat = Matrix<double>::translation(1.5, 0, -1.5);
    static const Matrix<double> teapot_mat =
        Matrix<double>::rotation_x(-90) * Matrix<double>::translation(8, 0, 0);

    g_markers.draw(camera.proj_view(), I);
    g_markers2.draw(camera.proj_view(), surf_mat);
    g_surfaces.draw(camera.proj_view(), camera.view(), surf_mat, g_matcap);
    g_surfaces_teapot.draw(camera.proj_view(), camera.view(), teapot_mat, g_matcap);

    // Skybox last: GL_LEQUAL + z=w means it fills only untouched pixels
    const int skybox_idx = g_skybox_current.load();
    if (skybox_idx >= 0 && skybox_idx < static_cast<int>(g_skybox_textures.size())) {
        Matrix<double> view_rot = camera.view();
        view_rot(0, 3) = 0.0;
        view_rot(1, 3) = 0.0;
        view_rot(2, 3) = 0.0;
        const Matrix<double> proj_view_rot = camera.proj() * view_rot;
        g_skybox.draw(proj_view_rot, g_skybox_textures[skybox_idx]);
    }

    glReadPixels(0, 0, g_width, g_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());


    for (int y = 0; y < g_height; ++y) {
        std::memcpy(&flipped[y * g_width * 4],
                    &pixels[(g_height - 1 - y) * g_width * 4],
                    g_width * 4);
    }
    return flipped.data();
}

int current_width()  { return g_width; }
int current_height() { return g_height; }

} // namespace geom::render
