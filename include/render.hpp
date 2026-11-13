/*
GLAD-generator:
https://glad.dav1d.de/#language=c&specification=gl&api=gl%3D3.3&api=gles1%3Dnone&api=gles2%3Dnone&api=glsc2%3Dnone&profile=core&loader=on
*/

#pragma once

#include <cstdint>


namespace geom::render {

constexpr int WIDTH = 1024;
constexpr int HEIGHT = 768;

// Mouse event as seen by the render layer. Wire types map to this enum
// in server.cpp; render.cpp does not know about sockets.
enum class MouseEvent : uint8_t {
    Down,
    Move,
    Up,
};


void init();
void shutdown();

void handle_mouse(MouseEvent ev, int32_t x, int32_t y);
const uint8_t* render_frame();

} // namespace geom::render
