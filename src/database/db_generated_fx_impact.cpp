#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <EffectsCore/fx_types.h>

#include <cstdint>
#include <limits>

FxImpactEntry *varFxImpactEntry = nullptr;
FxImpactTable *varFxImpactTable = nullptr;
FxImpactTable **varFxImpactTablePtr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical FX impact loader requires the IW3 32-bit ABI");
static_assert(sizeof(FxImpactEntry) == 132u);
static_assert(sizeof(FxImpactTable) == 8u);

bool CheckedEntries(std::int32_t count, const char *stage,
    std::size_t &bytes)
{
    if (count < 0 || static_cast<std::uint64_t>(count) *
            sizeof(FxImpactEntry) >
            (std::numeric_limits<std::uint32_t>::max)())
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    bytes = static_cast<std::size_t>(count) * sizeof(FxImpactEntry);
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    return true;
}

void Load_FxImpactEntry(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varFxImpactEntry),
        sizeof(FxImpactEntry));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varFxEffectDefHandle = varFxImpactEntry->nonflesh;
    Load_FxEffectDefHandleArray(false, 29);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varFxEffectDefHandle = varFxImpactEntry->flesh;
    Load_FxEffectDefHandleArray(false, 4);
}

void Load_FxImpactEntryArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedEntries(count, "FX impact/entry array", bytes)) return;
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varFxImpactEntry),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    FxImpactEntry *entry = varFxImpactEntry;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varFxImpactEntry = entry;
        Load_FxImpactEntry(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_FxImpactTable(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varFxImpactTable),
        sizeof(FxImpactTable));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varFxImpactTable->name;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed() && varFxImpactTable->table)
    {
        varFxImpactTable->table = reinterpret_cast<FxImpactEntry *>(
            AllocLoad_FxElemVisStateSample());
        varFxImpactEntry = varFxImpactTable->table;
        Load_FxImpactEntryArray(true, 12);
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_FxImpactTablePtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varFxImpactTablePtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varFxImpactTablePtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varFxImpactTablePtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varFxImpactTablePtr = reinterpret_cast<FxImpactTable *>(
                AllocLoad_FxElemVisStateSample());
            varFxImpactTable = *varFxImpactTablePtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_FxImpactTable(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_FxImpactTableAsset(reinterpret_cast<XAssetHeader *>(
                    varFxImpactTablePtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varFxImpactTablePtr)->name);
                if (inserted) *inserted = *varFxImpactTablePtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varFxImpactTablePtr));
        }
    }
    DB_PopStreamPos();
}
