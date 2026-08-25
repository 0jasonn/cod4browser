#include <web/web_renderer_lighting.h>

#include <gfx_d3d/gfx_world_types.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

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

void TestNativeSecondaryDirectionalNormalDecode()
{
    const std::array<float, 4> base{0.8f, 0.6f, 0.4f, 1.0f};
    const std::array<float, 4> vertex{0.5f, 0.75f, 1.0f, 1.0f};
    const std::array<float, 4> lobe0{0.2f, 0.3f, 0.4f, 0.75f};
    const std::array<float, 4> lobe1{0.6f, 0.4f, 0.2f, 0.25f};
    const std::array<float, 4> normal{0.0f, 0.8f, 0.0f, 0.9f};
    const std::array<float, 3> result =
        WebRenderer_EvaluateSecondaryDirectionalNormalLighting(
            base, vertex, lobe0, lobe1, normal);
    assert(Near(result[0], 0.11716839f));
    assert(Near(result[1], 0.12160789f));
    assert(Near(result[2], 0.09902341f));
}

void TestNeutralSlopeNormalMatchesNonNormalDirectionalDecode()
{
    const std::array<float, 4> base{0.8f, 0.6f, 0.4f, 1.0f};
    const std::array<float, 4> vertex{0.5f, 0.75f, 1.0f, 1.0f};
    const std::array<float, 4> lobe0{0.2f, 0.3f, 0.4f, 0.5f};
    const std::array<float, 4> lobe1{0.6f, 0.4f, 0.2f, 0.5f};
    const std::array<float, 4> neutralNormal{
        0.0f, 2.06451607f / 4.06451607f, 0.0f, 2.08f / 4.08f};
    const auto expected = WebRenderer_EvaluateSecondaryDirectionalLighting(
        base, vertex, lobe0, lobe1);
    const auto actual =
        WebRenderer_EvaluateSecondaryDirectionalNormalLighting(
            base, vertex, lobe0, lobe1, neutralNormal);
    for (std::size_t channel = 0u; channel < actual.size(); ++channel)
        assert(Near(actual[channel], expected[channel]));
}

void TestCanonicalFrameFogTransition()
{
    std::array<WebRendererFog, 5> states{};
    states[3] = {0, 0, 0xff102030u, 100.0f, 0.001f};
    states[4] = {1000, 2000, 0xff506070u, 300.0f, 0.003f};
    WebRendererFog frame{};
    assert(WebRenderer_UpdateFrameFog(states, 1u, 1500, frame));
    assert(frame.color == 0xff304050u);
    assert(Near(frame.fogStart, 200.0f));
    assert(Near(frame.density, 0.002f));
    assert(states[2].color == frame.color);

    assert(WebRenderer_UpdateFrameFog(states, 1u, 2000, frame));
    assert(frame.color == states[4].color);
    assert(Near(frame.fogStart, 300.0f));
    assert(Near(frame.density, 0.003f));

    frame = states[4];
    assert(!WebRenderer_UpdateFrameFog(states, 0u, 2000, frame));
    assert(frame.color == 0u && frame.density == 0.0f);
    assert(!WebRenderer_UpdateFrameFog(states, 5u, 2000, frame));
}

void TestNativeExponentialFogVisibility()
{
    WebRendererFog fog{};
    fog.fogStart = 100.0f;
    fog.density = 0.69314718056f / 200.0f;
    assert(Near(WebRenderer_EvaluateFogVisibility(50.0f, fog), 1.0f));
    assert(Near(WebRenderer_EvaluateFogVisibility(100.0f, fog), 1.0f));
    assert(Near(WebRenderer_EvaluateFogVisibility(300.0f, fog), 0.5f));
    fog.density = 0.0f;
    assert(Near(WebRenderer_EvaluateFogVisibility(10000.0f, fog), 1.0f));
}

