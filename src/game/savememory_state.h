#pragma once

#include <game/savememory.h>

#include <cstdint>

struct alignas(4) SaveMemoryGlob
{
    SaveGame *committedGameSave;
    SaveGame *currentGameSave;
    SaveGame game0;
    SaveGame game1;
    SaveGame demo;
    unsigned char buffer0[1572864];
    unsigned char buffer1[1572864];
    unsigned char buffer2[1572864];
    int recentLoadTime;
    bool isCommitForced;
};

#if INTPTR_MAX == INT32_MAX
static_assert(sizeof(SaveMemoryGlob) == 4722040);
#endif

extern SaveMemoryGlob saveMemoryGlob;
