#include "render.hpp"
#include "camera.hpp"
#include "matrix.hpp"
#include "render_classes.hpp"

#include <glad/glad.h>
#include <GL/osmesa.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>


namespace geom::render {

namespace {

std::mutex camera_mutex;
Camera camera;

std::array<bool, 65536> key_state{};

constexpr std::uint16_t SC_LEFT    = 0xFF51;
constexpr std::uint16_t SC_UP      = 0xFF52;
constexpr std::uint16_t SC_RIGHT   = 0xFF53;
constexpr std::uint16_t SC_DOWN    = 0xFF54;
constexpr std::uint16_t SC_SHIFT_L = 0xFFE1;
constexpr std::uint16_t SC_SHIFT_R = 0xFFE2;

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

Fbo           g_fbo;
ShaderProgram g_program;
Mesh          g_mesh;
int           g_proj_view_loc = -1;

int g_width = WIDTH, g_height = HEIGHT;

std::vector<std::uint8_t> pixels(WIDTH * HEIGHT * 4);
std::vector<std::uint8_t> flipped(WIDTH * HEIGHT * 4);

const char* vertex_shader_source = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4 uProjView;

out vec3 vColor;

void main() {
    gl_Position = uProjView * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

const char* fragment_shader_source = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

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
    g_program = ShaderProgram(vertex_shader_source, fragment_shader_source);
    g_proj_view_loc = g_program.uniform_location("uProjView");

    float vertices[] = {
        // pos (x,y,z)  color (r,g,b)
        0,  1,  0,      1, 0, 0,
        1,  0,  0,      0, 1, 0,
        0,  0,  1,      0, 0, 1,
       -1,  0,  0,      1, 1, 0,
        0,  0, -1,      1, 0, 1,
        0, -1,  0,      0, 1, 1,
    };
    unsigned int indices[] = {
        0, 2, 1,
        0, 3, 2,
        0, 4, 3,
        0, 1, 4,
        5, 1, 2,
        5, 2, 3,
        5, 3, 4,
        5, 4, 1,
    };

    g_mesh.upload(vertices, 6, 6, indices, 24, {
        {0, 3, 0},   // position: location 0, 3 comps, offset 0
        {1, 3, 3},   // color:    location 1, 3 comps, offset 3 floats
    });

    camera.set_aspect(static_cast<double>(g_width) / g_height);
}

void shutdown() {
    // Destroy GL objects while the OSMesa context is still current.
    g_mesh.reset();
    g_program.reset();
    g_fbo.reset();

    if (ctx) OSMesaDestroyContext(ctx);
    delete[] osmesa_buffer;
    ctx = nullptr;
    osmesa_buffer = nullptr;
    g_proj_view_loc = -1;
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

namespace {

void apply_camera_input(double td) {
    if (key_state[' ']) {
        camera.change_orbital_mode();
        key_state[' '] = false;
    }

    double ax_fwd = 0.0, ax_right = 0.0, ax_up = 0.0;

    if (key_state['w'] || key_state[SC_UP])    ax_fwd   += 1.0;
    if (key_state['s'] || key_state[SC_DOWN])  ax_fwd   -= 1.0;
    if (key_state['d'] || key_state[SC_RIGHT]) ax_right += 1.0;
    if (key_state['a'] || key_state[SC_LEFT])  ax_right -= 1.0;
    if (key_state['e']) ax_up += 1.0;
    if (key_state['q']) ax_up -= 1.0;

    if (ax_fwd == 0.0 && ax_right == 0.0 && ax_up == 0.0) return;

    const Vec3<double> world_up{0.0, 1.0, 0.0};
    const Vec3<double> dir =
          camera.forward() * ax_fwd
        + camera.right()   * ax_right
        + world_up         * ax_up;

    const double len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (len < 1e-12) return;

    const bool   sprint = key_state[SC_SHIFT_L] || key_state[SC_SHIFT_R];
    const double speed  = MOVE_SPEED * (sprint ? SPRINT_MULT : 1.0);
    const double dist   = speed * td;
    const Vec3<double> d = dir * (dist / len);

    camera.move_forward(d.dot(camera.forward()));
    camera.move_right  (d.dot(camera.right()));
    camera.move_up     (d.dot(world_up));
}

} // anonymous namespace


const std::uint8_t* render_frame() {
    std::lock_guard<std::mutex> lock(camera_mutex);

    const auto now = std::chrono::steady_clock::now();
    double td = std::chrono::duration<double>(now - last_frame_tp).count();
    last_frame_tp = now;
    if (td > 0.1) td = 0.1;

    apply_camera_input(td);

    g_fbo.bind();
    glViewport(0, 0, g_fbo.width(), g_fbo.height());
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    g_program.use();

    float pv_f[16];
    {
        const auto& pv = camera.proj_view();
        for (std::size_t i = 0; i < 16; ++i)
            pv_f[i] = static_cast<float>(pv.data()[i]);
    }
    glUniformMatrix4fv(g_proj_view_loc, 1, GL_TRUE, pv_f);

    g_mesh.draw();

    glReadPixels(0, 0, g_width, g_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    for (int y = 0; y < g_height; ++y) {
        std::memcpy(&flipped[y * g_width * 4],
                    &pixels[(g_height - 1 - y) * g_width * 4],
                    g_width * 4);
    }
    return flipped.data();
}

} // namespace geom::render
