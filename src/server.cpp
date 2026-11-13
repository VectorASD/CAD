/*
https://glad.dav1d.de/#language=c&specification=gl&api=gl%3D3.3&api=gles1%3Dnone&api=gles2%3Dnone&api=glsc2%3Dnone&profile=core&loader=on
*/

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <glad/glad.h>
#include <GL/osmesa.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/opt.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}
namespace render {

constexpr int WIDTH = 800;
constexpr int HEIGHT = 600;
constexpr int VIDEO_PORT = 12345;
constexpr int CONTROL_PORT = 12346;

std::atomic<float> rotation_angle(0.0f);
std::atomic<bool> running(true);

// --- Socket Helpers ---
int create_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        exit(1);
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed on port " << port << std::endl;
        exit(1);
    }
    listen(server_fd, 3);
    return server_fd;
}

bool send_all(int sock, const void* data, size_t size) {
    const char* ptr = static_cast<const char*>(data);
    while (size > 0) {
        ssize_t sent = send(sock, ptr, size, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        ptr += sent;
        size -= sent;
    }
    return true;
}

// --- Mouse Control Thread ---
void control_thread(int control_socket, std::atomic<bool>* client_connected) {
    float mouse_data[2];
    while (*client_connected) {
        ssize_t bytes = recv(control_socket, mouse_data, sizeof(mouse_data), MSG_WAITALL);
        if (bytes <= 0) {
            *client_connected = false;
            break;
        }
        rotation_angle += mouse_data[0] * 0.05f;
    }
}

// --- Shaders ---
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
    }
    return shader;
}

// --- FFmpeg Encoder ---
struct EncoderContext {
    AVCodecContext* codec_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* sws_ctx = nullptr;

    EncoderContext() = default;
    EncoderContext(const EncoderContext&) = delete;
    EncoderContext& operator=(const EncoderContext&) = delete;
    EncoderContext(EncoderContext&&) = default;
    EncoderContext& operator=(EncoderContext&&) = default;
};

EncoderContext init_encoder() {
    EncoderContext enc;
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "H.264 encoder not found" << std::endl;
        exit(1);
    }

    enc.codec_ctx = avcodec_alloc_context3(codec);
    enc.codec_ctx->width = WIDTH;
    enc.codec_ctx->height = HEIGHT;
    enc.codec_ctx->time_base = {1, 30};
    enc.codec_ctx->framerate = {30, 1};
    enc.codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    enc.codec_ctx->bit_rate = 2000000;

    if (av_opt_set(enc.codec_ctx->priv_data, "preset", "ultrafast", 0) < 0) {
        std::cerr << "Failed to set preset=ultrafast" << std::endl;
        exit(1);
    }
    if (av_opt_set(enc.codec_ctx->priv_data, "tune", "zerolatency", 0) < 0) {
        std::cerr << "Failed to set tune=zerolatency" << std::endl;
        exit(1);
    }

    if (avcodec_open2(enc.codec_ctx, codec, NULL) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        exit(1);
    }

    enc.frame = av_frame_alloc();
    enc.frame->format = enc.codec_ctx->pix_fmt;
    enc.frame->width = WIDTH;
    enc.frame->height = HEIGHT;
    av_frame_get_buffer(enc.frame, 32);

    enc.packet = av_packet_alloc();
    enc.sws_ctx = sws_getContext(WIDTH, HEIGHT, AV_PIX_FMT_RGBA,
                                 WIDTH, HEIGHT, AV_PIX_FMT_YUV420P,
                                 SWS_BILINEAR, NULL, NULL, NULL);
    return enc;
}

