#include <gfx_d3d/r_image_resample.h>
#include <gfx_d3d/r_savegame_image.h>
#include <qcommon/qcommon_math.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

void MyAssertHandler(const char *, int, int, const char *, ...) { std::abort(); }

// Independent area/linear reference, using IW3's round-to-nearest-even.
static unsigned char Sample(const std::vector<unsigned char> &pixels,
    int offset, int stride, int channel, int oldSize, int newSize, int x)
{
    int sum = 0;
    if (newSize <= oldSize)
    {
        for (int i = 0; i < oldSize; ++i)
        {
            const int weight = std::max(0, std::min((x + 1) * oldSize, (i + 1) * newSize)
                - std::max(x * oldSize, i * newSize));
            sum += pixels[offset + i * stride + channel] * weight;
        }
        return SnapFloatToInt(static_cast<float>(1.0 / oldSize) * static_cast<float>(sum));
    }
    const int numerator = (2 * x + 1) * oldSize - newSize;
    const int denominator = 2 * newSize;
    const int first = numerator < 0 ? -1 : numerator / denominator;
    const int weight = numerator - first * denominator;
    sum = pixels[offset + std::clamp(first, 0, oldSize - 1) * stride + channel] * (denominator - weight)
        + pixels[offset + std::clamp(first + 1, 0, oldSize - 1) * stride + channel] * weight;
    return SnapFloatToInt(static_cast<float>(0.5 / newSize) * static_cast<float>(sum));
}

int main()
{
    // Synthetic baseline JPEG headers only; the browser codec tests own the
    // actual entropy encode/decode round trip. No proprietary fixture bytes.
    std::vector<unsigned char> jpeg{0xff, 0xd8, 0xff, 0xc0, 0, 17, 8, 2, 0, 2, 0, 3,
        1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1, 0xff, 0xda, 0, 2};
    assert(R_IsSaveGameJpeg(jpeg));
    for (size_t end = 0; end < jpeg.size(); ++end) assert(!R_IsSaveGameJpeg({jpeg.data(), end}));
    auto bad = jpeg; bad[7] = 0xff; assert(!R_IsSaveGameJpeg(bad));
    bad = jpeg; bad[5] = 0xff; assert(!R_IsSaveGameJpeg(bad));
    bad = jpeg; bad[6] = 16; assert(!R_IsSaveGameJpeg(bad));
    bad = jpeg; bad[11] = 4; assert(!R_IsSaveGameJpeg(bad));
    bad = jpeg; bad.insert(bad.begin() + 21, jpeg.begin() + 2, jpeg.begin() + 21);
    assert(!R_IsSaveGameJpeg(bad));
    bad = jpeg; bad.resize(SAVEGAME_JPEG_MAX_BYTES + 1); assert(!R_IsSaveGameJpeg(bad));
    unsigned cases = 0;
    for (int w = 1; w <= 9; ++w) for (int h = 1; h <= 7; ++h)
    for (int nw = 1; nw <= 11; ++nw) for (int nh = 1; nh <= 9; ++nh)
    for (int bpp : {3, 4})
    {
        std::vector<unsigned char> input(w * h * bpp), horizontal(nw * h * bpp);
        for (unsigned i = 0; i < input.size(); ++i) input[i] = (i * 73 + w * 19 + h * 7) % 256;
        for (int y = 0; y < h; ++y) for (int x = 0; x < nw; ++x)
        for (int c = 0; c < 3; ++c)
            horizontal[(y * nw + x) * bpp + c] = Sample(input, y * w * bpp, bpp, c, w, nw, x);
        const size_t capacity = std::max(w, nw) * std::max(h, nh) * bpp;
        std::vector<unsigned char> actual(capacity + 16, 0xD3);
        std::copy(input.begin(), input.end(), actual.begin());
        R_ResampleImage(w, h, nw, nh, bpp, actual.data());
        for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x)
        for (int c = 0; c < 3; ++c)
            assert(actual[(y * nw + x) * bpp + c] == Sample(horizontal, x * bpp, nw * bpp, c, h, nh, y));
        for (size_t i = capacity; i < actual.size(); ++i) assert(actual[i] == 0xD3);
        ++cases;
    }
#ifdef _WIN32
    // The old zero-weight read crosses into PAGE_NOACCESS here.
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    auto *pages = static_cast<unsigned char *>(VirtualAlloc(nullptr, info.dwPageSize * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(pages);
    DWORD previous = 0;
    assert(VirtualProtect(pages + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &previous));
    auto *edge = pages + info.dwPageSize - 6;
    std::fill(edge, edge + 6, 42);
    R_ResampleImage(2, 1, 1, 1, 3, edge);
    assert(edge[0] == 42 && edge[1] == 42 && edge[2] == 42);
    assert(VirtualFree(pages, 0, MEM_RELEASE));
#endif
    std::printf("canonical resampler: %u area/linear RGB cases passed\n", cases);
}
