#include <web/web_renderer_lighting.h>

#include <gfx_d3d/gfx_world_types.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
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
} // namespace

int main()
{
    TestNativeSecondaryDirectionalDecode();
    TestZeroDirectionalLobeLeavesLowFrequencyLobe();
    TestNativeLightGridRleAndFixedPointBlend();
    TestNativeModelLightingAtlasLayoutAndCoordinates();
    TestNativeModelLightingShaderComposition();
    return 0;
}
