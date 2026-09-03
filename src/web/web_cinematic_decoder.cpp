#include "web_cinematic_decoder.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <cmath>
#include <cstring>
#include <utility>

namespace
{
int Read(void *opaque, std::uint8_t *bytes, int size)
{
    const auto &input = *static_cast<WebCinematicInput *>(opaque);
    const int count = input.read(input.opaque, bytes, size);
    return count > 0 ? count : count == 0 ? AVERROR_EOF : AVERROR(EIO);
}

std::int64_t Seek(void *opaque, std::int64_t offset, int whence)
{
    const auto &input = *static_cast<WebCinematicInput *>(opaque);
    if (whence == AVSEEK_SIZE) return input.size;
    return input.seek(input.opaque, offset, whence & ~AVSEEK_FORCE);
}
}

WebCinematicDecoder::~WebCinematicDecoder() { Close(); }

bool WebCinematicDecoder::Fail(int error)
{
    av_strerror(error, error_, sizeof(error_));
    return false;
}

void WebCinematicDecoder::Close()
{
    av_packet_free(&packet_);
    av_frame_free(&frame_);
    for (auto &codec : codecs_) avcodec_free_context(&codec);
    avformat_close_input(&format_);
    if (io_) av_freep(&io_->buffer);
    avio_context_free(&io_);
    duration_ = frameSeconds_ = 0;
    framesRead_ = expectedFrames_ = 0;
}

bool WebCinematicDecoder::Open(const WebCinematicInput &input)
{
    Close();
    error_[0] = 0;
    if (!input.read || !input.seek || input.size < 44 || input.size > 512 * 1024 * 1024)
        return Fail(AVERROR_INVALIDDATA);
    input_ = input;
    // Decoder allocations are independent of Kisak's heap owners. Bound a
    // single codec allocation before admitting untrusted container metadata.
    av_max_alloc(64 * 1024 * 1024);
    auto *buffer = static_cast<std::uint8_t *>(av_malloc(32768));
    if (!buffer) return Fail(AVERROR(ENOMEM));
    io_ = avio_alloc_context(buffer, 32768, 0, &input_, Read, nullptr, Seek);
    if (!io_)
    {
        av_free(buffer);
        return Fail(AVERROR(ENOMEM));
    }
    format_ = avformat_alloc_context();
    if (!format_) return Fail(AVERROR(ENOMEM));
    format_->pb = io_;
    format_->flags |= AVFMT_FLAG_CUSTOM_IO;
    int result = avformat_open_input(&format_, nullptr, av_find_input_format("bink"), nullptr);
    if (result < 0) return Fail(result);
    // The owned PC movies have zero or one stereo/mono track. Reject surround
    // track layouts until their native mix-bin mapping is carried to the device.
    if (format_->nb_streams < 1 || format_->nb_streams > 2)
        return Fail(AVERROR_INVALIDDATA);
    const AVStream &video = *format_->streams[0];
    const AVCodecParameters &parameters = *video.codecpar;
    if (parameters.codec_id != AV_CODEC_ID_BINKVIDEO ||
        parameters.width < 1 || parameters.width > 1920 ||
        parameters.height < 1 || parameters.height > 1080 || video.duration < 1)
        return Fail(AVERROR_INVALIDDATA);
    frameSeconds_ = av_q2d(video.time_base);
    expectedFrames_ = video.duration;
    duration_ = video.duration * frameSeconds_;
    if (!std::isfinite(duration_) || duration_ > 3600 ||
        frameSeconds_ < 1.0 / 120 || frameSeconds_ > 1)
        return Fail(AVERROR_INVALIDDATA);
    for (unsigned int index = 0; index < format_->nb_streams; ++index)
    {
        const auto &params = *format_->streams[index]->codecpar;
        if (index && ((params.codec_id != AV_CODEC_ID_BINKAUDIO_DCT &&
            params.codec_id != AV_CODEC_ID_BINKAUDIO_RDFT) ||
            params.sample_rate < 8000 || params.sample_rate > 48000 ||
            params.ch_layout.nb_channels < 1 || params.ch_layout.nb_channels > 2))
            return Fail(AVERROR_INVALIDDATA);
        const AVCodec *codec = avcodec_find_decoder(params.codec_id);
        if (!codec) return Fail(AVERROR_DECODER_NOT_FOUND);
        codecs_[index] = avcodec_alloc_context3(codec);
        if (!codecs_[index]) return Fail(AVERROR(ENOMEM));
        if ((result = avcodec_parameters_to_context(codecs_[index], &params)) < 0)
            return Fail(result);
        codecs_[index]->thread_count = 1;
        codecs_[index]->max_pixels = 1920 * 1080;
        if ((result = avcodec_open2(codecs_[index], codec, nullptr)) < 0)
            return Fail(result);
    }
    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    return frame_ && packet_ ? true : Fail(AVERROR(ENOMEM));
}

