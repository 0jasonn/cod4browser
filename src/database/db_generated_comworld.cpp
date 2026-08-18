#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <qcommon/com_world_types.h>

#include <cstdint>
#include <limits>

ComWorld *varComWorld = nullptr;
ComWorld **varComWorldPtr = nullptr;
ComPrimaryLight *varComPrimaryLight = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical ComWorld loader requires the IW3 32-bit ABI");
static_assert(sizeof(ComWorld) == 16u);
static_assert(sizeof(ComPrimaryLight) == 68u);

void Load_ComPrimaryLight(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varComPrimaryLight),
        sizeof(ComPrimaryLight));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &varComPrimaryLight->defName;
    Load_XString(false);
}

void Load_ComPrimaryLightArray(bool atStreamStart, std::uint32_t count)
{
    if (count > static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)() /
            sizeof(ComPrimaryLight)))
    {
        DB_RuntimeGeneratedFailure("ComWorld/excessive primary lights");
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(count) *
        sizeof(ComPrimaryLight);
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure("ComWorld/primary light array");
        return;
    }
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varComPrimaryLight),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    ComPrimaryLight *entry = varComPrimaryLight;
    for (std::uint32_t index = 0; index < count; ++index, ++entry)
    {
        varComPrimaryLight = entry;
        Load_ComPrimaryLight(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_ComWorld(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varComWorld),
        sizeof(ComWorld));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varComWorld->name;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed() && varComWorld->primaryLights)
    {
        varComWorld->primaryLights = reinterpret_cast<ComPrimaryLight *>(
            AllocLoad_FxElemVisStateSample());
        varComPrimaryLight = varComWorld->primaryLights;
        Load_ComPrimaryLightArray(true, varComWorld->primaryLightCount);
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_ComWorldPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varComWorldPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varComWorldPtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varComWorldPtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varComWorldPtr = reinterpret_cast<ComWorld *>(
                AllocLoad_FxElemVisStateSample());
            varComWorld = *varComWorldPtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_ComWorld(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_ComWorldAsset(reinterpret_cast<XAssetHeader *>(
                    varComWorldPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varComWorldPtr)->name);
                if (inserted) *inserted = *varComWorldPtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varComWorldPtr));
        }
    }
    DB_PopStreamPos();
}
