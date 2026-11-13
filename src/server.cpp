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

// Wire format: [u8 type][i32 x_be][i32 y_be]

constexpr size_t EVENT_SIZE = 9;

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

void control_thread(int sock, std::atomic<bool>* client_connected) {
    uint8_t buf[EVENT_SIZE];
    while (*client_connected) {
        ssize_t bytes = recv(sock, buf, EVENT_SIZE, MSG_WAITALL);
        if (bytes <= 0) {
            *client_connected = false;
            break;
        }

        geom::render::MouseEvent ev;
        switch (buf[0]) {
            case 0: ev = geom::render::MouseEvent::Down; break;
            case 1: ev = geom::render::MouseEvent::Move; break;
            case 2: ev = geom::render::MouseEvent::Up;   break;
            default: continue;
        }

        int32_t xb, yb;
        std::memcpy(&xb, buf + 1, 4);
        std::memcpy(&yb, buf + 5, 4);
        geom::render::handle_mouse(ev, ntohl(xb), ntohl(yb));
    }
}

void run() {
    signal(SIGPIPE, SIG_IGN);
    geom::render::init();

    int video_server = create_server_socket(VIDEO_PORT);
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

        geom::stream::H264Encoder encoder(geom::render::WIDTH,
                                          geom::render::HEIGHT,
                                          2'000'000);

        std::atomic<bool> client_connected(true);
        std::thread ctrl(control_thread, control_socket, &client_connected);

        int64_t frame_count = 0;
        while (running && client_connected) {
            const uint8_t* pixels = geom::render::render_frame();

            bool ok = encoder.encode(pixels, frame_count++,
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
