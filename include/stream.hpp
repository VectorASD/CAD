#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>


struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;


namespace geom::stream {

class H264Encoder {
public:
    H264Encoder(int width, int height, int bitrate);
    ~H264Encoder();

    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    // Encode one top-down RGBA frame. Calls `sink(data, size)` for every
    // produced packet. Returns false if `sink` failed or the codec errored
    // (remaining packets are still drained to keep the codec consistent).
    bool encode(const uint8_t* rgba, int64_t pts,
                const std::function<bool(const uint8_t*, size_t)>& sink);

private:
    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

} // namespace geom::stream
