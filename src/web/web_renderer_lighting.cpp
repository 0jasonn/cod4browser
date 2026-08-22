#include <web/web_renderer_lighting.h>

#include <gfx_d3d/gfx_world_types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace
{
constexpr float GRID_ORIGIN = -131072.0f;
constexpr std::uint32_t MODEL_LIGHTING_WIDTH = 256u;
constexpr std::uint32_t MODEL_LIGHTING_DEPTH = 4u;
constexpr std::uint32_t ENTRIES_PER_ROW = 64u;

struct LightGridRow
{
    std::uint16_t colStart;
    std::uint16_t colCount;
    std::uint16_t zStart;
    std::uint16_t zCount;
    std::uint32_t firstEntry;
};
static_assert(sizeof(LightGridRow) == 12u);

bool ValidGrid(const GfxLightGrid &grid) noexcept
{
    return grid.rowAxis < 2u && grid.colAxis < 2u &&
        grid.rowAxis != grid.colAxis && grid.rowDataStart &&
        grid.rawRowData && grid.entries && grid.colors &&
        grid.rawRowDataSize >= sizeof(LightGridRow) &&
        grid.entryCount != 0u && grid.colorCount != 0u;
}

const GfxLightGridEntry *EntryAt(
    const GfxLightGrid &grid, std::int64_t index) noexcept
{
    return index >= 0 && static_cast<std::uint64_t>(index) < grid.entryCount
        ? &grid.entries[index] : nullptr;
}

bool ReadRow(
    const GfxLightGrid &grid, std::uint32_t rowIndex,
    LightGridRow &row, const std::uint8_t *&rle,
    const std::uint8_t *&end) noexcept
{
    const std::uint32_t rowCount =
        static_cast<std::uint32_t>(grid.maxs[grid.rowAxis]) + 1u -
        static_cast<std::uint32_t>(grid.mins[grid.rowAxis]);
    if (rowIndex >= rowCount || grid.rowDataStart[rowIndex] == 0xffffu)
        return false;
    const std::uint64_t offset =
        static_cast<std::uint64_t>(grid.rowDataStart[rowIndex]) * 4u;
    if (offset + sizeof(LightGridRow) > grid.rawRowDataSize)
        return false;
    const std::uint8_t *bytes = grid.rawRowData + offset;
    std::memcpy(&row, bytes, sizeof(row));
    if (row.firstEntry >= grid.entryCount)
        return false;
    rle = bytes + sizeof(row);
    end = grid.rawRowData + grid.rawRowDataSize;
    return true;
}

bool ReadRleBlock(
    const std::uint8_t *rle, const std::uint8_t *end,
    bool wideZ, std::uint32_t &runLength,
    std::uint32_t &sampleCount, std::uint32_t &baseZ,
    std::size_t &blockBytes) noexcept
{
    if (!rle || rle + 2u > end) return false;
    runLength = rle[0];
    sampleCount = rle[1];
    if (runLength == 0u) return false;
    blockBytes = sampleCount == 0u ? 2u : (wideZ ? 4u : 3u);
    if (rle + blockBytes > end) return false;
    baseZ = sampleCount == 0u ? 0u : rle[2];
    if (sampleCount != 0u && wideZ)
        baseZ |= static_cast<std::uint32_t>(rle[3]) << 8u;
    return true;
}

void ClearQuad(const GfxLightGridEntry **entries) noexcept
{
    std::fill_n(entries, 4u, nullptr);
}

void GetSampleEntryQuad(
    const GfxLightGrid &grid, const std::uint32_t pos[3],
    const GfxLightGridEntry **entries,
    std::uint32_t &defaultGridEntry) noexcept
{
    ClearQuad(entries);
    const std::int64_t rowIndexSigned =
        static_cast<std::int64_t>(pos[grid.rowAxis]) -
        grid.mins[grid.rowAxis];
    if (rowIndexSigned < 0 ||
        rowIndexSigned > std::numeric_limits<std::uint32_t>::max())
        return;
    LightGridRow row{};
    const std::uint8_t *rle = nullptr;
    const std::uint8_t *end = nullptr;
    if (!ReadRow(grid, static_cast<std::uint32_t>(rowIndexSigned),
            row, rle, end))
        return;

    std::int64_t colIndex =
        static_cast<std::int64_t>(pos[grid.colAxis]) - row.colStart;
    const std::int64_t z = static_cast<std::int64_t>(pos[2]) - row.zStart;
    if (colIndex < -1 || colIndex >= row.colCount ||
        z < -1 || z >= row.zCount)
    {
        if (z < 0) defaultGridEntry = 0u;
        return;
    }

    const bool wideZ = row.zCount > 255u;
    std::int64_t firstBlockEntry = row.firstEntry;
    if (colIndex == -1)
    {
        std::uint32_t run = 0u;
        std::uint32_t count = 0u;
        std::uint32_t baseZ = 0u;
        std::size_t blockBytes = 0u;
        if (!ReadRleBlock(
                rle, end, wideZ, run, count, baseZ, blockBytes))
            return;
        const std::int64_t localZ = z - baseZ;
        entries[2] = localZ >= 0 && localZ < count
            ? EntryAt(grid, firstBlockEntry + localZ) : nullptr;
        entries[3] = localZ + 1 >= 0 && localZ + 1 < count
            ? EntryAt(grid, firstBlockEntry + localZ + 1) : nullptr;
        if (z < baseZ) defaultGridEntry = 0u;
        return;
    }

    while (true)
    {
        std::uint32_t run = 0u;
        std::uint32_t count = 0u;
        std::uint32_t baseZ = 0u;
        std::size_t blockBytes = 0u;
        if (!ReadRleBlock(
                rle, end, wideZ, run, count, baseZ, blockBytes))
            return;
        if (colIndex < run)
        {
            if (count != 0u)
            {
                if (z < baseZ) defaultGridEntry = 0u;
                const std::int64_t localZ = z - baseZ;
                const std::int64_t lookup = firstBlockEntry +
                    colIndex * count + localZ;
                entries[0] = localZ >= 0 && localZ < count
                    ? EntryAt(grid, lookup) : nullptr;
                entries[1] = localZ + 1 >= 0 && localZ + 1 < count
                    ? EntryAt(grid, lookup + 1) : nullptr;
                if (colIndex + 1 < run)
                {
                    entries[2] = localZ >= 0 && localZ < count
                        ? EntryAt(grid, lookup + count) : nullptr;
                    entries[3] = localZ + 1 >= 0 && localZ + 1 < count
                        ? EntryAt(grid, lookup + count + 1) : nullptr;
                    return;
                }
            }
            else if (colIndex + 1 < run)
            {
                return;
            }

            // The second column crosses into the next RLE block.
            const std::int64_t nextFirst = firstBlockEntry +
                static_cast<std::int64_t>(run) * count;
            const std::uint8_t *next = rle + blockBytes;
            std::uint32_t nextRun = 0u;
            std::uint32_t nextCount = 0u;
            std::uint32_t nextBaseZ = 0u;
            std::size_t nextBytes = 0u;
            if (!ReadRleBlock(next, end, wideZ, nextRun, nextCount,
                    nextBaseZ, nextBytes) || nextCount == 0u)
                return;
            const std::int64_t nextLocalZ = z - nextBaseZ;
            entries[2] = nextLocalZ >= 0 && nextLocalZ < nextCount
                ? EntryAt(grid, nextFirst + nextLocalZ) : nullptr;
            entries[3] = nextLocalZ + 1 >= 0 &&
                    nextLocalZ + 1 < nextCount
                ? EntryAt(grid, nextFirst + nextLocalZ + 1) : nullptr;
            return;
        }
        colIndex -= run;
        firstBlockEntry += static_cast<std::int64_t>(run) * count;
        rle += blockBytes;
    }
}

void CornerPosition(
    const GfxLightGrid &grid, const std::uint32_t pos[3],
    std::uint32_t cornerIndex, float out[3]) noexcept
{
    out[0] = static_cast<float>(pos[0]) * 32.0f + GRID_ORIGIN;
    out[1] = static_cast<float>(pos[1]) * 32.0f + GRID_ORIGIN;
    out[2] = static_cast<float>(pos[2]) * 64.0f + GRID_ORIGIN;
    if ((cornerIndex & 4u) != 0u) out[grid.rowAxis] += 32.0f;
    if ((cornerIndex & 2u) != 0u) out[grid.colAxis] += 32.0f;
    if ((cornerIndex & 1u) != 0u) out[2] += 64.0f;
}

std::uint8_t Lookup(
    const GfxLightGrid &grid, const float samplePosition[3],
    const WebRendererModelLightingCallbacks *callbacks,
    float weights[8], const GfxLightGridEntry *entries[8],
    std::uint32_t &defaultGridEntry,
    std::uint32_t pos[3]) noexcept
{
    pos[0] = static_cast<std::uint32_t>(
        (static_cast<std::int64_t>(std::floor(samplePosition[0])) +
            0x20000ll) >> 5u);
    pos[1] = static_cast<std::uint32_t>(
        (static_cast<std::int64_t>(std::floor(samplePosition[1])) +
            0x20000ll) >> 5u);
    pos[2] = static_cast<std::uint32_t>(
        (static_cast<std::int64_t>(std::floor(samplePosition[2])) +
            0x20000ll) >> 6u);
    const float rowLerp =
        (samplePosition[grid.rowAxis] - GRID_ORIGIN) * (1.0f / 32.0f) -
        static_cast<float>(pos[grid.rowAxis]);
    const float colLerp =
        (samplePosition[grid.colAxis] - GRID_ORIGIN) * (1.0f / 32.0f) -
        static_cast<float>(pos[grid.colAxis]);
    const float zLerp =
        (samplePosition[2] - GRID_ORIGIN) * (1.0f / 64.0f) -
        static_cast<float>(pos[2]);
    const float col0z0 = (1.0f - colLerp) * (1.0f - zLerp);
    weights[0] = (1.0f - rowLerp) * col0z0;
    weights[4] = rowLerp * col0z0;
    const float col0z1 = (1.0f - colLerp) * zLerp;
    weights[1] = (1.0f - rowLerp) * col0z1;
    weights[5] = rowLerp * col0z1;
    const float col1z0 = colLerp * (1.0f - zLerp);
    weights[2] = (1.0f - rowLerp) * col1z0;
    weights[6] = rowLerp * col1z0;
    const float col1z1 = colLerp * zLerp;
    weights[3] = (1.0f - rowLerp) * col1z1;
    weights[7] = rowLerp * col1z1;

    defaultGridEntry = 1u;
    GetSampleEntryQuad(grid, pos, entries, defaultGridEntry);
    ++pos[grid.rowAxis];
    GetSampleEntryQuad(grid, pos, entries + 4u, defaultGridEntry);
    --pos[grid.rowAxis];

    bool honorSuppression = false;
    float bestPrimaryWeight = 0.0f;
    std::uint8_t primaryLightIndex = 0u;
    for (std::uint32_t corner = 0u; corner < 8u; ++corner)
    {
        const GfxLightGridEntry *entry = entries[corner];
        if (!entry) continue;
        if (weights[corner] < 0.001f)
        {
            entries[corner] = nullptr;
            continue;
        }
        bool suppressed = false;
        if ((entry->needsTrace & (1u << corner)) != 0u && callbacks &&
            callbacks->sampleVisible)
        {
            float gridPosition[3]{};
            CornerPosition(grid, pos, corner, gridPosition);
            suppressed = !callbacks->sampleVisible(
                samplePosition, gridPosition, callbacks->context);
        }
        if (suppressed && honorSuppression)
        {
            entries[corner] = nullptr;
            continue;
        }
        if (!suppressed && !honorSuppression)
        {
            honorSuppression = true;
            bestPrimaryWeight = weights[corner];
            primaryLightIndex = entry->primaryLightIndex;
            std::fill(entries, entries + corner, nullptr);
            continue;
        }
        const std::uint8_t candidate = entry->primaryLightIndex;
        const bool replace = primaryLightIndex == 0u ||
            (candidate != 0u && (primaryLightIndex == 255u ||
                (candidate != 255u &&
                    weights[corner] > bestPrimaryWeight)));
        if (replace)
        {
            bestPrimaryWeight = weights[corner];
            primaryLightIndex = candidate;
        }
    }
    return primaryLightIndex;
}

void BlendColors(
    const GfxLightGrid &grid,
    const std::uint16_t indices[8], const float weights[8],
    std::uint32_t count, float totalWeight,
    WebRendererLightGridColors &output) noexcept
{
    if (count == 1u)
    {
        std::memcpy(output.rgb, grid.colors[indices[0]].rgb,
            sizeof(output.rgb));
        return;
    }
    std::uint16_t fixed[8]{};
    std::uint32_t largest = 0u;
    int sum = 0;
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        fixed[index] = static_cast<std::uint16_t>(
            weights[index] * (256.0f / totalWeight) + 0.5f);
        sum += fixed[index];
        if (fixed[index] > fixed[largest]) largest = index;
    }
    fixed[largest] = static_cast<std::uint16_t>(
        static_cast<int>(fixed[largest]) + 256 - sum);
    for (std::size_t component = 0u; component < 168u; ++component)
    {
        std::uint32_t accumulated = 0u;
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            const auto *rgb = reinterpret_cast<const std::uint8_t *>(
                grid.colors[indices[index]].rgb);
            accumulated += fixed[index] * rgb[component];
        }
        auto *destination = reinterpret_cast<std::uint8_t *>(output.rgb);
        destination[component] = static_cast<std::uint8_t>(
            (accumulated + 127u) >> 8u);
    }
}

