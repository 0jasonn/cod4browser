#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>

#include <script/scr_stringlist.h>

#include <cstdint>
#include <limits>

const char **varTempString = nullptr;
const char *varConstChar = nullptr;
const char **varXString = nullptr;
RawFile *varRawFile = nullptr;
RawFile **varRawFilePtr = nullptr;
PhysPreset *varPhysPreset = nullptr;
PhysPreset **varPhysPresetPtr = nullptr;
XAsset *varXAsset = nullptr;
XAssetHeader *varXAssetHeader = nullptr;
XAssetList *varXAssetList = nullptr;
ScriptStringList *varScriptStringList = nullptr;
static std::uint32_t g_generatedAssetIndex = 0;

namespace
{
bool CheckedCount(std::int32_t count, std::size_t stride,
    const char *failureStage, std::size_t &bytes)
{
    if (count < 0 || static_cast<std::uint64_t>(count) * stride >
        (std::numeric_limits<std::uint32_t>::max)())
    {
        DB_RuntimeGeneratedFailure(failureStage);
        return false;
    }
    bytes = static_cast<std::size_t>(count) * stride;
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(failureStage);
        return false;
    }
    return true;
}

const char *PointerClassification(std::uintptr_t value)
{
    if (!value) return "null";
    if (value == static_cast<std::uintptr_t>(UINT32_MAX)) return "inline-shared/-1";
    if (value == static_cast<std::uintptr_t>(UINT32_MAX - 1u)) return "inline-insert/-2";
    return "prior-offset/alias";
}

std::uint8_t *AllocLoad_raw_byte()
{
    return DB_AllocStreamPos(0);
}

void Load_ConstCharArray(bool atStreamStart, std::int32_t count)
{
    if (count < 0)
    {
        DB_RuntimeGeneratedFailure("RawFile/invalid buffer length");
        return;
    }
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(
        const_cast<char *>(varConstChar)), count);
}

void Load_TempString(bool atStreamStart, std::uint32_t index)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varTempString), 4);
    if (DB_RuntimeGeneratedLoadFailed() || !*varTempString) return;
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(*varTempString);
    if (value == UINT32_MAX)
    {
        *varTempString = reinterpret_cast<const char *>(AllocLoad_raw_byte());
        varConstChar = *varTempString;
        Load_TempStringCustom(const_cast<char **>(varTempString));
    }
    else
    {
        DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(varTempString));
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;
    const std::uint32_t stringId = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(*varTempString));
    DB_RuntimeTraceScriptString(index,
        stringId ? SL_ConvertToString(stringId) : "");
}

void Load_TempStringArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedCount(count, 4u, "ScriptStringList/excessive count", bytes)) return;
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varTempString),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    const char **entry = varTempString;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varTempString = entry;
        Load_TempString(false, static_cast<std::uint32_t>(index));
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_XStringInternal(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varXString), 4);
    if (DB_RuntimeGeneratedLoadFailed() || !*varXString) return;
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(*varXString);
    if (value == UINT32_MAX)
    {
        *varXString = reinterpret_cast<const char *>(AllocLoad_raw_byte());
        varConstChar = *varXString;
        Load_XStringCustom(const_cast<char **>(varXString));
    }
    else
    {
        DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(varXString));
    }
}

void Load_PhysPreset(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varPhysPreset),
        sizeof(PhysPreset));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varPhysPreset->name;
    Load_XStringInternal(false);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varXString = &varPhysPreset->sndAliasPrefix;
        Load_XStringInternal(false);
    }
    DB_PopStreamPos();
}

void Load_PhysPresetPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varPhysPresetPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varPhysPresetPtr)
    {
        const std::uintptr_t value =
            reinterpret_cast<std::uintptr_t>(*varPhysPresetPtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varPhysPresetPtr = reinterpret_cast<PhysPreset *>(
                AllocLoad_FxElemVisStateSample());
            varPhysPreset = *varPhysPresetPtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_PhysPreset(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_PhysPresetAsset(
                    reinterpret_cast<XAssetHeader *>(varPhysPresetPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varPhysPresetPtr)->name);
                if (inserted) *inserted = *varPhysPresetPtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                reinterpret_cast<std::uint32_t *>(varPhysPresetPtr));
        }
    }
    DB_PopStreamPos();
}

