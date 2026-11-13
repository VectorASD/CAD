#include "stream.hpp"

#include <cstdlib>
#include <iostream>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/opt.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}


namespace geom::stream {

H264Encoder::H264Encoder(int width, int height, int bitrate)
    : width_(width), height_(height) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "H.264 encoder not found" << std::endl;
        std::exit(1);
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "avcodec_alloc_context3 failed" << std::endl;
        std::exit(1);
    }
    codec_ctx_->width = width;
    codec_ctx_->height = height;
    codec_ctx_->time_base = {1, 30};
    codec_ctx_->framerate = {30, 1};
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx_->bit_rate = bitrate;

    if (av_opt_set(codec_ctx_->priv_data, "preset", "ultrafast", 0) < 0) {
        std::cerr << "Failed to set preset=ultrafast" << std::endl;
        std::exit(1);
    }
    if (av_opt_set(codec_ctx_->priv_data, "tune", "zerolatency", 0) < 0) {
        std::cerr << "Failed to set tune=zerolatency" << std::endl;
        std::exit(1);
    }
    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        std::exit(1);
    }

    frame_ = av_frame_alloc();
    if (!frame_) {
        std::cerr << "av_frame_alloc failed" << std::endl;
        std::exit(1);
    }
    frame_->format = codec_ctx_->pix_fmt;
    frame_->width = width;
    frame_->height = height;
    if (av_frame_get_buffer(frame_, 32) < 0) {
        std::cerr << "av_frame_get_buffer failed" << std::endl;
        std::exit(1);
    }

    packet_ = av_packet_alloc();
    if (!packet_) {
        std::cerr << "av_packet_alloc failed" << std::endl;
        std::exit(1);
    }

    sws_ctx_ = sws_getContext(width, height, AV_PIX_FMT_RGBA,
                              width, height, AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        std::cerr << "sws_getContext failed" << std::endl;
        std::exit(1);
    }
}

H264Encoder::~H264Encoder() {
    if (sws_ctx_) sws_freeContext(sws_ctx_);
    if (packet_) av_packet_free(&packet_);
    if (frame_) av_frame_free(&frame_);
    if (codec_ctx_) avcodec_free_context(&codec_ctx_);
}

bool H264Encoder::encode(const uint8_t* rgba, int64_t pts,
                         const std::function<bool(const uint8_t*, size_t)>& sink) {
    if (av_frame_make_writable(frame_) < 0) return false;

    const uint8_t* src_data[1] = { rgba };
    int src_linesize[1] = { width_ * 4 };
    sws_scale(sws_ctx_, src_data, src_linesize, 0, height_,
              frame_->data, frame_->linesize);

    frame_->pts = pts;
    if (avcodec_send_frame(codec_ctx_, frame_) < 0) return false;

    bool ok = true;
    while (avcodec_receive_packet(codec_ctx_, packet_) == 0) {
        if (ok && !sink(packet_->data, packet_->size)) ok = false;
        av_packet_unref(packet_);
    }
    return ok;
}

} // namespace geom::stream
