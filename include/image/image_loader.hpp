#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace geom {

struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // 4 bytes per pixel, top-down
};

// Load PNG/JPG (via stb_image) or WebP (via libwebp) into RGBA8.
// Returns an empty ImageData on failure.
ImageData load_image(const std::string& path);

} // namespace geom
