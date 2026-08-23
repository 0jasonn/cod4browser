#pragma once

#include <gfx_d3d/gfx_image_types.h>

using WebRendererImageLookup =
    const GfxImage *(*)(const char *name) noexcept;

// Fastfile comma-prefixed image names are DB reference bodies. Resolve them
// only at renderer evaluation, after every prerequisite zone has published;
// ordinary canonical image identities pass through unchanged.
inline const GfxImage *WebRenderer_ResolveImageReference(
    const GfxImage *image,
    WebRendererImageLookup lookup) noexcept
{
    if (!image || !image->name || image->name[0] != ',' ||
        image->name[1] == '\0' || !lookup)
    {
        return image;
    }
    if (const GfxImage *canonical = lookup(image->name + 1))
        return canonical;
    return image;
}
