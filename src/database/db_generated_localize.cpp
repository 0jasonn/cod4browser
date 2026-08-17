#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <database/localize_types.h>

#include <cstdint>

LocalizeEntry *varLocalizeEntry = nullptr;
LocalizeEntry **varLocalizeEntryPtr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical LocalizeEntry loader requires the IW3 32-bit ABI");
static_assert(sizeof(LocalizeEntry) == 8u);

void Load_LocalizeEntry(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varLocalizeEntry),
        sizeof(LocalizeEntry));
    if (DB_RuntimeGeneratedLoadFailed()) return;

    DB_PushStreamPos(4);
    varXString = &varLocalizeEntry->value;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varXString = &varLocalizeEntry->name;
        Load_XString(false);
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_LocalizeEntryPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varLocalizeEntryPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;

    DB_PushStreamPos(0);
    if (*varLocalizeEntryPtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varLocalizeEntryPtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varLocalizeEntryPtr = reinterpret_cast<LocalizeEntry *>(
                AllocLoad_FxElemVisStateSample());
            varLocalizeEntry = *varLocalizeEntryPtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_LocalizeEntry(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_LocalizeEntryAsset(reinterpret_cast<XAssetHeader *>(
                    varLocalizeEntryPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varLocalizeEntryPtr)->name);
                if (inserted) *inserted = *varLocalizeEntryPtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varLocalizeEntryPtr));
        }
    }
    DB_PopStreamPos();
}
