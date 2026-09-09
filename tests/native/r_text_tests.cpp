#include <universal/q_shared.h>
#include <universal/com_math.h>
#include <qcommon/mem_track.h>
#include <gfx_d3d/r_text.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_warning_types.h>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Synthetic ASCII font and recording backend. No game assets or graphics API.
struct Quad { const Material *material; float x, y, w, h, s, t; std::uint32_t color; };
std::vector<Quad> quads;
int sceneTime = 0, cursorTime = 0;
int R_TextSceneTime() { return sceneTime; }
int R_TextCursorTime() { return cursorTime; }
void R_TextDrawQuad(const Material *material, float x, float y, float w, float h,
    float s, float t, float, float, float, float, std::uint32_t color)
{ quads.push_back({material, x, y, w, h, s, t, color}); }
void MyAssertHandler(const char *, int, int, const char *, ...) { std::abort(); }
void R_WarnOncePerFrame(GfxWarningType, ...) { std::abort(); }
const Material *Material_FromHandle(Material *material) { return material; }
bool IsValidMaterialHandle(Material *const material) { return material != nullptr; }
bool Material_HasAnyFogableTechnique(const Material *) { return false; }
std::uint32_t LongNoSwap(std::uint32_t value) { return value; }
std::uint8_t ColorIndex(std::uint8_t c) { return c >= '0' && c <= '9' ? c - '0' : 7; }
void RB_LookupColor(std::uint8_t c, GfxColor *color) { *color = color_table[ColorIndex(c)]; }
char *va(const char *, ...) { static char message[] = "invalid synthetic icon"; return message; }
std::uint32_t SEH_ReadCharFromString(const char **text, int *)
{ return static_cast<unsigned char>(*(*text)++); }
int SEH_PrintStrlen(const char *text)
{
    int count = 0;
    while (*text) {
        if (text[0] == '^' && text[1] >= '0' && text[1] <= '9') text += 2;
        else { ++count; ++text; }
    }
    return count;
}
bool Near(float a, float b) { return std::fabs(a - b) < 0.0001f; }

void TestSeededRandom()
{
    // Include negative quotients around the signed-division boundaries.
    constexpr int vectors[][3] = {
        {2, -2087924461, 909}, {-1, -1103502900, 15930},
        {INT32_MIN, -2147471303, 1},
        {576240414, -65537, 32767}, {286434947, -65536, 32767},
        {-3370520, -65535, 0}, {230538014, -1, 0}, {-59267453, 0, 0},
        {-349072920, 1, 0}, {-115164386, 65535, 0},
        {-404969853, 65536, 1}, {-694775320, 65537, 1},
        {2088216195, INT32_MIN, 0}, {-1916945634, INT32_MAX, 32767},
    };
    for (const auto &values : vectors)
    {
        int seed = values[0];
        assert(RandWithSeed(&seed) == static_cast<unsigned>(values[2]));
        assert(seed == values[1]);
    }
    assert(R_FontGetRandomLetter(nullptr, 2) == 'p');
    assert(R_FontGetRandomLetter(nullptr, -1) == '7');
    // Native x86 wraps the LCG to 32 bits, then divides the signed result
    // toward zero. Widen the oracle arithmetic so it cannot overflow.
    for (const int initial : {0, 1, 2, -1, INT32_MIN, INT32_MAX})
    {
        int seed = initial;
        std::int64_t expected = initial;
        for (unsigned step = 0; step < 4096; ++step)
        {
            const auto wrapped = static_cast<std::uint32_t>(1103515245ll * expected + 12345);
            expected = wrapped <= INT32_MAX ? wrapped : static_cast<std::int64_t>(wrapped) - 0x100000000ll;
            const auto result = RandWithSeed(&seed);
            if (seed != expected)
                std::fprintf(stderr, "RNG seed %d step %u: got %d, expected %lld\n",
                    initial, step, seed, static_cast<long long>(expected));
            assert(seed == expected);
            assert(result == static_cast<std::uint32_t>(expected / 65536) % 32768u);
        }
    }
}

