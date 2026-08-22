#include <web/web_renderer_lighting.h>

#include <algorithm>
#include <cmath>

std::array<float, 3> WebRenderer_EvaluateSecondaryDirectionalLighting(
    const std::array<float, 4> &base,
    const std::array<float, 4> &vertexColor,
    const std::array<float, 4> &secondaryLobe0,
    const std::array<float, 4> &secondaryLobe1) noexcept
{
    const float encoded0 = secondaryLobe0[3] * 4.08f - 2.08f;
    const float encoded1 = secondaryLobe1[3] * 4.06451607f - 2.06451607f;
    const float directionalWeight = std::clamp(
        1.0f / std::sqrt(
            encoded0 * encoded0 + encoded1 * encoded1 + 1.0f),
        0.0f,
        1.0f);
    std::array<float, 3> result{};
    for (std::size_t channel = 0u; channel < result.size(); ++channel)
    {
        const float lighting = secondaryLobe0[channel] +
            secondaryLobe1[channel] * directionalWeight;
        result[channel] = base[channel] * vertexColor[channel] * lighting;
    }
    return result;
}
