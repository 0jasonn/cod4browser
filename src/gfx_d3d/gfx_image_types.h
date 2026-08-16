#pragma once

#include <cstddef>
#include <cstdint>

// Canonical KisakCOD/IW3 database-facing image records.  The browser database
// loader leaves GfxTexture null: decoded IWI bytes and GPU resources belong to
// the renderer/backend, not to zone publication.
#if defined(D3D_SDK_VERSION)
using GfxImageFormat = _D3DFORMAT;
#else
using GfxImageFormat = std::int32_t;
#endif

struct IDirect3DBaseTexture9;
struct IDirect3DTexture9;
struct IDirect3DVolumeTexture9;
struct IDirect3DCubeTexture9;

enum MapType : std::int32_t
{
    MAPTYPE_NONE = 0x0,
    MAPTYPE_INVALID1 = 0x1,
    MAPTYPE_INVALID2 = 0x2,
    MAPTYPE_2D = 0x3,
    MAPTYPE_3D = 0x4,
    MAPTYPE_CUBE = 0x5,
    MAPTYPE_COUNT = 0x6,
};

struct Picmip
{
    Picmip() : platform{0u, 0u} {}
    Picmip(int value)
        : platform{static_cast<std::uint8_t>(value),
                   static_cast<std::uint8_t>(value)} {}
    std::uint8_t platform[2];
};

struct CardMemory
{
    int platform[2];
};

struct GfxImageLoadDef
{
    std::uint8_t levelCount;
    std::uint8_t flags;
    std::int16_t dimensions[3];
    GfxImageFormat format;
    int resourceSize;
    std::uint8_t data[4];
};

union GfxTexture
{
    IDirect3DBaseTexture9 *basemap;
    IDirect3DTexture9 *map;
    IDirect3DVolumeTexture9 *volmap;
    IDirect3DCubeTexture9 *cubemap;
    GfxImageLoadDef *loadDef;
};

struct GfxImage
{
    MapType mapType;
    GfxTexture texture;
    Picmip picmip;
    bool noPicmip;
    std::uint8_t semantic;
    std::uint8_t track;
    CardMemory cardMemory;
    std::uint16_t width;
    std::uint16_t height;
    std::uint16_t depth;
    std::uint8_t category;
    bool delayLoadPixels;
    const char *name;
};

inline constexpr std::size_t GFX_IMAGE_LOAD_DEF_DATA_OFFSET = 16u;

static_assert(sizeof(void *) != 4u || sizeof(GfxImageLoadDef) == 20u);
static_assert(sizeof(void *) != 4u || sizeof(GfxTexture) == 4u);
static_assert(sizeof(void *) != 4u || sizeof(GfxImage) == 36u);
static_assert(sizeof(void *) != 4u || offsetof(GfxImage, name) == 32u);