void Load_RawFile(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varRawFile),
        sizeof(RawFile));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varRawFile->name;
    Load_XStringInternal(false);
    if (!DB_RuntimeGeneratedLoadFailed() && varRawFile->buffer)
    {
        if (varRawFile->len < 0 || varRawFile->len ==
            (std::numeric_limits<std::int32_t>::max)())
        {
            DB_RuntimeGeneratedFailure("RawFile/invalid buffer length");
        }
        else
        {
            varRawFile->buffer = reinterpret_cast<const char *>(AllocLoad_raw_byte());
            varConstChar = varRawFile->buffer;
            Load_ConstCharArray(true, varRawFile->len + 1);
        }
    }
    DB_PopStreamPos();
}

void Load_RawFilePtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varRawFilePtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varRawFilePtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(*varRawFilePtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varRawFilePtr = reinterpret_cast<RawFile *>(AllocLoad_FxElemVisStateSample());
            varRawFile = *varRawFilePtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_RawFile(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_RawFileAsset(reinterpret_cast<XAssetHeader *>(varRawFilePtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varRawFilePtr)->name);
                if (inserted) *inserted = *varRawFilePtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(varRawFilePtr));
        }
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_XString(bool atStreamStart)
{
    Load_XStringInternal(atStreamStart);
}

XAsset *__cdecl AllocLoad_FxElemVisStateSample()
{
    return reinterpret_cast<XAsset *>(DB_AllocStreamPos(3));
}

void DB_SetGeneratedAssetIndex(std::uint32_t index)
{
    g_generatedAssetIndex = index;
}

void __cdecl Load_ScriptStringList(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varScriptStringList),
        sizeof(ScriptStringList));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    std::size_t pointerBytes = 0;
    if (!CheckedCount(varScriptStringList->count, 4u,
        "ScriptStringList/excessive count", pointerBytes))
    {
    }
    else if (varScriptStringList->strings)
    {
        varScriptStringList->strings = reinterpret_cast<const char **>(
            AllocLoad_FxElemVisStateSample());
        varTempString = varScriptStringList->strings;
        Load_TempStringArray(true, varScriptStringList->count);
    }
    DB_PopStreamPos();
}

static void Load_XAssetHeader(bool atStreamStart)
{
    (void)atStreamStart;
    if (varXAsset->type < 0 || varXAsset->type >= ASSET_TYPE_COUNT)
    {
        DB_RuntimeGeneratedFailure("Load_XAsset/invalid asset type");
        return;
    }
    DB_RuntimeTraceAssetBegin(g_generatedAssetIndex, varXAsset->type,
        PointerClassification(reinterpret_cast<std::uintptr_t>(
            varXAsset->header.data)));
    switch (varXAsset->type)
    {
    case ASSET_TYPE_MATERIAL:
        varMaterialHandle = reinterpret_cast<Material **>(varXAssetHeader);
        Load_MaterialHandle(false);
        break;
    case ASSET_TYPE_PHYSPRESET:
        varPhysPresetPtr = reinterpret_cast<PhysPreset **>(varXAssetHeader);
        Load_PhysPresetPtr(false);
        break;
    case ASSET_TYPE_TECHNIQUE_SET:
        varMaterialTechniqueSetPtr =
            reinterpret_cast<MaterialTechniqueSet **>(varXAssetHeader);
        Load_MaterialTechniqueSetPtr(false);
        break;
    case ASSET_TYPE_IMAGE:
        varGfxImagePtr = reinterpret_cast<GfxImage **>(varXAssetHeader);
        Load_GfxImagePtr(false);
        break;
    case ASSET_TYPE_RAWFILE:
        varRawFilePtr = reinterpret_cast<RawFile **>(varXAssetHeader);
        Load_RawFilePtr(false);
        break;
    default:
        DB_RuntimeGeneratedFailure("Load_XAssetHeader/unsupported family closure");
        break;
    }
}

void __cdecl Load_XAsset(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varXAsset),
        sizeof(XAsset));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXAssetHeader = &varXAsset->header;
    Load_XAssetHeader(false);
}