void TestNativeColorManipulationConstants()
{
    WebRendererFilmSettings film{};
    film.enabled = true;
    film.brightness = 0.1f;
    film.contrast = 1.2f;
    film.desaturation = 0.25f;
    film.invert = false;
    film.tintDark[0] = 0.5f;
    film.tintDark[1] = 0.75f;
    film.tintDark[2] = 1.0f;
    film.tintLight[0] = 1.0f;
    film.tintLight[1] = 0.75f;
    film.tintLight[2] = 0.5f;
    WebRendererColorManipulationConstants constants{};
    assert(WebRenderer_CalculateColorManipulationConstants(film, constants));
    assert(constants.enabled);
    assert(Near(constants.colorBias[0], 0.0f));
    assert(Near(constants.colorBias[1], 0.0f));
    assert(Near(constants.colorBias[2], 0.0f));
    assert(Near(constants.colorBias[3], 3.0f));
    assert(Near(constants.colorTintBase[0], 0.15f));
    assert(Near(constants.colorTintBase[1], 0.225f));
    assert(Near(constants.colorTintBase[2], 0.3f));
    assert(Near(constants.colorTintDelta[0], 0.15f));
    assert(Near(constants.colorTintDelta[1], 0.0f));
    assert(Near(constants.colorTintDelta[2], -0.15f));

    film.invert = true;
    assert(WebRenderer_CalculateColorManipulationConstants(film, constants));
    assert(Near(constants.colorBias[0], 1.0f));
    assert(Near(constants.colorTintBase[0], -0.15f));
    assert(Near(constants.colorTintDelta[0], -0.15f));

    film.enabled = false;
    assert(WebRenderer_CalculateColorManipulationConstants(film, constants));
    assert(!constants.enabled);
    assert(Near(constants.colorBias[3], 4095.0f));
    assert(Near(constants.colorTintBase[0], 1.0f / 4096.0f));
}

void TestNativeGlowConstants()
{
    WebRendererGlowSettings glow{};
    glow.enabled = true;
    glow.bloomCutoff = 0.812775f;
    glow.bloomDesaturation = 0.0f;
    glow.bloomIntensity = 0.5f;
    glow.radius = 9.2f;
    WebRendererGlowConstants constants{};
    assert(WebRenderer_CalculateGlowConstants(glow, constants));
    assert(constants.enabled);
    assert(Near(constants.bloomCutoff, 0.812775f));
    assert(Near(constants.bloomCutoffRescale,
        1.0f / (1.0f - 0.812775f)));
    assert(Near(constants.bloomIntensity, 0.5f));
    assert(Near(constants.radius, 9.2f));

    glow.enabled = false;
    assert(WebRenderer_CalculateGlowConstants(glow, constants));
    assert(!constants.enabled);
    assert(Near(constants.bloomCutoffRescale, 0.0f));

    glow.enabled = true;
    glow.bloomCutoff = 1.0f;
    assert(WebRenderer_CalculateGlowConstants(glow, constants));
    assert(!constants.enabled);
    glow.bloomCutoff = std::numeric_limits<float>::quiet_NaN();
    assert(!WebRenderer_CalculateGlowConstants(glow, constants));
}

void TestNativeDepthOfFieldBlur()
{
    WebRendererDepthOfFieldSettings dof{};
    dof.enabled = true;
    dof.viewModelStart = 2.0f;
    dof.viewModelEnd = 8.0f;
    dof.nearStart = 10.0f;
    dof.nearEnd = 60.0f;
    dof.farStart = 1000.0f;
    dof.farEnd = 7000.0f;
    dof.nearBlur = 6.0f;
    dof.farBlur = 1.8f;
    assert(WebRenderer_ValidateDepthOfFieldSettings(dof));
    assert(Near(WebRenderer_EvaluateDepthOfFieldBlur(
        dof, 2.0f, true), 6.0f));
    assert(Near(WebRenderer_EvaluateDepthOfFieldBlur(
        dof, 8.0f, true), 0.0f));
    assert(Near(WebRenderer_EvaluateDepthOfFieldBlur(
        dof, 10.0f, false), 6.0f));
    assert(Near(WebRenderer_EvaluateDepthOfFieldBlur(
        dof, 60.0f, false), 0.0f));
    assert(Near(WebRenderer_EvaluateDepthOfFieldBlur(
        dof, 4000.0f, false), 0.9f));
    assert(Near(WebRenderer_EvaluateDepthOfFieldBlur(
        dof, 7000.0f, false), 1.8f));
    dof.nearBlur = 3.0f;
    assert(!WebRenderer_ValidateDepthOfFieldSettings(dof));
}

void TestNativeDisplayGammaRamp()
{
    assert(Near(WebRenderer_EvaluateDisplayGamma(0.0f, 0.8f), 0.0f));
    assert(Near(WebRenderer_EvaluateDisplayGamma(1.0f, 0.8f), 1.0f));
    assert(Near(WebRenderer_EvaluateDisplayGamma(0.5f, 1.0f), 0.5f));
    assert(Near(WebRenderer_EvaluateDisplayGamma(0.5f, 0.8f),
        0.42044821f));
    assert(Near(WebRenderer_EvaluateDisplayGamma(-1.0f, 0.8f), 0.0f));
    assert(Near(WebRenderer_EvaluateDisplayGamma(2.0f, 0.8f), 1.0f));
    assert(Near(WebRenderer_GetDisplayGammaExponent(
        true, false, 0.8f), 1.25f));
    assert(Near(WebRenderer_GetDisplayGammaExponent(
        false, false, 0.8f), 1.0f));
    assert(Near(WebRenderer_GetDisplayGammaExponent(
        true, true, 0.8f), 1.0f));
    assert(Near(WebRenderer_GetDisplayGammaExponent(
        true, false, 0.0f), 1.0f));
}

