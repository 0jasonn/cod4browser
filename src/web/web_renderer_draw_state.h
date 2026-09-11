#pragma once

#include <gfx_d3d/gfx_draw_surf_types.h>

#include <array>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <utility>
#include <vector>

#ifndef KISAK_WEB_BATCH_SHADOW_ERRORS
#define KISAK_WEB_BATCH_SHADOW_ERRORS 1
#endif

// A family publishes one readiness flag, so none of its partitions may be
// sampled until both CPU submission and the GL check succeed. No GL allocation
// or other error-consuming operation may run between these draws and check.
template<bool BatchErrors, typename Draw, typename Check>
bool WebRenderer_DrawShadowFamily(std::size_t count, Draw draw, Check check)
{
    if (count == 0u) return false;
    bool submitted = true;
    for (std::size_t partition = 0u; partition < count; ++partition)
    {
        submitted = draw(partition);
        if constexpr (!BatchErrors)
        {
            const bool checked = check();
            if (!checked) return false;
        }
        if (!submitted) break;
    }
    if constexpr (BatchErrors)
    {
        // Even a failed CPU submission can follow earlier GL work. Do not
        // short-circuit this check or leak that error into the next family.
        const bool checked = check();
        return submitted && checked;
    }
    return submitted;
}

// Sun and spot maps have no consumers between their submissions. Validate
// the whole shadow phase before either becomes sampleable by the camera.
template<typename Check>
void WebRenderer_CompleteShadowMaps(bool &sunReady, bool &spotReady, Check check)
{
    const bool clean = check(); // Drain even when CPU submission failed.
    sunReady &= clean;
    spotReady &= clean;
}

inline unsigned WebRenderer_PrimarySortKey(std::uint64_t packed) noexcept
{
    GfxDrawSurf draw{};
    draw.packed = packed;
    return draw.fields.primarySortKey;
}

inline bool WebRenderer_MatchesPrimarySortKey(
    std::uint64_t packed, unsigned key) noexcept
{
    return WebRenderer_PrimarySortKey(packed) == key;
}

// R_MergeAndEmitDrawSurfLists merges families by primary key only. Each
// callback emits that key in BSP/static/entity/FX order; the lists retain
// their own full-key or AUTO/DECAL append ordering within the band.
template<typename DrawBand>
void WebRenderer_ForEachPrimarySortKey(std::uint64_t keys, DrawBand drawBand)
{
    while (keys)
    {
        drawBand(static_cast<unsigned>(std::countr_zero(keys)));
        keys &= keys - 1u;
    }
}

// R_RenderDrawSurfListMaterial runs each pass over the complete material
// sublist. Camera visibility can split that sublist into several ranges.
// Never interleave its two passes per range: overlap makes that observable.
template<typename BatchFor, typename HasSecondPass, typename DrawGroup>
void WebRenderer_ForEachMaterialPassGroup(std::size_t count, BatchFor batchFor,
    HasSecondPass hasSecondPass, DrawGroup drawGroup)
{
    for (std::size_t begin = 0; begin < count;)
    {
        std::size_t end = begin + 1;
        const auto &first = batchFor(begin);
        const bool second = hasSecondPass(first);
        if (second)
            while (end < count)
            {
                const auto &next = batchFor(end);
                if (next.materialIdentity != first.materialIdentity ||
                    next.techniqueType != first.techniqueType ||
                    next.sourceKind != first.sourceKind ||
                    next.primaryLightIndex != first.primaryLightIndex ||
                    next.depthHack != first.depthHack || !hasSecondPass(next)) break;
                ++end;
            }
        drawGroup(begin, end, 0u);
        if (second) drawGroup(begin, end, 1u);
        begin = end;
    }
}

// Preserve every non-reorderable entry as an anchor. Stable-sort only the
// contiguous runs in the same caller-selected sort group (zero is an anchor).
template<typename Entries, typename BatchFor, typename CanReorder,
    typename SortKey>
void WebRenderer_BuildStableDrawOrder(
    const Entries &entries, BatchFor batchFor, CanReorder canReorder,
    SortKey sortKey, std::vector<std::uint32_t> &order)
{
    order.resize(entries.size());
    std::iota(order.begin(), order.end(), 0u);
    // Read each key once into contiguous storage. The original index breaks
    // ties, preserving stable order without sorting through batch pointers.
    using Key = decltype(sortKey(batchFor(entries[0])));
    std::vector<std::pair<Key, std::uint32_t>> keyed;
    std::size_t runBegin = 0u;
    while (runBegin < order.size())
    {
        const auto group = canReorder(batchFor(entries[order[runBegin]]));
        if (!group)
        {
            ++runBegin;
            continue;
        }
        std::size_t runEnd = runBegin + 1u;
        while (runEnd < order.size() &&
            canReorder(batchFor(entries[order[runEnd]])) == group)
            ++runEnd;
        if (runEnd - runBegin > 1u)
        {
            keyed.clear();
            keyed.reserve(runEnd - runBegin);
            for (std::size_t i = runBegin; i < runEnd; ++i)
                keyed.emplace_back(sortKey(batchFor(entries[i])), static_cast<std::uint32_t>(i));
            std::sort(keyed.begin(), keyed.end());
            for (std::size_t i = 0; i < keyed.size(); ++i)
                order[runBegin + i] = keyed[i].second;
        }
        runBegin = runEnd;
    }
}

