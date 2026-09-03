#include <web/web_cinematic_decoder.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// Synthetic malformed containers only. Optional owned-file validation reads
// the caller's path directly and never adds movie data to the test fixtures.
namespace
{
struct MemoryFile { std::vector<std::uint8_t> bytes; std::size_t position = 0; };
int ReadMemory(void *opaque, std::uint8_t *bytes, int count)
{
    auto &file = *static_cast<MemoryFile *>(opaque);
    const auto size = std::min<std::size_t>(count, file.bytes.size() - file.position);
    std::memcpy(bytes, file.bytes.data() + file.position, size);
    file.position += size;
    return static_cast<int>(size);
}
std::int64_t SeekMemory(void *opaque, std::int64_t offset, int whence)
{
    auto &file = *static_cast<MemoryFile *>(opaque);
    if (whence == SEEK_CUR) offset += file.position;
    else if (whence == SEEK_END) offset += file.bytes.size();
    else if (whence != SEEK_SET) return -1;
    if (offset < 0 || static_cast<std::uint64_t>(offset) > file.bytes.size()) return -1;
    file.position = static_cast<std::size_t>(offset);
    return offset;
}
void Put32(MemoryFile &file, int offset, std::uint32_t value)
{
    for (int byte = 0; byte < 4; ++byte) file.bytes[offset + byte] = value >> (byte * 8);
}
MemoryFile Header()
{
    MemoryFile file{std::vector<std::uint8_t>(64)};
    std::memcpy(file.bytes.data(), "BIKi", 4);
    Put32(file, 4, 56); Put32(file, 8, 1); Put32(file, 12, 16);
    Put32(file, 16, 1); Put32(file, 20, 16); Put32(file, 24, 16);
    Put32(file, 28, 30); Put32(file, 32, 1); Put32(file, 44, 49);
    return file;
}
int ReadFile(void *opaque, std::uint8_t *bytes, int count)
{
    return static_cast<int>(std::fread(bytes, 1, count, static_cast<std::FILE *>(opaque)));
}
std::int64_t SeekFile(void *opaque, std::int64_t offset, int whence)
{
    auto *file = static_cast<std::FILE *>(opaque);
    return std::fseek(file, static_cast<long>(offset), whence) ? -1 : std::ftell(file);
}
}

int main(int argc, char **argv)
{
    for (const auto [offset, value] : std::array<std::pair<int, std::uint32_t>, 7>{{
        {8, 1000001}, {12, 1000}, {20, 20000}, {24, 20000}, {28, 0}, {32, 0}, {40, 257}}})
    {
        auto file = Header();
        Put32(file, offset, value);
        WebCinematicDecoder decoder;
        assert(!decoder.Open({&file, static_cast<std::int64_t>(file.bytes.size()), ReadMemory, SeekMemory}));
        assert(decoder.Error()[0]);
    }
    for (int size = 0; size < 44; ++size)
    {
        auto file = Header(); file.bytes.resize(size);
        WebCinematicDecoder decoder;
        assert(!decoder.Open({&file, size, ReadMemory, SeekMemory}));
    }
    if (argc == 1) { std::puts("synthetic cinematic rejection checks passed"); return 0; }

    auto *file = std::fopen(argv[1], "rb");
    assert(file);
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::rewind(file);
    WebCinematicDecoder decoder;
    if (!decoder.Open({file, size, ReadFile, SeekFile}))
    { std::fprintf(stderr, "open: %s\n", decoder.Error()); return 1; }
    WebCinematicVideo video;
    std::vector<WebCinematicAudio> audio;
    unsigned int frames = 0;
    std::uint64_t samples = 0;
    std::uint32_t checksum = 2166136261u;
    float peak = 0;
    double lastSeconds = -1;
    int result;
    auto started = std::chrono::steady_clock::now();
    while ((result = decoder.ReadFrame(video, audio)) == 1)
    {
        assert(video.seconds > lastSeconds);
        lastSeconds = video.seconds;
        for (int y = 0; y < video.height; ++y)
            for (int x = 0; x < video.width; ++x)
                checksum = (checksum ^ video.planes[0][y * video.strides[0] + x]) * 16777619u;
        for (const auto &block : audio)
            for (float sample : block.samples)
            { assert(std::isfinite(sample)); peak = std::max(peak, std::abs(sample)); ++samples; }
        if (frames == 0 && argc > 2)
        {
            auto *image = std::fopen(argv[2], "wb"); assert(image);
            std::fprintf(image, "P5\n%d %d\n255\n", video.width, video.height);
            for (int y = 0; y < video.height; ++y)
                assert(std::fwrite(video.planes[0] + y * video.strides[0], 1, video.width, image) == static_cast<std::size_t>(video.width));
            std::fclose(image);
        }
        ++frames;
    }
    if (result < 0) { std::fprintf(stderr, "decode frame %u: %s\n", frames, decoder.Error()); return 1; }
    assert(frames > 0);
    assert(std::abs(frames * decoder.FrameSeconds() - decoder.Duration()) < 0.001);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("{\"frames\":%u,\"duration\":%.6f,\"audioSamples\":%llu,\"audioPeak\":%.6f,\"lumaHash\":%u,\"decodeSeconds\":%.3f}\n",
        frames, decoder.Duration(), static_cast<unsigned long long>(samples), peak, checksum, elapsed);
    decoder.Close();
    std::fclose(file);
}
