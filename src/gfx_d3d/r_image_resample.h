#pragma once
#include <cstdint>

// Canonical in-place RGB resampling. The buffer must hold
// max(oldWidth, newWidth) * max(oldHeight, newHeight) * bytesPerPixel bytes.
// Dimensions are positive; bytesPerPixel >= 3. Extra channels are untouched.
void R_ResampleImage(int oldWidth, int oldHeight, int newWidth, int newHeight,
    int bytesPerPixel, std::uint8_t *data);
