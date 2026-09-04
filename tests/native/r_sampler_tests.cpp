#include <gfx_d3d/r_sampler.h>
#include <cassert>
#include <cstdio>

int main()
{
    assert(R_DecodeSamplerFilter(0x12, 0, 1, 16, false) == 0x22201);
    assert(R_DecodeSamplerFilter(0x12, 0, 8, 16, false) == 0x23308);
    assert(R_DecodeSamplerFilter(0x02, 0, 8, 16, false) == 0x02201);
    assert(R_DecodeSamplerFilter(0x13, 0, 1, 16, false) == 0x23302);
    assert(R_DecodeSamplerFilter(0x14, 0, 1, 2, false) == 0x23302);
    assert(R_DecodeSamplerFilter(0x14, 0, 8, 4, false) == 0x23304);
    assert(R_DecodeSamplerFilter(0x0a, 1, 1, 1, false) == 0x22201);
    assert(R_DecodeSamplerFilter(0x12, 2, 1, 1, false) == 0x12201);
    assert(R_DecodeSamplerFilter(0x12, 3, 1, 1, false) == 0x02201);
    assert(R_DecodeSamplerFilter(0x14, 0, 8, 16, true) == 0x01101);
    assert(R_DecodeSamplerFilter(0x13, 0, 1, 16, false, false, true) == 0x23202);
    assert(R_DecodeSamplerFilter(0x13, 0, 1, 16, false, true, false) == 0x22302);
    unsigned hash = 2166136261u;
    for (int mode = 0; mode < 4; ++mode)
        for (int minimum : {1, 2, 4, 8, 16})
            for (int maximum : {1, 2, 4, 8, 16})
                for (int capabilities = 0; capabilities < 8; ++capabilities)
                    for (unsigned sampler = 0; sampler < 24; ++sampler)
                        hash = (hash ^ R_DecodeSamplerFilter(sampler, mode,
                            minimum, maximum, capabilities & 4,
                            capabilities & 1, capabilities & 2)) * 16777619u;
    std::printf("native-sampler-policy=%08x cases=19200\n", hash);
    // Matched against unmodified R_SetTexFilter at 8be61213 on MSVC Win32.
    assert(hash == 0x7a8b72b5u);
}
