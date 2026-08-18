#pragma once

#include <cstdint>

struct GfxConfiguration
{
    std::uint32_t maxClientViews;
    std::uint32_t entCount;
    std::uint32_t entnumNone;
    std::uint32_t entnumOrdinaryEnd;
    std::int32_t threadContextCount;
    std::int32_t critSectCount;
    const char *codeFastFileName;
    const char *uiFastFileName;
    const char *commonFastFileName;
    const char *localizedCodeFastFileName;
    const char *localizedCommonFastFileName;
    const char *modFastFileName;
};