// Join adjacent index ranges only when the caller proves identical shadow
// inputs. Skipped casters always break a run. This never uses camera visibility.
template<typename Entries, typename BatchFor, typename IsCaster,
    typename CanMerge, typename Draw>
std::uint32_t WebRenderer_ForEachShadowRange(
    const Entries &entries, BatchFor batchFor, IsCaster isCaster,
    CanMerge canMerge, Draw draw)
{
    const typename Entries::value_type *pending = nullptr;
    std::uint32_t count = 0u;
    std::uint32_t merged = 0u;
    const auto flush = [&]() {
        if (pending) draw(*pending, batchFor(*pending),
            batchFor(*pending).firstIndex, count);
    };
    for (const auto &entry : entries)
    {
        const auto &batch = batchFor(entry);
        if (!isCaster(entry, batch) || batch.indexCount == 0u)
        {
            flush();
            pending = nullptr;
            continue;
        }
        if (pending && canMerge(*pending, entry) &&
            std::uint64_t(batchFor(*pending).firstIndex) + count == batch.firstIndex &&
            batch.indexCount <= UINT32_MAX - count)
        {
            count += batch.indexCount;
            ++merged;
            continue;
        }
        flush();
        pending = &entry;
        count = batch.indexCount;
    }
    flush();
    return merged;
}

// Sun depth uses one projection, no culling, and no material inputs for opaque
// world triangles. Alpha-tested ranges keep their own material inputs.
template<typename Batches, typename IsOpaque, typename Draw>
std::uint32_t WebRenderer_ForEachSunShadowRange(
    const Batches &batches, IsOpaque isOpaque, Draw draw)
{
    return WebRenderer_ForEachShadowRange(batches,
        [](const auto &batch) -> const auto & { return batch; },
        [](const auto &, const auto &batch) { return batch.castsSunShadow; },
        [&isOpaque](const auto &a, const auto &b) { return isOpaque(a) && isOpaque(b); },
        [&draw](const auto &, const auto &batch, std::uint32_t first, std::uint32_t count) {
            draw(batch, first, count);
        });
}

// Visible surface spans may cover only part of a retained batch. Preserve
// those holes while retaining the opaque cross-batch merge used by sun depth.
template<typename Ranges, typename Batches, typename IsCaster,
    typename IsOpaque, typename Draw>
std::uint32_t WebRenderer_ForEachWorldSunShadowRange(
    const Ranges &ranges, const Batches &batches, IsCaster isCaster,
    IsOpaque isOpaque, Draw draw)
{
    const typename Ranges::value_type *pending = nullptr;
    std::uint32_t count = 0u;
    std::uint32_t merged = 0u;
    const auto flush = [&]() {
        if (pending)
            draw(batches[pending->batchIndex], pending->firstIndex, count);
    };
    for (const auto &range : ranges)
    {
        const auto &batch = batches[range.batchIndex];
        if (!isCaster(batch) || range.indexCount == 0u)
        {
            flush();
            pending = nullptr;
            continue;
        }
        if (pending && isOpaque(batches[pending->batchIndex]) &&
            isOpaque(batch) &&
            std::uint64_t(pending->firstIndex) + count == range.firstIndex &&
            range.indexCount <= UINT32_MAX - count)
        {
            count += range.indexCount;
            ++merged;
            continue;
        }
        flush();
        pending = &range;
        count = range.indexCount;
    }
    flush();
    return merged;
}

// Local to one draw pass and one shader program. Batch values and
// matrix contents must remain immutable for that pass. Reset after any direct
// GL override (sun query/sprite), and never retain this across frames/contexts.
// The batch template uses the same fields in retained and portable commands;
// it avoids copying canonical identities or owning another material model.
template<typename Batch>
class WebRendererDrawState
{
public:
    void Reset() noexcept { *this = {}; }

    bool NeedsProjection(const float *matrix) noexcept
    {
        if (projection_ == matrix) return false;
        projection_ = matrix;
        return true;
    }

