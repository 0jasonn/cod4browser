#pragma once

#include <gfx_d3d/gfx_draw_surf_types.h>

#include <cstdint>

struct MaterialTechniqueSet;
struct MaterialTextureDef;
struct MaterialConstantDef;
struct GfxStateBits;

// Canonical KisakCOD/IW3 database-facing material records. Backend shader and
// texture implementations remain in r_material.h.
struct MaterialInfo // sizeof=0x18
{
    const char *name;
    std::uint8_t gameFlags;
    std::uint8_t sortKey;
    std::uint8_t textureAtlasRowCount;
    std::uint8_t textureAtlasColumnCount;
    GfxDrawSurf drawSurf;
    std::uint32_t surfaceTypeBits;
    std::uint16_t hashIndex;
    std::uint16_t padding;
};

struct Material // IW3 size: 0x50
{
    MaterialInfo info;
    std::uint8_t stateBitsEntry[34];
    std::uint8_t textureCount;
    std::uint8_t constantCount;
    std::uint8_t stateBitsCount;
    std::uint8_t stateFlags;
    std::uint8_t cameraRegion;
    std::uint8_t padding;
    MaterialTechniqueSet *techniqueSet;
    MaterialTextureDef *textureTable;
    MaterialConstantDef *constantTable;
    GfxStateBits *stateBitsTable;
#ifdef KISAK_RADIANT
    int surfaceFlags;
    std::uint16_t editorToolFlags;
    std::uint8_t editorUsage;
    std::uint32_t editorLocale;
#endif
};

static_assert(sizeof(void *) != 4u || sizeof(MaterialInfo) == 24u);
#ifdef KISAK_RADIANT
static_assert(sizeof(void *) != 4u || sizeof(Material) == 96u);
#else
static_assert(sizeof(void *) != 4u || sizeof(Material) == 80u);
#endif
