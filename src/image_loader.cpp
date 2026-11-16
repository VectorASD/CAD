#include "image/image_loader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "image/stb_image.h"

#define WEBPDEC_IMPLEMENTATION
#include "image/webpdec.h"

#include <cctype>
#include <cstdio>
#include <cstring>


namespace geom {

namespace {

ImageData load_stbi(const std::string& path) {
    int w = 0, h = 0, ch = 0;
    std::uint8_t* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!px)
        return {};

    ImageData out;
    out.width = w;
    out.height = h;
    out.rgba.assign(px, px + w * h * 4);
    stbi_image_free(px);
    return out;
}

ImageData load_webp(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return {};

    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);

    int w = 0, h = 0;
    std::uint8_t* px = WebPDecodeRGBA(bytes.data(), bytes.size(), &w, &h);
    if (!px)
        return {};

    ImageData out;
    out.width = w;
    out.height = h;
    out.rgba.assign(px, px + w * h * 4);
    WebPFree(px);
    return out;
}

} // anonymous namespace


ImageData load_image(const std::string& path) {
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) return {};

    std::string ext = path.substr(dot + 1);
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == "webp")
        return load_webp(path);
    return load_stbi(path); // png, bmp, gif, psd, jpeg, pnm, hdr, tga (see: stbi__load_main; STBI_NO_* gates each)
}

} // namespace geom
