#pragma once

#include <gfx_d3d/gfx_image_types.h>

#include <cstddef>
#include <cstdint>

// Renderer post-load boundary used by the canonical generated image loader.
// Native builds provide the D3D implementation. The web implementation copies
// the transient DB load definition at this boundary, where native IW3 uploads
// it to D3D, and leaves the canonical GPU handle null.
void __cdecl Load_Texture(GfxTexture *remoteLoadDef, GfxImage *image);

struct WebDbImageLoadDef
{
    std::uint8_t levelCount;
    std::uint8_t flags;
    std::int16_t dimensions[3];
    GfxImageFormat format;
    const std::uint8_t *data;
    std::size_t byteLength;
};

struct WebDbImageLoadDefStats
{
    std::size_t entryCount;
    std::size_t encodedPayloadBytes;
    std::size_t budgetBytes;
    std::uint64_t evictionCount;
};

// Returns the most recently loaded transient payload for the canonical image
// name. The view remains valid until another image load or an explicit clear.
bool DB_WebGetImageLoadDef(const GfxImage *image, WebDbImageLoadDef &loadDef);
WebDbImageLoadDefStats DB_WebGetImageLoadDefStats();
void DB_WebClearImageLoadDefs();
