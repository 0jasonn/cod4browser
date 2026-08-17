#include <universal/q_shared.h>
#if defined(KISAK_DB_SYNC_FILE_TEST) && !defined(KISAK_WEB)
#define KISAK_WEB 1
#endif
#include "database.h"
#if defined(KISAK_WEB)
#include <database/db_runtime_prefix.h>
#endif

#include <cstdint>

uint32_t g_streamDelayIndex;
XBlock * g_streamBlocks;
uint8_t *g_streamPosArray[9];
StreamDelayInfo g_streamDelayArray[4096];
uint32_t g_streamPosIndex;
XZoneMemory *g_streamZoneMem;
uint8_t *g_streamPos;

StreamPosInfo g_streamPosStack[64];
uint32_t g_streamPosStackIndex;

// --- file-local forward declarations (moved out of database.h) ---
static void __cdecl DB_SetStreamIndex(uint32_t index);

void __cdecl DB_InitStreams(XZoneMemory *zoneMem)
{
    int32_t i; // [esp+0h] [ebp-4h]

    g_streamZoneMem = zoneMem;
    g_streamPos = zoneMem->blocks[0].data;
    g_streamPosIndex = 0;
    g_streamDelayIndex = 0;
    g_streamPosStackIndex = 0;
    for (i = 0; i < 9; ++i)
        g_streamPosArray[i] = zoneMem->blocks[i].data;
#if defined(KISAK_WEB)
    DB_RuntimeTraceStreamsInitialized(0, 0);
#endif
}

void __cdecl DB_PushStreamPos(uint32_t index)
{
    iassert(index < ARRAY_COUNT(g_streamPosArray));
    iassert(g_streamPosIndex < ARRAY_COUNT(g_streamPosArray));
    iassert(g_streamPosStackIndex < ARRAY_COUNT(g_streamPosStack));

    g_streamPosStack[g_streamPosStackIndex].index = g_streamPosIndex;
    DB_SetStreamIndex(index);

    g_streamPosStack[g_streamPosStackIndex++].pos = g_streamPos;
}

void __cdecl DB_CloneStreamData(uint8_t *destStart)
{
    if (destStart)
        memcpy(
            &destStart[g_streamPosArray[g_streamPosIndex] - g_streamZoneMem->blocks[g_streamPosIndex].data],
            g_streamPosArray[g_streamPosIndex],
            g_streamPos - g_streamPosArray[g_streamPosIndex]);
}

void __cdecl DB_SetStreamIndex(uint32_t index)
{
    if (index != g_streamPosIndex)
    {
        if (g_streamPosIndex == 7)
        {
            DB_CloneStreamData(g_streamZoneMem->lockedVertexData);
        }
        else if (g_streamPosIndex == 8)
        {
            DB_CloneStreamData(g_streamZoneMem->lockedIndexData);
        }
        iassert(index < arr_cnt(g_streamPosArray));
        g_streamPosArray[g_streamPosIndex] = g_streamPos;
        g_streamPosIndex = index;
        g_streamPos = g_streamPosArray[index];
    }
}

void __cdecl DB_PopStreamPos()
{
    vassert(g_streamPosStackIndex > 0, "(g_streamPosStackIndex = %d)", g_streamPosStackIndex);
    --g_streamPosStackIndex;
    if (!g_streamPosIndex)
        g_streamPos = g_streamPosStack[g_streamPosStackIndex].pos;
    DB_SetStreamIndex(g_streamPosStack[g_streamPosStackIndex].index);
}

uint8_t *__cdecl DB_GetStreamPos()
{
    return g_streamPos;
}

uint8_t *__cdecl DB_AllocStreamPos(int32_t alignment)
{
    iassert(g_streamPos);
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(g_streamPos);
    const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment);
    uint8_t *aligned = reinterpret_cast<uint8_t *>((address + mask) & ~mask);
#if defined(KISAK_WEB)
    const XBlock &block = g_streamZoneMem->blocks[g_streamPosIndex];
    if (alignment < 0 || aligned < block.data ||
        static_cast<std::size_t>(aligned - block.data) > block.size)
    {
        DB_RuntimeGeneratedFailure("stream/alignment outside canonical block");
        return g_streamPos;
    }
#endif
    g_streamPos = aligned;
    return g_streamPos;
}

void __cdecl DB_IncStreamPos(int32_t size)
{
    iassert(g_streamPos);
#if defined(KISAK_WEB)
    if (size < 0 || !DB_RuntimeStreamCanRead(static_cast<std::size_t>(size)))
    {
        DB_RuntimeGeneratedFailure("stream/increment outside canonical block");
        return;
    }
#endif
    iassert(g_streamPos + size <= g_streamZoneMem->blocks[g_streamPosIndex].data + g_streamZoneMem->blocks[g_streamPosIndex].size);

    g_streamPos += size;
}

const void **__cdecl DB_InsertPointer()
{
    const void **pData; // [esp+0h] [ebp-4h]

    DB_PushStreamPos(4);
    pData = (const void **)DB_AllocStreamPos(3);
#if defined(KISAK_WEB)
    if (DB_RuntimeGeneratedLoadFailed() || !DB_RuntimeStreamCanRead(4u))
    {
        DB_RuntimeGeneratedFailure("stream/insert pointer outside canonical block");
        DB_PopStreamPos();
        return nullptr;
    }
#endif
    DB_IncStreamPos(4);
    DB_PopStreamPos();
    return pData;
}

