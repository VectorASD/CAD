#include "render.hpp"
#include "camera.hpp"
#include "matrix.hpp"

#include <glad/glad.h>
#include <GL/osmesa.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>


namespace geom::render {

namespace {

std::mutex camera_mutex;
Camera camera;

// Scancodes currently held (set on KeyDown, cleared on KeyUp).
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

GLuint fbo = 0, color_tex = 0;
GLuint vao = 0, vbo = 0, ibo = 0;
GLuint program = 0;
GLint proj_view_loc = -1;

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

GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, NULL, info_log);
        std::cerr << "Shader compilation failed: " << info_log << std::endl;
        std::exit(1);
    }
    return shader;
}

void setup_fbo(int w, int h) {
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color_tex, 0);

    GLuint depth_rb;
    glGenRenderbuffers(1, &depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_rb);
}

void setup_mesh() {
    float vertices[] = {
        // pos (x,y,z)  color (r,g,b)
        0,  1,  0,      1, 0, 0,   // 0 top    - red
        1,  0,  0,      0, 1, 0,   // 1 +X     - green
        0,  0,  1,      0, 0, 1,   // 2 +Z     - blue
       -1,  0,  0,      1, 1, 0,   // 3 -X     - yellow
        0,  0, -1,      1, 0, 1,   // 4 -Z     - magenta
        0, -1,  0,      0, 1, 1,   // 5 bottom - cyan
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

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ibo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // color
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

void setup_program() {
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   vertex_shader_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    proj_view_loc = glGetUniformLocation(program, "uProjView");
}

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
    glFrontFace(GL_CCW);   // default, but explicit

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    pixels.resize(g_width * g_height * 4);
    flipped.resize(g_width * g_height * 4);

    setup_fbo(g_width, g_height);
    setup_mesh();
    setup_program();

    camera.set_aspect(static_cast<double>(g_width) / g_height);
}

void shutdown() {
    if (fbo)       glDeleteFramebuffers(1, &fbo);
    if (color_tex) glDeleteTextures(1, &color_tex);
    if (vbo)       glDeleteBuffers(1, &vbo);
    if (ibo)       glDeleteBuffers(1, &ibo);
    if (vao)       glDeleteVertexArrays(1, &vao);
    if (program)   glDeleteProgram(program);

    if (ctx) OSMesaDestroyContext(ctx);
    delete[] osmesa_buffer;
    fbo = color_tex = vao = vbo = ibo = program = 0;
    ctx = nullptr;
    osmesa_buffer = nullptr;
    proj_view_loc = -1;
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
void apply_camera_input(double td) {
    // Space: toggle orbital / free (edge-triggered)
    if (key_state[' ']) {
        camera.change_orbital_mode();
        key_state[' '] = false;
    }

    // Accumulate desired axes
    double ax_fwd   = 0.0;
    double ax_right = 0.0;
    double ax_up    = 0.0;

    // WASD and arrows are equivalent: both map onto forward/strafe axes
    if (key_state['w'] || key_state[SC_UP])    ax_fwd   += 1.0;
    if (key_state['s'] || key_state[SC_DOWN])  ax_fwd   -= 1.0;
    if (key_state['d'] || key_state[SC_RIGHT]) ax_right += 1.0;
    if (key_state['a'] || key_state[SC_LEFT])  ax_right -= 1.0;
    if (key_state['e']) ax_up += 1.0;
    if (key_state['q']) ax_up -= 1.0;

    if (ax_fwd == 0.0 && ax_right == 0.0 && ax_up == 0.0) return;

    const Vec3<double> world_up{0.0, 1.0, 0.0};

    // Accumulate in world space.
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

    // Decompose into camera axes; each move_* keeps its own semantics
    // (in orbital mode: forward changes radius, right/up rotate)
    camera.move_forward(d.dot(camera.forward()));
    camera.move_right  (d.dot(camera.right()));
    camera.move_up     (d.dot(world_up));
}

const std::uint8_t* render_frame() {
    std::lock_guard<std::mutex> lock(camera_mutex);

    const auto now = std::chrono::steady_clock::now();
    double td = std::chrono::duration<double>(now - last_frame_tp).count();
    last_frame_tp = now;
    if (td > 0.1) td = 0.1; // clamp: a stall must not teleport the camera

    apply_camera_input(td);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, g_width, g_height);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(program);

    float pv_f[16];
    {
        const auto& pv = camera.proj_view();
        for (std::size_t i = 0; i < 16; ++i)
            pv_f[i] = static_cast<float>(pv.data()[i]);
    }
    glUniformMatrix4fv(proj_view_loc, 1, GL_TRUE, pv_f);  // GL_TRUE: Matrix is row-major

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 24, GL_UNSIGNED_INT, 0);

    glReadPixels(0, 0, g_width, g_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    for (int y = 0; y < g_height; ++y) {
        std::memcpy(&flipped[y * g_width * 4],
                    &pixels[(g_height - 1 - y) * g_width * 4],
                    g_width * 4);
    }
    return flipped.data();
}

} // namespace geom::render