void TestNativeDxt5NormalDecode()
{
    const std::array<float, 3> identity =
        WebRenderer_DecodeDxt5Normal({0.0f, 0.5f, 0.0f, 0.5f});
    assert(Near(identity[0], 0.0f));
    assert(Near(identity[1], 0.0f));
    assert(Near(identity[2], 1.0f));

    const std::array<float, 3> tilted =
        WebRenderer_DecodeDxt5Normal({0.0f, 0.75f, 0.0f, 0.75f});
    assert(Near(tilted[0], 0.5f));
    assert(Near(tilted[1], 0.5f));
    assert(Near(tilted[2], std::sqrt(0.5f)));

    const std::array<float, 3> saturated =
        WebRenderer_DecodeDxt5Normal({0.0f, 1.0f, 0.0f, 1.0f});
    assert(Near(saturated[2], 0.0f));
}

struct GridFixture
{
    std::uint16_t rowDataStart[2]{0u, 4u};
    std::uint8_t rawRows[32]{};
    GfxLightGridEntry entries[8]{};
    GfxLightGridColors colors[8]{};
    GfxLightGrid grid{};

    GridFixture()
    {
        struct Row
        {
            std::uint16_t colStart;
            std::uint16_t colCount;
            std::uint16_t zStart;
            std::uint16_t zCount;
            std::uint32_t firstEntry;
        };
        for (std::uint32_t rowIndex = 0u; rowIndex < 2u; ++rowIndex)
        {
            Row row{4096u, 2u, 2048u, 2u, rowIndex * 4u};
            std::memcpy(rawRows + rowIndex * 16u, &row, sizeof(row));
            rawRows[rowIndex * 16u + 12u] = 2u;
            rawRows[rowIndex * 16u + 13u] = 2u;
            rawRows[rowIndex * 16u + 14u] = 0u;
        }
        for (std::uint32_t index = 0u; index < 8u; ++index)
        {
            entries[index].colorsIndex = static_cast<std::uint16_t>(index);
            entries[index].primaryLightIndex = 1u;
            for (auto &rgb : colors[index].rgb)
            {
                rgb[0] = static_cast<std::uint8_t>(index * 32u);
                rgb[1] = static_cast<std::uint8_t>(index * 32u);
                rgb[2] = static_cast<std::uint8_t>(index * 32u);
            }
        }
        grid.sunPrimaryLightIndex = 1u;
        grid.mins[0] = 4096u;
        grid.mins[1] = 4096u;
        grid.mins[2] = 2048u;
        grid.maxs[0] = 4097u;
        grid.maxs[1] = 4097u;
        grid.maxs[2] = 2049u;
        grid.rowAxis = 0u;
        grid.colAxis = 1u;
        grid.rowDataStart = rowDataStart;
        grid.rawRowDataSize = sizeof(rawRows);
        grid.rawRowData = rawRows;
        grid.entryCount = 8u;
        grid.entries = entries;
        grid.colorCount = 8u;
        grid.colors = colors;
    }
};

void TestNativeLightGridRleAndFixedPointBlend()
{
    GridFixture fixture;
    const float samplePosition[3]{16.0f, 16.0f, 32.0f};
    WebRendererModelLightingSample sample{};
    assert(WebRenderer_EvaluateModelLighting(
        fixture.grid, samplePosition, 1u, nullptr, sample));
    assert(!sample.extrapolated);
    assert(sample.primaryLightIndex == 1u);
    assert(sample.primaryLightWeight == 255u);
    for (const auto &rgb : sample.colors.rgb)
    {
        assert(rgb[0] == 112u);
        assert(rgb[1] == 112u);
        assert(rgb[2] == 112u);
    }
}