void run_server() {
    // 1. OSMesa Context Init (Core Profile)
    const int attribs[] = {
        OSMESA_FORMAT, OSMESA_RGBA,
        OSMESA_PROFILE, OSMESA_CORE_PROFILE, // Request Core Profile
        OSMESA_DEPTH_BITS, 24,
        0
    };
    OSMesaContext ctx = OSMesaCreateContextAttribs(attribs, NULL);
    if (!ctx) {
        std::cerr << "OSMesaCreateContextAttribs failed" << std::endl;
        exit(1);
    }

    // Allocate buffer for OSMesa to render into
    unsigned char* osmesa_buffer = new unsigned char[WIDTH * HEIGHT * 4];
    if (!OSMesaMakeCurrent(ctx, osmesa_buffer, GL_UNSIGNED_BYTE, WIDTH, HEIGHT)) {
        std::cerr << "OSMesaMakeCurrent failed" << std::endl;
        exit(1);
    }

    // 2. GLAD Init
    if (!gladLoadGLLoader((GLADloadproc) OSMesaGetProcAddress)) {
        std::cerr << "Failed to initialize glad with OSMesa" << std::endl;
        exit(1);
    }

    // 3. OpenGL FBO
    GLuint fbo, color_tex;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);

    // 4. OpenGL VBO/IBO (Diamond shape)
    float vertices[] = {
         0.0f,  0.5f, 0.0f, // Top
         0.5f,  0.0f, 0.0f, // Right
        -0.5f,  0.0f, 0.0f, // Left
         0.0f, -0.5f, 0.0f  // Bottom
    };
    unsigned int indices[] = {
        0, 1, 2, // First triangle
        2, 1, 3  // Second triangle
    };

    GLuint vao, vbo, ibo;
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

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint model_loc = glGetUniformLocation(program, "uModel");

    // 5. Buffers
    std::vector<uint8_t> pixels(WIDTH * HEIGHT * 4);
    std::vector<uint8_t> flipped(WIDTH * HEIGHT * 4);

    // Ignore SIGPIPE to prevent server crash when client disconnects
    signal(SIGPIPE, SIG_IGN);

    // 6. Sockets
    int video_server = create_server_socket(VIDEO_PORT);
    int control_server = create_server_socket(CONTROL_PORT);

    while (running) {
        std::cout << "Waiting for Python client to connect on video port " << VIDEO_PORT
                  << " and control port " << CONTROL_PORT << "..." << std::endl;

        int video_socket = accept(video_server, NULL, NULL);
        if (video_socket < 0) continue;

        int control_socket = accept(control_server, NULL, NULL);
        if (control_socket < 0) {
            close(video_socket);
            continue;
        }

        std::cout << "Client connected!" << std::endl;

        // --- CREATE A FRESH ENCODER FOR THIS CLIENT ---
        EncoderContext enc = init_encoder();

        std::atomic<bool> client_connected(true);
        std::thread ctrl_thread(control_thread, control_socket, &client_connected);

        // 7. Main Loop
        glUseProgram(program);
        int frame_count = 0;

        while (running && client_connected) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glViewport(0, 0, WIDTH, HEIGHT);
            glClearColor(0., 0., 0., 1.);
            glClear(GL_COLOR_BUFFER_BIT);

            glm::mat4 model = glm::rotate(glm::mat4(1.0f), rotation_angle.load(), glm::vec3(0.0f, 0.0f, 1.0f));
            glUniformMatrix4fv(model_loc, 1, GL_FALSE, glm::value_ptr(model));

            glBindVertexArray(vao);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

            for (int y = 0; y < HEIGHT; ++y) {
                std::memcpy(&flipped[y * WIDTH * 4], &pixels[(HEIGHT - 1 - y) * WIDTH * 4], WIDTH * 4);
            }

            av_frame_make_writable(enc.frame);
            const uint8_t* src_data[1] = { flipped.data() };
            int src_linesize[1] = { WIDTH * 4 };
            sws_scale(enc.sws_ctx, src_data, src_linesize, 0, HEIGHT, enc.frame->data, enc.frame->linesize);

            enc.frame->pts = frame_count++;
            int ret = avcodec_send_frame(enc.codec_ctx, enc.frame);
            if (ret < 0) {
                std::cerr << "avcodec_send_frame failed: " << ret << std::endl;
                client_connected = false;
                break;
            }

            bool ok = true;
            while (avcodec_receive_packet(enc.codec_ctx, enc.packet) == 0) {
                uint32_t net_size = htonl(enc.packet->size);
                ok = ok && send_all(video_socket, &net_size, sizeof(net_size));
                ok = ok && send_all(video_socket, enc.packet->data, enc.packet->size);
                av_packet_unref(enc.packet);
            }

            if (!ok) {
                std::cout << "Video send failed, disconnecting client." << std::endl;
                client_connected = false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }

        std::cout << "Client disconnected. Waiting for new connection..." << std::endl;

        // --- CLEANUP ENCODER FOR THIS CLIENT ---
        av_frame_free(&enc.frame);
        av_packet_free(&enc.packet);
        avcodec_free_context(&enc.codec_ctx);
        sws_freeContext(enc.sws_ctx);

        shutdown(control_socket, SHUT_RDWR);
        ctrl_thread.join();
        close(control_socket);
        close(video_socket);
    }

    running = false;

    // Cleanup (only sockets and OSMesa now)
    close(video_server);
    close(control_server);

    OSMesaDestroyContext(ctx);
    delete[] osmesa_buffer;
}

} // namespace render

int main() {
    render::run_server();
    return 0;
}
