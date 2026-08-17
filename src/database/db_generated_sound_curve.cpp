#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <sound/snd_alias_types.h>

#include <cstdint>

SndCurve *varSndCurve = nullptr;
SndCurve **varSndCurvePtr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical SndCurve loader requires the IW3 32-bit ABI");
static_assert(sizeof(SndCurve) == 72u);

void Load_SndCurve(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varSndCurve), sizeof(SndCurve));
    if (DB_RuntimeGeneratedLoadFailed()) return;

    DB_PushStreamPos(4);
    varXString = &varSndCurve->filename;
    Load_XString(false);
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_SndCurvePtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varSndCurvePtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;

    DB_PushStreamPos(0);
    if (*varSndCurvePtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varSndCurvePtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varSndCurvePtr = reinterpret_cast<SndCurve *>(
                AllocLoad_FxElemVisStateSample());
            varSndCurve = *varSndCurvePtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_SndCurve(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_SndCurveAsset(reinterpret_cast<XAssetHeader *>(
                    varSndCurvePtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varSndCurvePtr)->filename);
                if (inserted) *inserted = *varSndCurvePtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varSndCurvePtr));
        }
    }
    DB_PopStreamPos();
}
