#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <qcommon/cm_types.h>

#include <cstdint>
#include <limits>

MapEnts *varMapEnts = nullptr;
MapEnts **varMapEntsPtr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical MapEnts loader requires the IW3 32-bit ABI");
static_assert(sizeof(MapEnts) == 12u);

void Load_MapEnts(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varMapEnts),
        sizeof(MapEnts));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varMapEnts->name;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed() && varMapEnts->entityString)
    {
        if (varMapEnts->numEntityChars < 0 ||
            !DB_RuntimeStreamCanRead(static_cast<std::size_t>(
                varMapEnts->numEntityChars)))
        {
            DB_RuntimeGeneratedFailure("MapEnts/entity string length");
        }
        else
        {
            varMapEnts->entityString = reinterpret_cast<char *>(
                DB_AllocStreamPos(0));
            Load_Stream(true, reinterpret_cast<std::uint8_t *>(
                varMapEnts->entityString), varMapEnts->numEntityChars);
        }
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_MapEntsPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varMapEntsPtr),
        sizeof(MapEnts *));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varMapEntsPtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varMapEntsPtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varMapEntsPtr = reinterpret_cast<MapEnts *>(DB_AllocStreamPos(3));
            varMapEnts = *varMapEntsPtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_MapEnts(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_MapEntsAsset(reinterpret_cast<XAssetHeader *>(
                    varMapEntsPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varMapEntsPtr)->name);
                if (inserted) *inserted = *varMapEntsPtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varMapEntsPtr));
        }
    }
    DB_PopStreamPos();
}
