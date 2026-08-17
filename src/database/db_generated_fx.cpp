#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <EffectsCore/fx_types.h>
#include <gfx_d3d/material_types.h>

#include <cstdint>
#include <limits>

FxEffectDef *varFxEffectDef = nullptr;
const FxEffectDef **varFxEffectDefHandle = nullptr;
FxEffectDefRef *varFxEffectDefRef = nullptr;
FxElemMarkVisuals *varFxElemMarkVisuals = nullptr;
FxElemVisuals *varFxElemVisuals = nullptr;
FxElemDefVisuals *varFxElemDefVisuals = nullptr;
FxTrailVertex *varFxTrailVertex = nullptr;
FxTrailDef *varFxTrailDef = nullptr;
FxElemDef *varFxElemDef = nullptr;
FxElemVelStateSample *varFxElemVelStateSample = nullptr;
FxElemVisStateSample *varFxElemVisStateSample = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical FX loader requires the IW3 32-bit ABI");
static_assert(sizeof(FxEffectDef) == 32u);
static_assert(sizeof(FxElemDef) == 252u);
static_assert(sizeof(FxElemVisuals) == 4u);
static_assert(sizeof(FxElemMarkVisuals) == 8u);
static_assert(sizeof(FxTrailDef) == 28u);

bool CheckedArray(std::int64_t count, std::size_t stride,
    const char *stage, std::size_t &bytes)
{
    if (count < 0 || count > (std::numeric_limits<std::int32_t>::max)() ||
        static_cast<std::uint64_t>(count) * stride >
            (std::numeric_limits<std::uint32_t>::max)())
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    bytes = static_cast<std::size_t>(count) * stride;
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    return true;
}

void Load_MaterialHandleArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedArray(count, sizeof(Material *), "FX/material array", bytes))
        return;
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialHandle),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    Material **entry = varMaterialHandle;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varMaterialHandle = entry;
        Load_MaterialHandle(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_FxEffectDefRef(bool atStreamStart)
{
    varXString = reinterpret_cast<const char **>(varFxEffectDefRef);
    Load_XString(atStreamStart);
    if (!DB_RuntimeGeneratedLoadFailed())
        Load_FxEffectDefFromName(reinterpret_cast<const char **>(
            varFxEffectDefRef));
}

void Load_FxElemMarkVisuals(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varFxElemMarkVisuals),
        sizeof(FxElemMarkVisuals));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varMaterialHandle = varFxElemMarkVisuals->materials;
    Load_MaterialHandleArray(false, 2);
}

void Load_FxElemMarkVisualsArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedArray(count, sizeof(FxElemMarkVisuals),
        "FX/mark visuals", bytes)) return;
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varFxElemMarkVisuals),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    FxElemMarkVisuals *entry = varFxElemMarkVisuals;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varFxElemMarkVisuals = entry;
        Load_FxElemMarkVisuals(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_FxElemVisuals(bool atStreamStart)
{
    switch (varFxElemDef->elemType)
    {
    case 5:
    {
        XModel **model = &varFxElemVisuals->model;
        Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(model), 4);
        if (DB_RuntimeGeneratedLoadFailed() || !*model) break;
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(*model);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            // This is the exact point where native Load_XModelPtr allocates
            // and enters Load_XModel. Null and prior aliases remain usable.
            DB_RuntimeGeneratedFailure(
                "Load_XModelPtr/unsupported inline body closure");
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(model));
        }
        break;
    }
    case 10:
        varFxEffectDefRef = &varFxElemVisuals->effectDef;
        Load_FxEffectDefRef(atStreamStart);
        break;
    case 8:
        varXString = &varFxElemVisuals->soundName;
        Load_XString(atStreamStart);
        break;
    default:
        if (varFxElemDef->elemType != 6 && varFxElemDef->elemType != 7)
        {
            varMaterialHandle = &varFxElemVisuals->material;
            Load_MaterialHandle(atStreamStart);
        }
        break;
    }
}

void Load_FxElemVisualsArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedArray(count, sizeof(FxElemVisuals), "FX/visual array", bytes))
        return;
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varFxElemVisuals),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    FxElemVisuals *entry = varFxElemVisuals;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varFxElemVisuals = entry;
        Load_FxElemVisuals(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_FxElemDefVisuals(bool atStreamStart)
{
    if (varFxElemDef->elemType == 9)
    {
        if (varFxElemDefVisuals->markArray)
        {
            varFxElemDefVisuals->markArray =
                reinterpret_cast<FxElemMarkVisuals *>(
                    AllocLoad_FxElemVisStateSample());
            varFxElemMarkVisuals = varFxElemDefVisuals->markArray;
            Load_FxElemMarkVisualsArray(true, varFxElemDef->visualCount);
        }
    }
    else if (varFxElemDef->visualCount > 1u)
    {
        if (varFxElemDefVisuals->array)
        {
            varFxElemDefVisuals->array = reinterpret_cast<FxElemVisuals *>(
                AllocLoad_FxElemVisStateSample());
            varFxElemVisuals = varFxElemDefVisuals->array;
            Load_FxElemVisualsArray(true, varFxElemDef->visualCount);
        }
    }
    else
    {
        varFxElemVisuals = &varFxElemDefVisuals->instance;
        Load_FxElemVisuals(atStreamStart);
    }
}

void Load_FxTrailDef(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varFxTrailDef),
        sizeof(FxTrailDef));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (varFxTrailDef->verts)
    {
        std::size_t bytes = 0;
        if (!CheckedArray(varFxTrailDef->vertCount, sizeof(FxTrailVertex),
            "FX/trail vertices", bytes)) return;
        varFxTrailDef->verts = reinterpret_cast<FxTrailVertex *>(
            AllocLoad_FxElemVisStateSample());
        varFxTrailVertex = varFxTrailDef->verts;
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(varFxTrailVertex),
            static_cast<std::int32_t>(bytes));
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (varFxTrailDef->inds)
    {
        std::size_t bytes = 0;
        if (!CheckedArray(varFxTrailDef->indCount, sizeof(std::uint16_t),
            "FX/trail indices", bytes)) return;
        varFxTrailDef->inds = reinterpret_cast<std::uint16_t *>(
            DB_AllocStreamPos(1));
        Load_Stream(true,
            reinterpret_cast<std::uint8_t *>(varFxTrailDef->inds),
            static_cast<std::int32_t>(bytes));
    }
}

