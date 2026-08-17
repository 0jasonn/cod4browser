#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_image_platform.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <gfx_d3d/gfx_image_types.h>

#include <cstddef>
#include <cstdint>

GfxImage *varGfxImage = nullptr;
GfxImage **varGfxImagePtr = nullptr;
GfxTexture *varGfxTextureLoad = nullptr;
GfxImageLoadDef *varGfxImageLoadDef = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical GfxImage loader requires the IW3 32-bit ABI");
static_assert(sizeof(GfxImage) == 36u);
static_assert(GFX_IMAGE_LOAD_DEF_DATA_OFFSET == 16u);

void Load_GfxImageLoadDef(bool atStreamStart)
{
    if (!atStreamStart)
    {
        DB_RuntimeGeneratedFailure("GfxImageLoadDef/expected stream start");
        return;
    }
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(varGfxImageLoadDef), 16);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (DB_GetStreamPos() != varGfxImageLoadDef->data)
    {
        DB_RuntimeGeneratedFailure("GfxImageLoadDef/data position");
        return;
    }
    if (varGfxImageLoadDef->resourceSize < 0 ||
        !DB_RuntimeStreamCanRead(static_cast<std::size_t>(
            varGfxImageLoadDef->resourceSize)))
    {
        DB_RuntimeGeneratedFailure("GfxImageLoadDef/resource size");
        return;
    }
    Load_Stream(true, varGfxImageLoadDef->data,
        varGfxImageLoadDef->resourceSize);
}

void Load_GfxTextureLoad(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varGfxTextureLoad), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (varGfxTextureLoad->basemap)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            varGfxTextureLoad->basemap);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            varGfxTextureLoad->basemap =
                reinterpret_cast<IDirect3DBaseTexture9 *>(
                    AllocLoad_FxElemVisStateSample());
            varGfxImageLoadDef = varGfxTextureLoad->loadDef;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_GfxImageLoadDef(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_Texture(varGfxTextureLoad, varGfxImage);
            if (!DB_RuntimeGeneratedLoadFailed() && inserted)
                *inserted = varGfxTextureLoad->basemap;
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varGfxTextureLoad));
        }
    }
    DB_PopStreamPos();
}

void Load_GfxImage(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varGfxImage),
        sizeof(GfxImage));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varGfxImage->name;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varGfxTextureLoad = &varGfxImage->texture;
        Load_GfxTextureLoad(false);
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_GfxImagePtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varGfxImagePtr),
        4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varGfxImagePtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varGfxImagePtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varGfxImagePtr = reinterpret_cast<GfxImage *>(
                AllocLoad_FxElemVisStateSample());
            varGfxImage = *varGfxImagePtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_GfxImage(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_GfxImageAsset(reinterpret_cast<XAssetHeader *>(
                    varGfxImagePtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varGfxImagePtr)->name);
                if (inserted) *inserted = *varGfxImagePtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varGfxImagePtr));
        }
    }
    DB_PopStreamPos();
}
