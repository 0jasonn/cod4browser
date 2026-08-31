#pragma once

#include <array>
#include <cstdint>
#include <cstring>

// Sun depth uses one projection, no culling, and no material inputs for opaque
// world triangles. Join only adjacent opaque index ranges; never bridge an
// omitted caster or an alpha-tested range. Camera visibility is irrelevant here.
template<typename Batches, typename IsOpaque, typename Draw>
std::uint32_t WebRenderer_ForEachSunShadowRange(
    const Batches &batches, IsOpaque isOpaque, Draw draw)
{
    const typename Batches::value_type *pending = nullptr;
    std::uint32_t count = 0u;
    std::uint32_t merged = 0u;
    for (const auto &batch : batches)
    {
        if (!batch.castsSunShadow || batch.indexCount == 0u)
        {
            if (pending) draw(*pending, pending->firstIndex, count);
            pending = nullptr;
            continue;
        }
        if (pending && isOpaque(*pending) && isOpaque(batch) &&
            std::uint64_t(pending->firstIndex) + count == batch.firstIndex &&
            batch.indexCount <= UINT32_MAX - count)
        {
            count += batch.indexCount;
            ++merged;
            continue;
        }
        if (pending) draw(*pending, pending->firstIndex, count);
        pending = &batch;
        count = batch.indexCount;
    }
    if (pending) draw(*pending, pending->firstIndex, count);
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
        // are pass-wide. Compare float bits conservatively, including -0.
        return !previous ||
            (previous->materialIdentity != nullptr) != (next.materialIdentity != nullptr) ||
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

private:
    const float *projection_ = nullptr;
    const Batch *material_ = nullptr;
    std::array<bool, 9> features_{};
    bool featuresKnown_ = false;
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