constexpr std::array<std::uint8_t, 64> MODEL_LIGHTING_SAMPLE_MAP{{
     0,  1,  2,  3,  4,  5,  6,  7,
     8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20,  0,  3, 21,
    22, 12, 15, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 40, 43, 33,
    34, 52, 55, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55,
}};

std::uint32_t NextPowerOfTwo(std::uint32_t value) noexcept
{
    std::uint32_t result = 1u;
    while (result < value && result <= (1u << 30u)) result <<= 1u;
    return result;
}
} // namespace

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

bool WebRenderer_EvaluateModelLighting(
    const GfxLightGrid &lightGrid,
    const float samplePosition[3],
    std::uint32_t nonSunPrimaryLightIndex,
    const WebRendererModelLightingCallbacks *callbacks,
    WebRendererModelLightingSample &sample) noexcept
{
    if (!samplePosition || !ValidGrid(lightGrid) ||
        !std::isfinite(samplePosition[0]) ||
        !std::isfinite(samplePosition[1]) ||
        !std::isfinite(samplePosition[2]))
        return false;

    float cornerWeights[8]{};
    const GfxLightGridEntry *cornerEntries[8]{};
    std::uint32_t defaultGridEntry = 1u;
    std::uint32_t gridPosition[3]{};
    std::uint32_t primaryLightIndex = Lookup(
        lightGrid, samplePosition, callbacks, cornerWeights,
        cornerEntries, defaultGridEntry, gridPosition);
    if (primaryLightIndex == 255u)
        primaryLightIndex = lightGrid.sunPrimaryLightIndex & 0xffu;
    else if (lightGrid.hasLightRegions &&
        primaryLightIndex != lightGrid.sunPrimaryLightIndex)
        primaryLightIndex = nonSunPrimaryLightIndex;

    std::uint16_t colorsIndices[8]{};
    float colorsWeights[8]{};
    std::uint32_t colorsCount = 0u;
    float totalWeight = 0.0f;
    float primaryVisibleWeight = 0.0f;
    float primaryOccludedWeight = 0.0f;
    for (std::uint32_t corner = 0u; corner < 8u; ++corner)
    {
        const GfxLightGridEntry *entry = cornerEntries[corner];
        if (!entry || entry->colorsIndex >= lightGrid.colorCount) continue;
        const float weight = cornerWeights[corner];
        if (entry->primaryLightIndex == primaryLightIndex)
            primaryVisibleWeight += weight;
        else if (entry->primaryLightIndex == 0u ||
            (entry->primaryLightIndex == 255u && primaryLightIndex != 0u))
        {
            bool influences = false;
            if (callbacks && callbacks->primaryLightInfluences)
            {
                float cornerPosition[3]{};
                CornerPosition(lightGrid, gridPosition, corner,
                    cornerPosition);
                influences = callbacks->primaryLightInfluences(
                    primaryLightIndex, cornerPosition, callbacks->context);
            }
            if (influences) primaryOccludedWeight += weight;
        }
        totalWeight += weight;
        std::uint32_t existing = 0u;
        while (existing < colorsCount &&
            colorsIndices[existing] != entry->colorsIndex) ++existing;
        if (existing < colorsCount)
            colorsWeights[existing] += weight;
        else if (colorsCount < 8u)
        {
            colorsIndices[colorsCount] = entry->colorsIndex;
            colorsWeights[colorsCount] = weight;
            ++colorsCount;
        }
    }

    WebRendererModelLightingSample replacement{};
    replacement.primaryLightIndex = static_cast<std::uint8_t>(
        std::min<std::uint32_t>(primaryLightIndex, 255u));
    if (colorsCount == 0u || totalWeight <= 0.0f)
    {
        replacement.extrapolated = true;
        const std::uint32_t index = defaultGridEntry < lightGrid.colorCount
            ? defaultGridEntry : lightGrid.colorCount - 1u;
        std::memcpy(replacement.colors.rgb, lightGrid.colors[index].rgb,
            sizeof(replacement.colors.rgb));
        replacement.primaryLightIndex = static_cast<std::uint8_t>(
            lightGrid.sunPrimaryLightIndex & 0xffu);
        replacement.primaryLightWeight = 255u;
    }
    else
    {
        BlendColors(lightGrid, colorsIndices, colorsWeights,
            colorsCount, totalWeight, replacement.colors);
        float visible = 0.0f;
        if (primaryLightIndex != 0u)
        {
            visible = primaryOccludedWeight == 0.0f
                ? (primaryVisibleWeight != 0.0f ? 1.0f : 0.0f)
                : primaryVisibleWeight /
                    (primaryVisibleWeight + primaryOccludedWeight);
        }
        replacement.primaryLightWeight = static_cast<std::uint8_t>(
            std::clamp(visible * 255.0f + 0.5f, 0.0f, 255.0f));
    }
    sample = replacement;
    return true;
}

