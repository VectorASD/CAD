/*
GLAD-generator:
https://glad.dav1d.de/#language=c&specification=gl&api=gl%3D3.3&api=gles1%3Dnone&api=gles2%3Dnone&api=glsc2%3Dnone&profile=core&loader=on
*/

#pragma once

#include <cstdint>


namespace geom::render {

constexpr int DEFAULT_WIDTH  = 1024;
constexpr int DEFAULT_HEIGHT = 768;

// render.cpp does not know about sockets.
enum class MouseEvent : uint8_t {
    Down,
    Move,
    Up,
};

enum class KeyEvent : std::uint8_t {
    Down = 0,
    Up   = 1,
};

constexpr std::uint16_t SC_LEFT      = 0xFF51;
constexpr std::uint16_t SC_UP        = 0xFF52;
constexpr std::uint16_t SC_RIGHT     = 0xFF53;
constexpr std::uint16_t SC_DOWN      = 0xFF54;
constexpr std::uint16_t SC_SHIFT_L   = 0xFFE1;
constexpr std::uint16_t SC_SHIFT_R   = 0xFFE2;
constexpr std::uint16_t SC_CTRL_L    = 0xFFE3;
constexpr std::uint16_t SC_CTRL_R    = 0xFFE4;
constexpr std::uint16_t SC_ALT_L     = 0xFFE9;
constexpr std::uint16_t SC_ALT_R     = 0xFFEA;
constexpr std::uint16_t SC_TAB       = 0xFF09;
constexpr std::uint16_t SC_ENTER     = 0xFF0D;
constexpr std::uint16_t SC_ESC       = 0xFF1B;
constexpr std::uint16_t SC_BACKSPACE = 0xFF08;


void init();
void shutdown();

void handle_mouse(MouseEvent ev, int32_t x, int32_t y);
void handle_key(KeyEvent ev, std::uint16_t scancode);
void handle_resize(int width, int height);

const uint8_t* render_frame();

int current_width();
int current_height();

} // namespace geom::render
