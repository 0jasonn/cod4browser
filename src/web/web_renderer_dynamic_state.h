#pragma once

#include <array>
#include <cstring>

// Local to one dynamic camera pass and one shader program. Batch values and
// matrix contents must remain immutable for that pass. Reset after any direct
// GL override (sun query/sprite), and never retain this across frames/contexts.
// The batch template uses the same fields in retained and portable commands;
// it avoids copying canonical identities or owning another material model.
template<typename Batch>
class WebRendererDynamicDrawState
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
