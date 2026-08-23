#pragma once
#include "r_material.h"

#include <cstddef>
#include <cstdint>

#define HCOUNT 4096

struct WaterGlob // sizeof=0x9000
{                                       // ...
    complex_s H[4096];
    uint8_t pixels[4096];       // ...
};

struct WaterGlobStatic // sizeof=0x1C00
{                                       // ...
    float sinTable[1024];               // ...
    complex_s fftTrigTable[256];        // ...
    int fftBitswap[256];                // ...
};

void __cdecl TRACK_r_water();
void __cdecl R_UploadWaterTextureInternal(water_t **data);
void __cdecl WaterFrequenciesAtTime(complex_s *H, const water_t *water, float t);
void __cdecl WaterAmplitudesFromFrequencies(complex_s *H, const water_t *water);
void __cdecl TransposeArray(complex_s *H, uint32_t M);
void __cdecl WaterPixelsFromAmplitudes(GfxColor *pixels, complex_s *H, const water_t *water);
#ifndef KISAK_WEB
void __cdecl GenerateMipMaps(_D3DFORMAT format, uint8_t *pixels, water_t *water);
void __cdecl R_UploadWaterTexture(water_t *water, float floatTime);
#endif
void __cdecl R_InitWater();
#ifndef KISAK_WEB
void __cdecl Load_PicmipWater(water_t **waterRef);
#endif

// Runs the canonical IW3 water frequency/FFT/amplitude conversion without
// performing a platform texture upload. The output is the native L8 image
// consumed by water_l_sun.
bool R_GenerateWaterPixelsR8(
    const water_t *water,
    float floatTime,
    std::uint8_t *pixels,
    std::size_t pixelCapacity);

// r_water_load_obj
void __cdecl R_InitLoadWater();
water_t *__cdecl R_LoadWaterSetup(const water_t *water);
void __cdecl R_ShutdownLoadWater();