bool WebRenderer_InitializeModelLightingAtlas(
    std::uint32_t entryCount,
    WebRendererModelLightingAtlas &atlas)
{
    if (entryCount == 0u || entryCount > 65536u) return false;
    const std::uint32_t usedRows =
        (entryCount + ENTRIES_PER_ROW - 1u) / ENTRIES_PER_ROW;
    const std::uint32_t height = NextPowerOfTwo(usedRows * 4u);
    if (height < 4u || height > 4096u) return false;
    const std::uint64_t byteCount =
        static_cast<std::uint64_t>(MODEL_LIGHTING_WIDTH) * height *
        MODEL_LIGHTING_DEPTH * 4u;
    if (byteCount > std::numeric_limits<std::size_t>::max()) return false;
    WebRendererModelLightingAtlas replacement;
    replacement.width = MODEL_LIGHTING_WIDTH;
    replacement.height = height;
    replacement.depth = MODEL_LIGHTING_DEPTH;
    replacement.entryCount = entryCount;
    try
    {
        replacement.pixels.assign(static_cast<std::size_t>(byteCount), 0u);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    atlas = std::move(replacement);
    return true;
}

bool WebRenderer_SetModelLightingAtlasEntry(
    WebRendererModelLightingAtlas &atlas,
    std::uint32_t entryIndex,
    const WebRendererLightGridColors &colors,
    std::uint8_t primaryLightWeight) noexcept
{
    if (entryIndex >= atlas.entryCount || atlas.width != 256u ||
        atlas.depth != 4u || atlas.height < 4u || atlas.pixels.empty())
        return false;
    const std::uint32_t baseX = (entryIndex & 63u) * 4u;
    const std::uint32_t baseY = (entryIndex >> 6u) * 4u;
    if (baseX + 4u > atlas.width || baseY + 4u > atlas.height)
        return false;
    for (std::uint32_t sampleIndex = 0u; sampleIndex < 64u; ++sampleIndex)
    {
        const std::uint32_t x = baseX + (sampleIndex & 3u);
        const std::uint32_t y = baseY + ((sampleIndex >> 2u) & 3u);
        const std::uint32_t z = sampleIndex >> 4u;
        const std::size_t offset =
            ((static_cast<std::size_t>(z) * atlas.height + y) *
                atlas.width + x) * 4u;
        const std::uint8_t source = MODEL_LIGHTING_SAMPLE_MAP[sampleIndex];
        atlas.pixels[offset + 0u] = colors.rgb[source][0];
        atlas.pixels[offset + 1u] = colors.rgb[source][1];
        atlas.pixels[offset + 2u] = colors.rgb[source][2];
        atlas.pixels[offset + 3u] = primaryLightWeight;
    }
    return true;
}

bool WebRenderer_SetModelGroundLightingAtlasEntry(
    WebRendererModelLightingAtlas &atlas,
    std::uint32_t entryIndex,
    std::uint32_t groundLighting) noexcept
{
    WebRendererLightGridColors colors{};
    const std::uint8_t red = static_cast<std::uint8_t>(
        (groundLighting >> 16u) & 0xffu);
    const std::uint8_t green = static_cast<std::uint8_t>(
        (groundLighting >> 8u) & 0xffu);
    const std::uint8_t blue = static_cast<std::uint8_t>(
        groundLighting & 0xffu);
    for (auto &rgb : colors.rgb)
    {
        rgb[0] = red;
        rgb[1] = green;
        rgb[2] = blue;
    }
    const std::uint8_t alpha = static_cast<std::uint8_t>(
        groundLighting >> 24u);
    return WebRenderer_SetModelLightingAtlasEntry(
        atlas, entryIndex, colors, alpha);
}

void WebRenderer_GetModelLightingCoordinates(
    const WebRendererModelLightingAtlas &atlas,
    std::uint32_t entryIndex,
    float coordinates[3]) noexcept
{
    if (!coordinates || entryIndex >= atlas.entryCount ||
        atlas.width == 0u || atlas.height == 0u || atlas.depth == 0u)
    {
        if (coordinates) std::fill_n(coordinates, 3u, 0.0f);
        return;
    }
    coordinates[0] = static_cast<float>((entryIndex & 63u) * 4u + 2u) /
        static_cast<float>(atlas.width);
    coordinates[1] = static_cast<float>((entryIndex >> 6u) * 4u + 2u) /
        static_cast<float>(atlas.height);
    coordinates[2] = 0.5f;
}

std::array<float, 3> WebRenderer_EvaluateModelLightingShader(
    const std::array<float, 4> &base,
    const std::array<float, 4> &vertexColor,
    const std::array<float, 3> &sampledLighting) noexcept
{
    return {{
        base[0] * vertexColor[0] * sampledLighting[0] * 2.0f,
        base[1] * vertexColor[1] * sampledLighting[1] * 2.0f,
        base[2] * vertexColor[2] * sampledLighting[2] * 2.0f,
    }};
}
