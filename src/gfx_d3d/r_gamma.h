#pragma once
#include <cstdint>

struct GfxGammaRamp
{
    std::uint16_t entries[256];
};

void R_CalcGammaRamp(GfxGammaRamp *gammaRamp);
void R_GammaCorrect(std::uint8_t *buffer, int bufSize);
