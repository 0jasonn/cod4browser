#include <universal/q_shared.h>
#if defined(KISAK_DB_SYNC_FILE_TEST) && !defined(KISAK_WEB)
#define KISAK_WEB 1
#endif
#include "database.h"
#include <script/scr_stringlist.h>
#if defined(KISAK_WEB)
#include <database/db_runtime_prefix.h>
#endif


void __cdecl Load_Stream(bool atStreamStart, uint8_t *ptr, int32_t size)
{
#if defined(KISAK_WEB)
    if (size < 0 || (atStreamStart && !DB_RuntimeStreamCanRead(
        static_cast<std::size_t>(size))))
    {
        DB_RuntimeGeneratedFailure("stream/read outside canonical block");
        return;
    }
#endif
    iassert(atStreamStart == (ptr == DB_GetStreamPos()));
    if (atStreamStart && size)
    {
        if (g_streamPosIndex - 1 < 3)
        {
            if (g_streamPosIndex == 1)
            {
                memset(ptr, 0, size);
            }
            else
            {
                bcassert(g_streamDelayIndex, ARRAY_COUNT(g_streamDelayArray));
                g_streamDelayArray[g_streamDelayIndex].ptr = ptr;
                g_streamDelayArray[g_streamDelayIndex++].size = size;
            }
        }
        else
        {
            DB_LoadXFileData(ptr, size);
        }
        DB_IncStreamPos(size);
    }
}

void __cdecl Load_DelayStream()
{
    uint32_t index; // [esp+4h] [ebp-8h]

    for (index = 0; index < g_streamDelayIndex; ++index)
        DB_LoadXFileData((unsigned char*)g_streamDelayArray[index].ptr, g_streamDelayArray[index].size);
}

void __cdecl DB_ConvertOffsetToAlias(uint32_t *data)
{
    uint32_t offset; // [esp+0h] [ebp-8h]

    offset = *data;
    iassert((offset && (offset != -1) && (offset != -2)));
#if defined(KISAK_WEB)
    const std::uint32_t blockIndex = (offset - 1) >> 28;
    const std::uint32_t blockOffset = (offset - 1) & 0xFFFFFFF;
    if (blockIndex >= 9 || !g_streamZoneMem->blocks[blockIndex].data ||
        blockOffset > g_streamZoneMem->blocks[blockIndex].size ||
        sizeof(std::uint32_t) > g_streamZoneMem->blocks[blockIndex].size - blockOffset)
    {
        DB_RuntimeGeneratedFailure("stream/invalid alias offset");
        return;
    }
#endif
    *data = *(uint32_t *)&g_streamZoneMem->blocks[(offset - 1) >> 28].data[(offset - 1) & 0xFFFFFFF];
}

void __cdecl DB_ConvertOffsetToPointer(uint32_t *data)
{
#if defined(KISAK_WEB)
    const std::uint32_t offset = *data;
    const std::uint32_t blockIndex = (offset - 1) >> 28;
    const std::uint32_t blockOffset = (offset - 1) & 0xFFFFFFF;
    if (!offset || offset == UINT32_MAX || offset == UINT32_MAX - 1u ||
        blockIndex >= 9 || !g_streamZoneMem->blocks[blockIndex].data ||
        blockOffset >= g_streamZoneMem->blocks[blockIndex].size)
    {
        DB_RuntimeGeneratedFailure("stream/invalid pointer offset");
        return;
    }
#endif
    *data = (uint32_t)&g_streamZoneMem->blocks[(uint32_t)(*data - 1) >> 28].data[(*data - 1) & 0xFFFFFFF];
}

void __cdecl Load_XStringCustom(char **str)
{
    uint8_t *pos; // [esp+0h] [ebp-8h]
    char *s; // [esp+4h] [ebp-4h]

    s = *str;
    for (pos = (uint8_t *)*str; ; ++pos)
    {
#if defined(KISAK_WEB)
        if (!DB_RuntimeStreamCanRead(1u))
        {
            DB_RuntimeGeneratedFailure("stream/truncated string");
            return;
        }
#endif
        DB_LoadXFileData(pos, 1u);
#if defined(KISAK_WEB)
        if (DB_RuntimeGeneratedLoadFailed()) return;
#endif
        if (!*pos)
            break;
    }
    DB_IncStreamPos(pos - (uint8_t *)s + 1);
}

void __cdecl Load_TempStringCustom(char **str)
{
    const char * string; // [esp+0h] [ebp-4h]

    Load_XStringCustom(str);
    if (*str)
        string = (const char*)SL_GetString(*str, 4u); // KISAKTODO: this seems way wrong but it's what the decomp is showing
    else
        string= 0;
    *str = (char *)string;
}

