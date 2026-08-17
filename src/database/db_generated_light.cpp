#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <gfx_d3d/gfx_light_types.h>

#include <cstdint>

GfxLightDef *varGfxLightDef = nullptr;
GfxLightDef **varGfxLightDefPtr = nullptr;
GfxLightImage *varGfxLightImage = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical light loader requires the IW3 32-bit ABI");
static_assert(sizeof(GfxLightImage) == 8u);
static_assert(sizeof(GfxLightDef) == 16u);

void Load_GfxLightImage(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varGfxLightImage),
        sizeof(GfxLightImage));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varGfxImagePtr = &varGfxLightImage->image;
    Load_GfxImagePtr(false);
}

void Load_GfxLightDef(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varGfxLightDef),
        sizeof(GfxLightDef));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varGfxLightDef->name;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varGfxLightImage = &varGfxLightDef->attenuation;
        Load_GfxLightImage(false);
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_GfxLightDefPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varGfxLightDefPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varGfxLightDefPtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varGfxLightDefPtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varGfxLightDefPtr = reinterpret_cast<GfxLightDef *>(
                AllocLoad_FxElemVisStateSample());
            varGfxLightDef = *varGfxLightDefPtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_GfxLightDef(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_LightDefAsset(reinterpret_cast<XAssetHeader *>(
                    varGfxLightDefPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varGfxLightDefPtr)->name);
                if (inserted) *inserted = *varGfxLightDefPtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varGfxLightDefPtr));
        }
    }
    DB_PopStreamPos();
}
