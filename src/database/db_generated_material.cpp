#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_generated_material_platform.h>
#include <database/db_generated_water.h>
#include <database/db_runtime_prefix.h>
#include <gfx_d3d/material_types.h>

#include <cstddef>
#include <cstdint>
#include <limits>

Material *varMaterial = nullptr;
Material **varMaterialHandle = nullptr;
MaterialInfo *varMaterialInfo = nullptr;
MaterialTextureDef *varMaterialTextureDef = nullptr;
water_t **varMaterialTextureDefInfo = nullptr;
MaterialConstantDef *varMaterialConstantDef = nullptr;
GfxStateBits *varGfxStateBits = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical Material loader requires the IW3 32-bit ABI");
static_assert(sizeof(Material) == 80u);
static_assert(sizeof(MaterialInfo) == 24u);
static_assert(sizeof(MaterialTextureDef) == 12u);
static_assert(sizeof(MaterialConstantDef) == 32u);
static_assert(sizeof(GfxStateBits) == 8u);

bool CheckedArrayBytes(std::size_t count, std::size_t stride,
    const char *failureStage, std::size_t &bytes)
{
    if (count > (std::numeric_limits<std::uint32_t>::max)() / stride)
    {
        DB_RuntimeGeneratedFailure(failureStage);
        return false;
    }
    bytes = count * stride;
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(failureStage);
        return false;
    }
    return true;
}

void Load_MaterialInfo(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varMaterialInfo),
        sizeof(MaterialInfo));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &varMaterialInfo->name;
    Load_XString(false);
}

void Load_MaterialTextureDefInfo(bool atStreamStart)
{
    if (varMaterialTextureDef->semantic == 11u)
    {
        if (*varMaterialTextureDefInfo)
        {
            const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
                *varMaterialTextureDefInfo);
            if (value == UINT32_MAX)
            {
                *varMaterialTextureDefInfo = reinterpret_cast<water_t *>(
                    AllocLoad_FxElemVisStateSample());
                varwater_t = *varMaterialTextureDefInfo;
                Load_water_t(true);
                if (!DB_RuntimeGeneratedLoadFailed())
                    Load_PicmipWater(varMaterialTextureDefInfo);
            }
            else
            {
                DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                    varMaterialTextureDefInfo));
            }
        }
    }
    else
    {
        varGfxImagePtr = reinterpret_cast<GfxImage **>(
            varMaterialTextureDefInfo);
        Load_GfxImagePtr(atStreamStart);
    }
}

void Load_MaterialTextureDef(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialTextureDef),
        sizeof(MaterialTextureDef));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varMaterialTextureDefInfo = reinterpret_cast<water_t **>(
        &varMaterialTextureDef->u);
    Load_MaterialTextureDefInfo(false);
}

void Load_MaterialTextureDefArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (count < 0 || !CheckedArrayBytes(static_cast<std::size_t>(count),
        sizeof(MaterialTextureDef), "Material/texture table", bytes)) return;
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialTextureDef),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    MaterialTextureDef *entry = varMaterialTextureDef;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varMaterialTextureDef = entry;
        Load_MaterialTextureDef(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_MaterialConstantDefArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (count < 0 || !CheckedArrayBytes(static_cast<std::size_t>(count),
        sizeof(MaterialConstantDef), "Material/constant table", bytes)) return;
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialConstantDef),
        static_cast<std::int32_t>(bytes));
}

void Load_GfxStateBitsArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (count < 0 || !CheckedArrayBytes(static_cast<std::size_t>(count),
        sizeof(GfxStateBits), "Material/state bits table", bytes)) return;
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varGfxStateBits),
        static_cast<std::int32_t>(bytes));
}

void Load_Material(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varMaterial),
        sizeof(Material));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varMaterialInfo = &varMaterial->info;
    Load_MaterialInfo(false);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varMaterialTechniqueSetPtr = &varMaterial->techniqueSet;
        Load_MaterialTechniqueSetPtr(false);
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varMaterial->textureTable)
    {
        if (reinterpret_cast<std::uintptr_t>(varMaterial->textureTable) ==
            UINT32_MAX)
        {
            varMaterial->textureTable = reinterpret_cast<MaterialTextureDef *>(
                AllocLoad_FxElemVisStateSample());
            varMaterialTextureDef = varMaterial->textureTable;
            Load_MaterialTextureDefArray(true, varMaterial->textureCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &varMaterial->textureTable));
        }
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varMaterial->constantTable)
    {
        if (reinterpret_cast<std::uintptr_t>(varMaterial->constantTable) ==
            UINT32_MAX)
        {
            varMaterial->constantTable = reinterpret_cast<MaterialConstantDef *>(
                DB_AllocStreamPos(15));
            varMaterialConstantDef = varMaterial->constantTable;
            Load_MaterialConstantDefArray(true, varMaterial->constantCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &varMaterial->constantTable));
        }
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varMaterial->stateBitsTable)
    {
        if (reinterpret_cast<std::uintptr_t>(varMaterial->stateBitsTable) ==
            UINT32_MAX)
        {
            varMaterial->stateBitsTable = reinterpret_cast<GfxStateBits *>(
                AllocLoad_FxElemVisStateSample());
            varGfxStateBits = varMaterial->stateBitsTable;
            Load_GfxStateBitsArray(true, varMaterial->stateBitsCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &varMaterial->stateBitsTable));
        }
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_MaterialHandle(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varMaterialHandle),
        4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varMaterialHandle)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varMaterialHandle);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varMaterialHandle = reinterpret_cast<Material *>(
                AllocLoad_FxElemVisStateSample());
            varMaterial = *varMaterialHandle;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_Material(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_MaterialAsset(reinterpret_cast<XAssetHeader *>(
                    varMaterialHandle));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varMaterialHandle)->info.name);
                if (inserted) *inserted = *varMaterialHandle;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varMaterialHandle));
        }
    }
    DB_PopStreamPos();
}
