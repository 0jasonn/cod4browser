#pragma once

#include <gfx_d3d/gfx_image_types.h>

// Renderer post-load boundary used by the canonical generated image loader.
// Native builds provide the D3D implementation; renderer-disabled targets
// retain the canonical record and leave its GPU handle null.
void __cdecl Load_Texture(GfxTexture *remoteLoadDef, GfxImage *image);
