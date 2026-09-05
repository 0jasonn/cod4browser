#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>

// Canonical primary-light history, shared by native selection and WebGL.
struct GfxShadowedLightEntry
{
    std::uint8_t shadowableLightIndex;
    bool isFadingOut;
    float fade;
};

struct GfxShadowedLightHistory
{
    std::uint32_t shadowableLightWasUsed[8];
    GfxShadowedLightEntry entries[4];
    std::uint32_t entryCount;
    std::uint32_t lastUpdateTime;
};
static_assert(sizeof(GfxShadowedLightEntry) == 8u);
static_assert(sizeof(GfxShadowedLightHistory) == 72u);

inline void R_FadeShadowHistory(GfxShadowedLightHistory &history,
    const std::uint32_t used[8], float fadeDelta)
{
    assert(history.entryCount <= 4u);
    for (unsigned i = 0; i < history.entryCount;)
    {
        auto &entry = history.entries[i];
        assert(entry.fade > 0.0f);
        const unsigned light = entry.shadowableLightIndex;
        if (!(used[light >> 5u] & (1u << (light & 31u))))
        {
            entry = history.entries[--history.entryCount];
            continue;
        }
        if (entry.isFadingOut)
        {
            entry.fade -= fadeDelta;
            if (entry.fade < 0.01f)
            {
                entry = history.entries[--history.entryCount];
                continue;
            }
        }
        else entry.isFadingOut = true;
        ++i;
    }
}

inline void R_AddShadowHistory(GfxShadowedLightHistory &history,
    unsigned light, float fadeDelta, unsigned limit)
{
    assert(light < 256u && limit <= 4u && history.entryCount <= 4u);
    for (unsigned i = 0; i < history.entryCount; ++i)
    {
        auto &entry = history.entries[i];
        if (entry.shadowableLightIndex != light) continue;
        entry.isFadingOut = false;
        entry.fade = (std::min)(entry.fade + fadeDelta, 1.0f);
        return;
    }
    if (history.entryCount >= limit) return;
    const bool wasUsed = (history.shadowableLightWasUsed[light >> 5u] &
        (1u << (light & 31u))) != 0u;
    history.entries[history.entryCount++] = {
        static_cast<std::uint8_t>(light), false, wasUsed ? fadeDelta : 1.0f};
}
