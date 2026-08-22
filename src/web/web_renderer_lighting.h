#pragma once

#include <array>

// CPU reference for the normalized-rgba8 math emitted by the native
// lm_r0c0_sm2 pixel shader. The browser fragment shader mirrors this helper.
std::array<float, 3> WebRenderer_EvaluateSecondaryDirectionalLighting(
    const std::array<float, 4> &base,
    const std::array<float, 4> &vertexColor,
    const std::array<float, 4> &secondaryLobe0,
    const std::array<float, 4> &secondaryLobe1) noexcept;