int main()
{
    // The debug text caller supplies stack storage. Disabled tracking must
    // report defined counters rather than rendering prior stack contents.
    meminfo_t memoryInfo;
    memoryInfo.total = memoryInfo.nonSwapTotal = memoryInfo.nonSwapMinSpecTotal = 12345;
    for (auto &total : memoryInfo.typeTotal) total = 12345;
    track_getbasicinfo(&memoryInfo);
    assert(memoryInfo.total == 0 && memoryInfo.nonSwapTotal == 0 && memoryInfo.nonSwapMinSpecTotal == 0);
    for (auto total : memoryInfo.typeTotal) assert(total == 0);
    TestSeededRandom();
    Material base{}, glow{}, fx{};
    MaterialTechniqueSet tech{};
    base.info.name = "synthetic-font";
    glow.info.name = "synthetic-glow";
    fx.info.name = "synthetic-fx";
    fx.techniqueSet = &tech;
    std::array<Glyph, 96> glyphs{};
    for (unsigned i = 0; i < glyphs.size(); ++i) {
        glyphs[i].letter = static_cast<std::uint16_t>(i + 32);
        glyphs[i].dx = 8;
        glyphs[i].pixelWidth = 6;
        glyphs[i].pixelHeight = 10;
        glyphs[i].y0 = -10;
        glyphs[i].s1 = glyphs[i].t1 = 1;
    }
    glyphs['o' - 32].dx = 12;
    Font_s font{"synthetic", 12, 96, &base, &glow, glyphs.data()};
    const auto draw = [&](const char *text, int flags = 0, int cursor = -1,
        float sine = 0, float cosine = 1, int birthTime = 100) {
        quads.clear();
        DrawText2D(text, 20, 30, &font, 1, 1, sine, cosine, GfxColor(0xffffffffu),
            99, static_cast<short>(flags), cursor, '|', 0, GfxColor(0xff030201u),
            birthTime, 100, 1000, 1000, &fx, &fx);
    };
    draw("AB");
    assert(quads.size() == 2 && Near(quads[0].x, 19.5f) && Near(quads[0].y, 19.5f));
    assert(Near(quads[1].x, 27.5f));
    draw("AB", R_TextStyleFlags(128));
    assert(quads.size() == 2 && Near(quads[1].x, 31.5f));
    draw("A", R_TextStyleFlags(3));
    assert(quads.size() == 2 && Near(quads[0].x, 20.5f));
    assert(quads[0].color == 0xff000000u && quads[1].color == 0xffffffffu);
    draw("A", R_TextStyleFlags(6));
    assert(Near(quads[0].x, 21.5f) && Near(quads[0].y, 21.5f));
    draw("A", 0, -1, 1, 0);
    assert(Near(quads[0].x, 29.5f) && Near(quads[0].y, 29.5f));
    draw("^1A^7B");
    assert(quads.size() == 2 && quads[0].color == 0xffff5c5cu && quads[1].color == 0xffffffffu);
    draw("A^2B^7C", 0x130);
    assert(quads.size() == 7); // only the highlighted subtitle glyph glows
    assert(quads[1].color == 0xdce6ffe6u);
    for (int i = 3; i < 7; ++i) {
        assert(quads[i].material == &glow && Near(quads[i].w, 10.5f));
        assert(Near(quads[i].h, 11.25f) && (quads[i].color & 0xffffffu) == 0x030201u);
    }
    cursorTime = 0;
    draw("A", 2, 1);
    assert(quads.size() == 2);
    cursorTime = 256;
    draw("A", 2, 1);
    assert(quads.size() == 1);
    sceneTime = 100;
    draw("ABCD", 0xc0);
    assert(quads.size() == 1 && (quads[0].color >> 24) == 192u);
    sceneTime = 250;
    draw("ABCD", 0xc0);
    assert(quads.size() == 2);
    sceneTime = 501;
    draw("ABCD", 0xc0);
    assert(quads.size() == 4);
    sceneTime = 2101;
    draw("ABCD", 0xc0);
    assert(quads.empty());
    // Seed 1 selects tick 8 of 30: decay ends at 8 * 33 = 264 ms.
    int seed = 1;
    bool skip = false, extra = false;
    std::uint8_t alpha = 0;
    std::uint32_t letter = 0;
    GetDecayingLetterInfo('A', &font, &seed, 250, 100, 1000, 128,
        &skip, &alpha, &letter, &extra);
    assert(!skip && alpha == 30);
    seed = 1;
    GetDecayingLetterInfo('A', &font, &seed, 264, 100, 1000, 128,
        &skip, &alpha, &letter, &extra);
    assert(skip);
    // Record real text output across reveal, decay and expiry for many seeds.
    // Exact binary-fraction glyph UVs expose changed random letters as well
    // as changed quad counts, without retail assets or platform rendering.
    for (unsigned i = 0; i < glyphs.size(); ++i) glyphs[i].s0 = i / 128.0f;
    std::uint32_t trace = 2166136261u;
    std::uint64_t quadCount = 0;
    const auto record = [&](std::uint32_t word) { trace = (trace ^ word) * 16777619u; };
    for (int birthTime = 1; birthTime < 64000; birthTime += 997)
        for (int elapsed = 0; elapsed <= 2048; elapsed += 16)
            for (int flags : {0xc0, 0xd0})
            {
                sceneTime = birthTime + elapsed;
                draw("Crew Expendable", flags, -1, 0, 1, birthTime);
                record(static_cast<std::uint32_t>(quads.size()));
                quadCount += quads.size();
                for (const auto &quad : quads)
                {
                    record(quad.material == &base ? 0u : quad.material == &glow ? 1u : 2u);
                    for (float value : {quad.x, quad.y, quad.w, quad.h, quad.s, quad.t})
                        record(std::bit_cast<std::uint32_t>(value));
                    record(quad.color);
                }
            }
    std::printf("text effects trace: %08x, %llu quads\n", trace,
        static_cast<unsigned long long>(quadCount));
    assert(trace == 0x2d6524e5u && quadCount == 334932u);
    std::puts("native text: position/rotation, colors, styles, subtitle glow, cursor, reveal/expiry passed");
}