void TestNativeModelLightingAtlasLayoutAndCoordinates()
{
    WebRendererModelLightingAtlas atlas;
    assert(WebRenderer_InitializeModelLightingAtlas(65u, atlas));
    assert(atlas.width == 256u && atlas.height == 8u && atlas.depth == 4u);
    WebRendererLightGridColors colors{};
    for (std::uint32_t index = 0u; index < 56u; ++index)
    {
        colors.rgb[index][0] = static_cast<std::uint8_t>(index);
        colors.rgb[index][1] = static_cast<std::uint8_t>(index + 1u);
        colors.rgb[index][2] = static_cast<std::uint8_t>(index + 2u);
    }
    assert(WebRenderer_SetModelLightingAtlasEntry(
        atlas, 64u, colors, 77u));
    float coordinates[3]{};
    WebRenderer_GetModelLightingCoordinates(atlas, 64u, coordinates);
    assert(Near(coordinates[0], 2.0f / 256.0f));
    assert(Near(coordinates[1], 6.0f / 8.0f));
    assert(Near(coordinates[2], 0.5f));
    const std::size_t base =
        ((0u * atlas.height + 4u) * atlas.width + 0u) * 4u;
    assert(atlas.pixels[base + 0u] == 0u);
    assert(atlas.pixels[base + 1u] == 1u);
    assert(atlas.pixels[base + 2u] == 2u);
    assert(atlas.pixels[base + 3u] == 77u);
    const std::size_t duplicated =
        ((1u * atlas.height + 5u) * atlas.width + 1u) * 4u;
    assert(atlas.pixels[duplicated] == 0u);
}

void TestNativeModelLightingShaderComposition()
{
    const std::array<float, 4> base{0.8f, 0.5f, 0.25f, 1.0f};
    const std::array<float, 4> vertex{0.5f, 0.75f, 1.0f, 1.0f};
    const std::array<float, 3> lighting{0.25f, 0.5f, 0.75f};
    const auto result = WebRenderer_EvaluateModelLightingShader(
        base, vertex, lighting);
    assert(Near(result[0], 0.2f));
    assert(Near(result[1], 0.375f));
    assert(Near(result[2], 0.375f));
}

void TestNativeAmbientProbeLightingComposition()
{
    const std::array<float, 4> base{0.8f, 0.5f, 0.25f, 1.0f};
    const std::array<float, 4> vertex{0.5f, 0.75f, 1.0f, 1.0f};
    const std::array<float, 4> lighting{0.25f, 0.5f, 0.75f, 0.5f};
    const std::array<float, 3> sun{0.4f, 0.2f, 0.1f};
    const auto result = WebRenderer_EvaluateAmbientProbeLightingShader(
        base, vertex, lighting, sun);
    assert(Near(result[0], 0.28f));
    assert(Near(result[1], 0.4125f));
    assert(Near(result[2], 0.3875f));
}

void TestModelLightingAtlasEntryCopyAcrossHeights()
{
    WebRendererModelLightingAtlas source;
    WebRendererModelLightingAtlas destination;
    assert(WebRenderer_InitializeModelLightingAtlas(1u, source));
    assert(WebRenderer_InitializeModelLightingAtlas(65u, destination));
    WebRendererLightGridColors colors{};
    for (auto &rgb : colors.rgb)
    {
        rgb[0] = 17u;
        rgb[1] = 34u;
        rgb[2] = 51u;
    }
    assert(WebRenderer_SetModelLightingAtlasEntry(
        source, 0u, colors, 68u));
    assert(WebRenderer_CopyModelLightingAtlasEntries(
        source, destination, 64u));
    const std::size_t copied =
        ((0u * destination.height + 4u) * destination.width + 0u) * 4u;
    assert(destination.pixels[copied + 0u] == 17u);
    assert(destination.pixels[copied + 1u] == 34u);
    assert(destination.pixels[copied + 2u] == 51u);
    assert(destination.pixels[copied + 3u] == 68u);
    assert(!WebRenderer_CopyModelLightingAtlasEntries(
        source, destination, 65u));
}
} // namespace

int main()
{
    TestNativeSecondaryDirectionalDecode();
    TestZeroDirectionalLobeLeavesLowFrequencyLobe();
    TestNativeSecondaryDirectionalNormalDecode();
    TestNeutralSlopeNormalMatchesNonNormalDirectionalDecode();
    TestCanonicalFrameFogTransition();
    TestNativeExponentialFogVisibility();
    TestNativeColorManipulationConstants();
    TestNativeGlowConstants();
    TestNativeDepthOfFieldBlur();
    TestNativeDisplayGammaRamp();
    TestNativeDxt5NormalDecode();
    TestNativeLightGridRleAndFixedPointBlend();
    TestNativeModelLightingAtlasLayoutAndCoordinates();
    TestNativeModelLightingShaderComposition();
    TestNativeAmbientProbeLightingComposition();
    TestModelLightingAtlasEntryCopyAcrossHeights();
    return 0;
}
