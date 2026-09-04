#include <web/web_renderer_lighting.h>

#include <gfx_d3d/gfx_world_types.h>
#include <gfx_d3d/r_dynamiclights_core.h>

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

void TestFractionalShadowComparisonReconstruction()
{
    const std::array<float, 4> diagonal{0.0f, 1.0f, 0.0f, 1.0f};
    assert(Near(WebRenderer_EvaluateBilinearShadowVisibility(
        diagonal, 0.0f, 0.25f), 0.0f));
    assert(Near(WebRenderer_EvaluateBilinearShadowVisibility(
        diagonal, 0.25f, 0.75f), 0.25f));
    assert(Near(WebRenderer_EvaluateBilinearShadowVisibility(
        diagonal, 0.75f, 0.25f), 0.75f));
    assert(Near(WebRenderer_EvaluateBilinearShadowVisibility(
        diagonal, 1.0f, 0.75f), 1.0f));

    const std::array<float, 4> corner{0.0f, 0.0f, 0.0f, 1.0f};
    assert(Near(WebRenderer_EvaluateBilinearShadowVisibility(
        corner, 0.5f, 0.5f), 0.25f));
    assert(Near(WebRenderer_EvaluateBilinearShadowVisibility(
        corner, -1.0f, 2.0f), 0.0f));
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

void TestNativeAverageLightingSelectionAndQuantization()
{
    GridFixture fixture;
    const float center[3]{16.0f, 16.0f, 32.0f};
    const float sun[3]{0.5f, 1.0f, 2.0f};
    std::array<std::uint8_t, 4> color{};
    assert(WebRenderer_EvaluateAverageLighting(fixture.grid, center, sun, nullptr, color.data()));
    // Native RB: palette mean 112, plus sun * (255 * .5), truncated/clamped.
    assert((color == std::array<std::uint8_t, 4>{175, 239, 255, 255}));
    // Half the corners carry no primary light. Ambient remains blended, but
    // native primary weight is now 128/255 rather than model-query alpha 255.
    for (unsigned i = 4; i < 8; ++i) fixture.entries[i].primaryLightIndex = 0;
    assert(WebRenderer_EvaluateAverageLighting(fixture.grid, center, sun, nullptr, color.data()));
    assert((color == std::array<std::uint8_t, 4>{144, 176, 240, 255}));
    // Non-sun primary corners are excluded, including their palette values.
    for (unsigned i = 4; i < 8; ++i) fixture.entries[i].primaryLightIndex = 2;
    assert(WebRenderer_EvaluateAverageLighting(fixture.grid, center, sun, nullptr, color.data()));
    assert((color == std::array<std::uint8_t, 4>{111, 175, 255, 255}));
    for (auto &entry : fixture.entries) entry.primaryLightIndex = 2;
    assert(WebRenderer_EvaluateAverageLighting(fixture.grid, center, sun, nullptr, color.data()));
    assert((color == std::array<std::uint8_t, 4>{32, 64, 128, 255}));
    const auto previous = color;
    const float invalid[3]{std::numeric_limits<float>::quiet_NaN(), 0, 0};
    assert(!WebRenderer_EvaluateAverageLighting(fixture.grid, invalid, sun, nullptr, color.data()));
    assert(color == previous);
    for (auto &entry : fixture.entries) entry.primaryLightIndex = 1;
    fixture.entries[0].colorsIndex = 8;
    assert(!WebRenderer_EvaluateAverageLighting(fixture.grid, center, sun, nullptr, color.data()));
    assert(color == previous);
}

void TestCanonicalEmptyLightGrid()
{
    // Synthetic R_InitEmptyLightGrid representation; no retail data.
    std::uint16_t missingRow = 0xffffu;
    GfxLightGridColors colors[2]{};
    std::memset(&colors[0], 37, sizeof(colors[0]));
    std::memset(&colors[1], 81, sizeof(colors[1]));
    GfxLightGrid grid{};
    grid.colAxis = 1u;
    grid.sunPrimaryLightIndex = 1u;
    grid.rowDataStart = &missingRow;
    grid.colors = colors;
    grid.colorCount = 1u;
    const float position[3]{100.0f, 200.0f, 300.0f};
    WebRendererModelLightingSample sample{};
    assert(WebRenderer_EvaluateModelLighting(grid, position, 0u, nullptr, sample));
    assert(sample.extrapolated && sample.primaryLightWeight == 255u);
    // R_ExtrapolateLightingAtPoint: absent default entry 1 selects the last
    // palette and returns no primary light; an existing entry selects sun.
    assert(sample.primaryLightIndex == 0u);
    assert(std::memcmp(sample.colors.rgb, colors[0].rgb, sizeof(colors[0].rgb)) == 0);
    grid.colorCount = 2u;
    assert(WebRenderer_EvaluateModelLighting(grid, position, 0u, nullptr, sample));
    assert(sample.extrapolated && sample.primaryLightIndex == 1u);
    assert(std::memcmp(sample.colors.rgb, colors[1].rgb, sizeof(colors[1].rgb)) == 0);

    const float sun[3]{0.5f, 1.0f, 2.0f};
    std::array<std::uint8_t, 4> average{};
    assert(WebRenderer_EvaluateAverageLighting(grid, position, sun, nullptr, average.data()));
    assert((average == std::array<std::uint8_t, 4>{32, 64, 128, 255}));
    missingRow = 0u; // Missing storage for a purported nonempty row is invalid.
    assert(!WebRenderer_EvaluateModelLighting(grid, position, 0u, nullptr, sample));
    missingRow = 0xffffu;
    grid.colorCount = 0u;
    assert(!WebRenderer_EvaluateModelLighting(grid, position, 0u, nullptr, sample));
    assert(!WebRenderer_EvaluateAverageLighting(grid, position, sun, nullptr, average.data()));
}

void TestSharedTransientLights()
{
    using namespace kisak::dynamic_lights;
    GfxLightDef definition{};
    const float origin[3]{3, 4, 5}, direction[3]{1, 0, 0}, color[3]{0.25f, 0.5f, 1};
    GfxLight omni, spot;
    SetOmni(omni, &definition, origin, 100, color);
    assert(omni.type == 3 && omni.def == &definition && omni.radius == 100);
    assert(omni.origin[0] == 3 && omni.origin[1] == 4 && omni.origin[2] == 5);
    assert(omni.color[0] == 0.25f && omni.color[2] == 1);
    assert(!omni.canUseShadowMap && omni.spotShadowIndex == UINT32_MAX);
    const float offset = SetSpot(spot, &definition, origin, direction, 400,
        color, 36, 196, 0.7f, 14, true);
    assert(std::fabs(offset - 90) < 0.0001f && std::fabs(spot.radius - 490) < 0.0001f);
    assert(std::fabs(spot.origin[0] + 87) < 0.0001f && spot.dir[0] == -1);
    assert(spot.type == 2 && spot.color[0] == 3.5f && spot.color[2] == 14);
    assert(spot.exponent == 1 && spot.canUseShadowMap && spot.spotShadowIndex == UINT32_MAX);
    assert(Near(spot.cosHalfFovOuter, std::cos(std::atan(0.4f))));
    assert(Near(spot.cosHalfFovInner, std::cos(std::atan(0.4f) * 0.7f)));
    std::array<GfxLight, 6> lights{};
    std::array<const GfxLight *, 6> selected{};
    const float view[3]{};
    for (unsigned i = 0; i < lights.size(); ++i)
    {
        const float position[3]{static_cast<float>(i + 1), 0, 0};
        SetOmni(lights[i], &definition, position, 1, color);
        selected[i] = &lights[i];
    }
    lights[5].type = 2; // Spot priority outranks a nearer omni, exactly as native.
    MostImportant(selected.data(), 6, 4, view);
    for (const auto *expected : {&lights[0], &lights[1], &lights[2], &lights[5]})
        assert(std::find(selected.begin(), selected.begin() + 4, expected) != selected.begin() + 4);
    MostImportant(selected.data(), 4, 1, view);
    assert(selected[0] == &lights[5]);

    const float viewAxis[3][3]{
        {1, 0, 0}, {0, -1, 0}, {0, 0, 1}};
    float viewProjection[4][4]{};
    viewProjection[1][0] = -1.0f;
    viewProjection[2][1] = -1.0f;
    viewProjection[0][3] = 1.0f;
    GfxLight scissorLight{};
    scissorLight.origin[0] = 100.0f;
    scissorLight.radius = 10.0f;
    ScissorRect rect;
    assert(ComputeScissorRect(scissorLight, view, viewAxis,
        viewProjection, 10, 20, 200, 100, rect));
    assert(rect.x > 90 && rect.x < 110);
    assert(rect.y > 60 && rect.y < 80);
    assert(rect.width > 0 && rect.width < 30);
    assert(rect.height > 0 && rect.height < 20);
    scissorLight.origin[0] = 0.0f;
    assert(ComputeScissorRect(scissorLight, view, viewAxis,
        viewProjection, 10, 20, 200, 100, rect));
    assert(rect.x == 10 && rect.y == 20 &&
        rect.width == 200 && rect.height == 100);
    scissorLight.radius = 0.0f;
    assert(!ComputeScissorRect(scissorLight, view, viewAxis,
        viewProjection, 10, 20, 200, 100, rect));
}

void TestNativeReceiverDrawSortKey()
{
    using kisak::dynamic_lights::ReceiverDrawSortKey;
    std::uint64_t state = 0x9e3779b97f4a7c15ull;
    for (unsigned sample = 0u; sample < 4096u; ++sample)
    {
        state ^= state << 13u;
        state ^= state >> 7u;
        state ^= state << 17u;
        const std::uint32_t low = static_cast<std::uint32_t>(state);
        std::uint32_t high = static_cast<std::uint32_t>(state >> 32u);
        high = (high & 0xf03fffffu) |
            ((~static_cast<std::uint32_t>((state >> 54u) & 0x3fu) & 0x3fu) << 22u);
        const std::uint64_t nativeKey = low |
            (static_cast<std::uint64_t>(high) << 32u);
        assert(ReceiverDrawSortKey(state) == nativeKey);
    }

    GfxDrawSurf lowerBand{}, higherBand{}, world{}, brush{}, model{};
    lowerBand.fields.primarySortKey = 12u;
    higherBand.fields.primarySortKey = 13u;
    assert(ReceiverDrawSortKey(higherBand.packed) <
        ReceiverDrawSortKey(lowerBand.packed));
    world.fields.primarySortKey = brush.fields.primarySortKey =
        model.fields.primarySortKey = 20u;
    world.fields.surfType = 0u;
    brush.fields.surfType = 6u;
    model.fields.surfType = 7u;
    assert(ReceiverDrawSortKey(world.packed) <
        ReceiverDrawSortKey(brush.packed));
    assert(ReceiverDrawSortKey(brush.packed) <
        ReceiverDrawSortKey(model.packed));
    model.fields.materialSortedIndex = 2u;
    GfxDrawSurf laterMaterial = model;
    laterMaterial.fields.materialSortedIndex = 3u;
    assert(ReceiverDrawSortKey(model.packed) <
        ReceiverDrawSortKey(laterMaterial.packed));

    // Native non-camera builders copy the material, then change only these
    // fields. Exercise every receiver family and packed-field preservation.
    for (unsigned sample = 0u; sample < 1024u; ++sample)
    {
        state ^= state << 13u;
        state ^= state >> 7u;
        state ^= state << 17u;
        for (unsigned type : {2u, 5u, 6u, 7u, 8u, 9u})
        {
            GfxDrawSurf material{}, expected{};
            material.packed = expected.packed = state;
            const unsigned id = sample * 61u;
            const bool depthHack = type >= 7u && (sample & 1u);
            expected.fields.objectId = id;
            expected.fields.surfType = type;
            expected.fields.primarySortKey =
                (material.fields.primarySortKey - depthHack) & 0x3fu;
            assert(kisak::dynamic_lights::ReceiverDrawSurf(
                material, type, id, depthHack).packed == expected.packed);
        }
    }

    // Camera primary-light state must not reverse the material order in a
    // transient-light pass (whose destination alpha makes order observable).
    GfxDrawSurf firstMaterial{}, secondMaterial{};
    firstMaterial.fields.materialSortedIndex = 2u;
    secondMaterial.fields.materialSortedIndex = 3u;
    auto firstCamera = firstMaterial;
    auto secondCamera = secondMaterial;
    firstCamera.fields.primaryLightIndex = 9u;
    secondCamera.fields.primaryLightIndex = 1u;
    firstCamera.fields.reflectionProbeIndex = 7u;
    assert(ReceiverDrawSortKey(firstCamera.packed) >
        ReceiverDrawSortKey(secondCamera.packed));
    assert(ReceiverDrawSortKey(kisak::dynamic_lights::ReceiverDrawSurf(
        firstMaterial, 7u, 0u).packed) <
        ReceiverDrawSortKey(kisak::dynamic_lights::ReceiverDrawSurf(
        secondMaterial, 7u, 0u).packed));
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

void TestNativeSphereReceiverContact()
{
    using namespace kisak::dynamic_lights;
    GfxLight light{};
    light.type = 2;
    light.dir[0] = -1.0f;
    light.radius = 10.0f;
    light.cosHalfFovOuter = 0.8f;
    float planes[6][4];
    assert(ReceiverPlanes(light, 3.0f, planes));
    float center[3]{2.0f, 0.0f, 0.0f};
    assert(SphereInPlanes(planes, center, 1.0f)); // near-plane tangent included
    center[0] = 1.99f;
    assert(!SphereInPlanes(planes, center, 1.0f));
    center[0] = 11.0f;
    assert(SphereInPlanes(planes, center, 1.0f)); // far-plane tangent included
    center[0] = 11.01f;
    assert(!SphereInPlanes(planes, center, 1.0f));
    center[0] = 5.0f; center[1] = 4.0f;
    assert(SphereInPlanes(planes, center, 1.0f));
    center[1] = 6.0f;
    assert(!SphereInPlanes(planes, center, 1.0f));
    center[0] = 12.0f; center[1] = 0.0f;
    assert(SpheresIntersect(center, 2.0f, light.origin, light.radius));
    assert(!SpheresIntersect(center, 1.99f, light.origin, light.radius));
    center[0] = 0.0f;
    assert(SpheresIntersect(center, 0.0f, light.origin, 0.0f));
}

int main()
{
    TestNativeSphereReceiverContact();
    TestNativeReceiverDrawSortKey();
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
    TestFractionalShadowComparisonReconstruction();
    TestNativeLightGridRleAndFixedPointBlend();
    TestNativeAverageLightingSelectionAndQuantization();
    TestCanonicalEmptyLightGrid();
    TestSharedTransientLights();
    TestNativeModelLightingAtlasLayoutAndCoordinates();
    TestNativeModelLightingShaderComposition();
    TestNativeAmbientProbeLightingComposition();
    TestModelLightingAtlasEntryCopyAcrossHeights();
    return 0;
}
