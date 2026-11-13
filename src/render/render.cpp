#include "render.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include <glad/glad.h>
#include <GL/osmesa.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


namespace geom::render {

std::atomic<float> tx_{0.0f};
std::atomic<float> ty_{0.0f};

struct DragState {
    bool dragging = false;
    int32_t last_x = 0;
    int32_t last_y = 0;
};
DragState drag_;


OSMesaContext ctx = nullptr;
unsigned char* osmesa_buffer = nullptr;

GLuint fbo = 0, color_tex = 0;
GLuint vao = 0, vbo = 0, ibo = 0;
GLuint program = 0;
GLint model_loc = -1;

std::vector<uint8_t> pixels(WIDTH * HEIGHT * 4);
std::vector<uint8_t> flipped(WIDTH * HEIGHT * 4);

const char* vertex_shader_source = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
void main() {
    gl_Position = uModel * vec4(aPos, 1.0);
}
)";

const char* fragment_shader_source = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(0.2, 0.6, 1.0, 1.0);
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

void setup_fbo() {
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color_tex, 0);
}

void setup_mesh() {
    float vertices[] = {
         0.0f,  0.5f, 0.0f,
         0.5f,  0.0f, 0.0f,
        -0.5f,  0.0f, 0.0f,
         0.0f, -0.5f, 0.0f
    };
    unsigned int indices[] = { 0, 1, 2, 2, 1, 3 };

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ibo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
}

void setup_program() {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    model_loc = glGetUniformLocation(program, "uModel");
}


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

    osmesa_buffer = new unsigned char[WIDTH * HEIGHT * 4];
    if (!OSMesaMakeCurrent(ctx, osmesa_buffer, GL_UNSIGNED_BYTE, WIDTH, HEIGHT)) {
        std::cerr << "OSMesaMakeCurrent failed" << std::endl;
        std::exit(1);
    }

    if (!gladLoadGLLoader((GLADloadproc) OSMesaGetProcAddress)) {
        std::cerr << "Failed to initialize glad with OSMesa" << std::endl;
        std::exit(1);
    }

    setup_fbo();
    setup_mesh();
    setup_program();
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

    tx_.store(0.0f);
    ty_.store(0.0f);
}


void handle_mouse(MouseEvent ev, int32_t x, int32_t y) {
    switch (ev) {
        case MouseEvent::Down:
            drag_.dragging = true;
            drag_.last_x = x;
            drag_.last_y = y;
            break;
        case MouseEvent::Move:
            if (drag_.dragging) {
                float dx = static_cast<float>(x - drag_.last_x);
                float dy = static_cast<float>(y - drag_.last_y);
                tx_.fetch_add(dx / (WIDTH/2.));
                ty_.fetch_add(-dy / (HEIGHT/2.)); // screen Y down -> NDC Y up
                drag_.last_x = x;
                drag_.last_y = y;
            }
            break;
        case MouseEvent::Up:
            drag_.dragging = false;
            break;
    }
}

const uint8_t* render_frame() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, WIDTH, HEIGHT);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program);
    glm::vec3 pos = glm::vec3(tx_.load(), ty_.load(), 0.0f);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
    glUniformMatrix4fv(model_loc, 1, GL_FALSE, glm::value_ptr(model));

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    for (int y = 0; y < HEIGHT; ++y) {
        std::memcpy(&flipped[y * WIDTH * 4],
                    &pixels[(HEIGHT - 1 - y) * WIDTH * 4],
                    WIDTH * 4);
    }
    return flipped.data();
}

} // namespace geom::render