int WebCinematicDecoder::ReadFrame(WebCinematicVideo &video,
    std::vector<WebCinematicAudio> &audio)
{
    video = {};
    audio.clear();
    if (!format_ || !frame_ || !packet_) return -1;
    std::size_t audioSamples = 0;
    // A Bink frame contains at most one packet per track, then video. Keep an
    // explicit bound even if a malformed demuxer stream fails to make progress.
    for (unsigned int packetIndex = 0; packetIndex < codecs_.size(); ++packetIndex)
    {
        av_packet_unref(packet_);
        int result = av_read_frame(format_, packet_);
        if (result == AVERROR_EOF)
        {
            if (framesRead_ == expectedFrames_) return 0;
            Fail(AVERROR_INVALIDDATA);
            return -1;
        }
        if (result < 0) { Fail(result); return -1; }
        const int stream = packet_->stream_index;
        if (stream < 0 || static_cast<unsigned int>(stream) >= format_->nb_streams ||
            packet_->size > 16 * 1024 * 1024)
        { Fail(AVERROR_INVALIDDATA); return -1; }
        result = avcodec_send_packet(codecs_[stream], packet_);
        if (result < 0) { Fail(result); return -1; }
        while ((result = avcodec_receive_frame(codecs_[stream], frame_)) >= 0)
        {
            if (stream == 0)
            {
                if (framesRead_ >= expectedFrames_ || packet_->pts != framesRead_++)
                { Fail(AVERROR_INVALIDDATA); return -1; }
                if (frame_->format != AV_PIX_FMT_YUV420P && frame_->format != AV_PIX_FMT_YUVA420P)
                { Fail(AVERROR_INVALIDDATA); return -1; }
                video.width = frame_->width;
                video.height = frame_->height;
                video.alpha = frame_->format == AV_PIX_FMT_YUVA420P;
                video.fullRange = frame_->color_range == AVCOL_RANGE_JPEG;
                video.seconds = packet_->pts * frameSeconds_;
                for (unsigned int plane = 0; plane < video.planes.size(); ++plane)
                {
                    video.planes[plane] = frame_->data[plane];
                    video.strides[plane] = frame_->linesize[plane];
                }
                return 1;
            }
            const int channels = frame_->ch_layout.nb_channels;
            if ((frame_->format != AV_SAMPLE_FMT_FLT && frame_->format != AV_SAMPLE_FMT_FLTP) ||
                channels < 1 || channels > 2 || frame_->nb_samples < 1 || frame_->nb_samples > 48000)
            { Fail(AVERROR_INVALIDDATA); return -1; }
            const std::size_t count = static_cast<std::size_t>(frame_->nb_samples) * channels;
            audioSamples += count;
            if (audioSamples > 48000 * 2 * 5 * 2)
            { Fail(AVERROR_INVALIDDATA); return -1; }
            WebCinematicAudio block;
            block.track = stream - 1;
            block.channels = channels;
            block.sampleRate = frame_->sample_rate;
            block.samples.resize(count);
            for (int sample = 0; sample < frame_->nb_samples; ++sample)
                for (int channel = 0; channel < channels; ++channel)
                {
                    const bool planar = frame_->format == AV_SAMPLE_FMT_FLTP;
                    const float value = reinterpret_cast<const float *>(frame_->extended_data[planar ? channel : 0])
                        [planar ? sample : sample * channels + channel];
                    if (!std::isfinite(value)) { Fail(AVERROR_INVALIDDATA); return -1; }
                    block.samples[sample * channels + channel] = value;
                }
            audio.push_back(std::move(block));
        }
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
        { Fail(result); return -1; }
    }
    Fail(AVERROR_INVALIDDATA);
    return -1;
}
