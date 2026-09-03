#include <universal/q_shared.h>
#include <gfx_d3d/r_gamma.h>
#include <gfx_d3d/r_dvars.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>

dvar_t gammaDvar{};
const dvar_t *r_gamma = &gammaDvar;
void MyAssertHandler(const char *, int, int, const char *, ...) { std::abort(); }

int main()
{
    for (float gamma : {0.5f, 0.8f, 1.0f, 2.0f, 3.0f})
    {
        gammaDvar.current.value = gamma;
        GfxGammaRamp ramp{};
        R_CalcGammaRamp(&ramp);
        assert(ramp.entries[0] == 0 && ramp.entries[255] == 65535);
        std::array<std::uint8_t, 256> pixels{};
        for (unsigned i = 0; i < 256; ++i) pixels[i] = i;
        R_GammaCorrect(pixels.data(), pixels.size());
        std::uint32_t hash = 2166136261u;
        for (unsigned i = 0; i < 256; ++i)
        {
            assert(!i || ramp.entries[i] >= ramp.entries[i - 1]);
            assert(gamma != 1.0f || ramp.entries[i] == i * 257);
            assert(pixels[i] == 255u * ramp.entries[i] / 65535u);
            hash = (hash ^ ramp.entries[i]) * 16777619u;
        }
        std::printf("gamma=%.1f ramp=%08x mid=%u byte=%u\n",
            gamma, hash, ramp.entries[128], pixels[128]);
    }
}
