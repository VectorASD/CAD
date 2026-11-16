#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "render.hpp"
#include "stream.hpp"


namespace server {

constexpr int VIDEO_PORT = 12345;
constexpr int CONTROL_PORT = 12346;

std::atomic<bool> running(true);


int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        std::exit(1);
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed on port " << port << std::endl;
        std::exit(1);
    }
    listen(fd, 3);
    return fd;
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

void control_thread(int control_socket, std::atomic<bool>* client_connected) {
    while (*client_connected) {
        std::uint8_t type;
        if (recv(control_socket, &type, 1, MSG_WAITALL) != 1) {
            *client_connected = false;
            break;
        }

        switch (type) {
        case 0 ... 2: {
            // Mouse: [i32 x_be][i32 y_be]
            std::uint8_t buf[8];
            if (recv(control_socket, buf, 8, MSG_WAITALL) != 8) {
                *client_connected = false;
                break;
            }
            std::int32_t xb, yb;
            std::memcpy(&xb, buf,     4);
            std::memcpy(&yb, buf + 4, 4);
            geom::render::handle_mouse(
                static_cast<geom::render::MouseEvent>(type),
                ntohl(xb), ntohl(yb));
            break; }

        case 3 ... 4: {
            // Keyboard: [u16 scancode_be]
            std::uint8_t buf[2];
            if (recv(control_socket, buf, 2, MSG_WAITALL) != 2) {
                *client_connected = false;
                break;
            }
            std::uint16_t sb;
            std::memcpy(&sb, buf, 2);
            geom::render::handle_key(
                (type == 3) ? geom::render::KeyEvent::Down
                            : geom::render::KeyEvent::Up,
                ntohs(sb));
            break; }

        case 5: {
            // Resize: [u16 width_be][u16 height_be]
            std::uint8_t buf[4];
            if (recv(control_socket, buf, 4, MSG_WAITALL) != 4) {
                *client_connected = false;
                break;
            }
            std::uint16_t wb, hb;
            std::memcpy(&wb, buf,     2);
            std::memcpy(&hb, buf + 2, 2);
            geom::render::handle_resize(ntohs(wb), ntohs(hb));
            break; }

        default:
            std::cerr << "Unknown event type: " << static_cast<int>(type) << std::endl;
            *client_connected = false;
            break;
        }
    }
}

void run() {
    signal(SIGPIPE, SIG_IGN);
    geom::render::init();

    int video_server   = create_server_socket(VIDEO_PORT);
    int control_server = create_server_socket(CONTROL_PORT);

    while (running) {
        std::cout << "Waiting for clients on " << VIDEO_PORT
                  << " (video) and " << CONTROL_PORT << " (control)..."
                  << std::endl;

        int video_socket = accept(video_server, NULL, NULL);
        if (video_socket < 0) continue;

        int control_socket = accept(control_server, NULL, NULL);
        if (control_socket < 0) {
            close(video_socket);
            continue;
        }

        std::cout << "Client connected." << std::endl;
        geom::render::handle_resize(geom::render::DEFAULT_WIDTH,
                                    geom::render::DEFAULT_HEIGHT);

        // Render one frame so the pending resize is applied before the
        // encoder is sized -- otherwise the first frame would hand the
        // encoder a buffer of the wrong size.
        geom::render::render_frame();

        int enc_w = geom::render::current_width();
        int enc_h = geom::render::current_height();
        auto encoder = std::make_unique<geom::stream::H264Encoder>(
            enc_w, enc_h, 5'000'000);

        std::atomic<bool> client_connected(true);
        std::thread ctrl(control_thread, control_socket, &client_connected);

        int64_t frame_count = 0;
        while (running && client_connected) {
            const uint8_t* pixels = geom::render::render_frame();

            const int w = geom::render::current_width();
            const int h = geom::render::current_height();
            if (w != enc_w || h != enc_h) {
                // Client resized. Old encoder's SPS/PPS are for the old
                // resolution; drop it and start a fresh stream. The first
                // packet of the new encoder carries its own SPS/PPS, which
                // the client's decoder re-reads.
                enc_w = w;
                enc_h = h;
                encoder = std::make_unique<geom::stream::H264Encoder>(
                    enc_w, enc_h, 2'000'000);
            }

            bool ok = encoder->encode(pixels, frame_count++,
                [&](const uint8_t* data, size_t size) {
                    uint32_t net_size = htonl(static_cast<uint32_t>(size));
                    return send_all(video_socket, &net_size, sizeof(net_size))
                        && send_all(video_socket, data, size);
                });

            if (!ok) {
                std::cout << "Video send failed, disconnecting." << std::endl;
                client_connected = false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }

        std::cout << "Client disconnected." << std::endl;

        shutdown(control_socket, SHUT_RDWR);
        ctrl.join();
        close(control_socket);
        close(video_socket);
    }

    close(video_server);
    close(control_server);
    geom::render::shutdown();
}

} // namespace server


int main() {
    server::run();
    return 0;
}