    bool NeedsMaterial(const Batch &next) noexcept
    {
        const Batch *previous = material_;
        material_ = &next;
        // All per-batch inputs read by ApplyWorldMaterialState. AA settings
        // are pass-wide. Canonical material/technique identity also owns shader
        // arguments: equal blend bits can use different flare falloff/eye offset.
        // Compare float bits conservatively, including -0.
        return !previous ||
            previous->materialIdentity != next.materialIdentity ||
            previous->techniqueType != next.techniqueType ||
            previous->depthHack != next.depthHack ||
            previous->stateBits[0] != next.stateBits[0] ||
            previous->stateBits[1] != next.stateBits[1] ||
            previous->technique != next.technique ||
            previous->sourceKind != next.sourceKind ||
            previous->ambientProbeLighting != next.ambientProbeLighting ||
            std::memcmp(previous->falloffParms, next.falloffParms, sizeof(next.falloffParms)) != 0 ||
            std::memcmp(previous->falloffBeginColor, next.falloffBeginColor, sizeof(next.falloffBeginColor)) != 0 ||
            std::memcmp(previous->falloffEndColor, next.falloffEndColor, sizeof(next.falloffEndColor)) != 0;
    }

    // fog, fallback, texture, lightmap, model lighting, detail, normal,
    // specular, primary light. Both lightmap uniforms share one flag.
    bool NeedsFeatures(const std::array<bool, 9> &next) noexcept
    {
        if (featuresKnown_ && features_ == next) return false;
        features_ = next;
        featuresKnown_ = true;
        return true;
    }

    // Light records, shadow slots/fades and lighting dvars are immutable for
    // this pass. Index zero denotes no local-light constants. Material setup
    // never writes these uniforms or the spot-shadow texture unit.
    bool NeedsLighting(std::uint8_t index, std::uint8_t primaryMode,
        std::uint8_t sunMode) noexcept
    {
        const std::array<std::uint8_t, 3> next{index, primaryMode, sunMode};
        if (lightingKnown_ && lighting_ == next) return false;
        lighting_ = next;
        lightingKnown_ = true;
        return true;
    }

    // Different materials still upload their shader arguments, but can share
    // the same raster state. AA settings are immutable within this pass.
    bool NeedsRaster(std::uint32_t state0, std::uint32_t state1,
        bool canonical, bool floatZ) noexcept
    {
        const std::array<std::uint32_t, 3> next{state0, state1,
            std::uint32_t(canonical) | (std::uint32_t(floatZ) << 1u)};
        if (rasterKnown_ && raster_ == next) return false;
        raster_ = next;
        rasterKnown_ = true;
        return true;
    }

private:
    const float *projection_ = nullptr;
    const Batch *material_ = nullptr;
    std::array<bool, 9> features_{};
    bool featuresKnown_ = false;
    std::array<std::uint8_t, 3> lighting_{};
    bool lightingKnown_ = false;
    std::array<std::uint32_t, 3> raster_{};
    bool rasterKnown_ = false;
};

// One pass, with immutable VAO attribute enables/divisors. Reset after binding
// another VAO or directly overriding instance attributes. A changed count
// changes the draw, not the attribute pointers; only its first instance matters.
class WebRendererInstanceState
{
public:
    void Reset() noexcept { valid_ = false; }
    bool NeedsRange(std::uint32_t vertexArray, std::uint32_t buffer,
        std::uint32_t offset) noexcept
    {
        if (valid_ && vertexArray_ == vertexArray && buffer_ == buffer && offset_ == offset)
            return false;
        vertexArray_ = vertexArray;
        buffer_ = buffer;
        offset_ = offset;
        valid_ = true;
        return true;
    }

private:
    std::uint32_t vertexArray_ = 0u, buffer_ = 0u, offset_ = 0u;
    bool valid_ = false;
};

// One shadow partition. Texture binding, instance ranges and caster membership
// remain per-draw; this only avoids repeating the same two uniforms/cull mode.
class WebRendererShadowState
{
public:
    bool NeedsAlpha(int alphaTest, bool samplesTexture) noexcept
    {
        if (alphaKnown_ && alphaTest_ == alphaTest && samplesTexture_ == samplesTexture)
            return false;
        alphaKnown_ = true;
        alphaTest_ = alphaTest;
        samplesTexture_ = samplesTexture;
        return true;
    }

    bool NeedsCull(std::uint32_t stateBits0) noexcept
    {
        const std::uint32_t cull = stateBits0 & 0xc000u;
        if (cull_ == cull) return false;
        cull_ = cull;
        return true;
    }

private:
    int alphaTest_ = 0;
    bool samplesTexture_ = false;
    bool alphaKnown_ = false;
    std::uint32_t cull_ = UINT32_MAX;
};