void Load_FxElemDef(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varFxElemDef),
        sizeof(FxElemDef));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (varFxElemDef->velSamples)
    {
        const std::int32_t count =
            static_cast<std::int32_t>(varFxElemDef->velIntervalCount) + 1;
        std::size_t bytes = 0;
        if (!CheckedArray(count, sizeof(FxElemVelStateSample),
            "FX/velocity samples", bytes)) return;
        varFxElemDef->velSamples = reinterpret_cast<FxElemVelStateSample *>(
            AllocLoad_FxElemVisStateSample());
        varFxElemVelStateSample = varFxElemDef->velSamples;
        Load_Stream(true,
            reinterpret_cast<std::uint8_t *>(varFxElemVelStateSample),
            static_cast<std::int32_t>(bytes));
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (varFxElemDef->visSamples)
    {
        const std::int32_t count =
            static_cast<std::int32_t>(varFxElemDef->visStateIntervalCount) + 1;
        std::size_t bytes = 0;
        if (!CheckedArray(count, sizeof(FxElemVisStateSample),
            "FX/visual samples", bytes)) return;
        varFxElemDef->visSamples = reinterpret_cast<FxElemVisStateSample *>(
            AllocLoad_FxElemVisStateSample());
        varFxElemVisStateSample = varFxElemDef->visSamples;
        Load_Stream(true,
            reinterpret_cast<std::uint8_t *>(varFxElemVisStateSample),
            static_cast<std::int32_t>(bytes));
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varFxElemDefVisuals = &varFxElemDef->visuals;
    Load_FxElemDefVisuals(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varFxEffectDefRef = &varFxElemDef->effectOnImpact;
    Load_FxEffectDefRef(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varFxEffectDefRef = &varFxElemDef->effectOnDeath;
    Load_FxEffectDefRef(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varFxEffectDefRef = &varFxElemDef->effectEmitted;
    Load_FxEffectDefRef(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (varFxElemDef->trailDef)
    {
        varFxElemDef->trailDef = reinterpret_cast<FxTrailDef *>(
            AllocLoad_FxElemVisStateSample());
        varFxTrailDef = varFxElemDef->trailDef;
        Load_FxTrailDef(true);
    }
}

void Load_FxElemDefArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedArray(count, sizeof(FxElemDef), "FX/element array", bytes))
        return;
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varFxElemDef),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    FxElemDef *entry = varFxElemDef;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varFxElemDef = entry;
        Load_FxElemDef(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_FxEffectDef(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varFxEffectDef), sizeof(FxEffectDef));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varFxEffectDef->name;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed() && varFxEffectDef->elemDefs)
    {
        const std::int64_t count =
            static_cast<std::int64_t>(varFxEffectDef->elemDefCountEmission) +
            varFxEffectDef->elemDefCountOneShot +
            varFxEffectDef->elemDefCountLooping;
        std::size_t bytes = 0;
        if (CheckedArray(count, sizeof(FxElemDef), "FX/element array", bytes))
        {
            varFxEffectDef->elemDefs = reinterpret_cast<const FxElemDef *>(
                AllocLoad_FxElemVisStateSample());
            varFxElemDef = const_cast<FxElemDef *>(varFxEffectDef->elemDefs);
            Load_FxElemDefArray(true, static_cast<std::int32_t>(count));
        }
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_FxEffectDefHandle(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varFxEffectDefHandle), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varFxEffectDefHandle)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varFxEffectDefHandle);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varFxEffectDefHandle = reinterpret_cast<const FxEffectDef *>(
                AllocLoad_FxElemVisStateSample());
            varFxEffectDef = const_cast<FxEffectDef *>(*varFxEffectDefHandle);
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_FxEffectDef(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_FxEffectDefAsset(reinterpret_cast<XAssetHeader *>(
                    varFxEffectDefHandle));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varFxEffectDefHandle)->name);
                if (inserted) *inserted = *varFxEffectDefHandle;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varFxEffectDefHandle));
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_FxEffectDefHandleArray(bool atStreamStart,
    std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedArray(count, sizeof(FxEffectDef *), "FX/handle array", bytes))
        return;
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varFxEffectDefHandle),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    const FxEffectDef **entry = varFxEffectDefHandle;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varFxEffectDefHandle = entry;
        Load_FxEffectDefHandle(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}
