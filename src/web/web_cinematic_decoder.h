#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVIOContext;
struct AVPacket;

// Codec/device boundary only. Kisak owns cinematic requests and mission state.
struct WebCinematicInput
{
    void *opaque;
    std::int64_t size;
    int (*read)(void *, std::uint8_t *, int);
    std::int64_t (*seek)(void *, std::int64_t, int);
};

struct WebCinematicVideo
{
    std::array<const std::uint8_t *, 4> planes{};
    std::array<int, 4> strides{};
    int width = 0;
    int height = 0;
    bool alpha = false;
    bool fullRange = false;
    double seconds = 0;
};

struct WebCinematicAudio
{
    int track = 0;
    int channels = 0;
    int sampleRate = 0;
    std::vector<float> samples;
};

class WebCinematicDecoder
{
public:
    WebCinematicDecoder() = default;
    ~WebCinematicDecoder();
    WebCinematicDecoder(const WebCinematicDecoder &) = delete;
    WebCinematicDecoder &operator=(const WebCinematicDecoder &) = delete;

    bool Open(const WebCinematicInput &input);
    void Close();
    // One video frame and its preceding audio. Plane pointers live until the
    // next ReadFrame/Close. 1 = frame, 0 = clean end, -1 = rejected data.
    int ReadFrame(WebCinematicVideo &video, std::vector<WebCinematicAudio> &audio);
    double Duration() const { return duration_; }
    double FrameSeconds() const { return frameSeconds_; }
    const char *Error() const { return error_; }

private:
    bool Fail(int error);
    WebCinematicInput input_{};
    AVFormatContext *format_ = nullptr;
    AVIOContext *io_ = nullptr;
    std::array<AVCodecContext *, 2> codecs_{};
    AVFrame *frame_ = nullptr;
    AVPacket *packet_ = nullptr;
    double duration_ = 0;
    double frameSeconds_ = 0;
    std::int64_t framesRead_ = 0;
    std::int64_t expectedFrames_ = 0;
    char error_[128]{};
};
