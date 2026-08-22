#include <web/web_renderer_lighting.h>

#include <array>
#include <cassert>
#include <cmath>

namespace
{
bool Near(float actual, float expected) noexcept
{
    return std::fabs(actual - expected) < 0.00001f;
}

void TestNativeSecondaryDirectionalDecode()
{
    const std::array<float, 4> base{0.8f, 0.6f, 0.4f, 1.0f};
    const std::array<float, 4> vertex{0.5f, 0.75f, 1.0f, 1.0f};
    const std::array<float, 4> lobe0{0.2f, 0.3f, 0.4f, 0.5f};
    const std::array<float, 4> lobe1{0.6f, 0.4f, 0.2f, 0.5f};
    const std::array<float, 3> result =
        WebRenderer_EvaluateSecondaryDirectionalLighting(
            base, vertex, lobe0, lobe1);
    assert(Near(result[0], 0.31968376f));
    assert(Near(result[1], 0.31476282f));
    assert(Near(result[2], 0.23989459f));
}

void TestZeroDirectionalLobeLeavesLowFrequencyLobe()
{
    const std::array<float, 4> white{1.0f, 1.0f, 1.0f, 1.0f};
    const std::array<float, 4> lobe0{0.125f, 0.25f, 0.5f, 0.0f};
    const std::array<float, 4> lobe1{0.0f, 0.0f, 0.0f, 1.0f};
    const std::array<float, 3> result =
        WebRenderer_EvaluateSecondaryDirectionalLighting(
            white, white, lobe0, lobe1);
    assert(Near(result[0], 0.125f));
    assert(Near(result[1], 0.25f));
    assert(Near(result[2], 0.5f));
}
} // namespace

int main()
{
    TestNativeSecondaryDirectionalDecode();
    TestZeroDirectionalLobeLeavesLowFrequencyLobe();
    return 0;
}
