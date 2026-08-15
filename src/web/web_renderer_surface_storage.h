#pragma once

#include <web/web_renderer_surface.h>

#include <cstdint>
#include <vector>

// Renderer-internal, backend-neutral recovery value. Building a replacement
// here is host-testable and guarantees that the public callback-scoped pointers
// are copied before any graphics backend sees the description.
struct WebRendererOwnedSurface
{
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint16_t> indices;
    WebRendererDrawDesc draw{
        WebRendererPrimitiveTopology::TriangleList,
        0u,
        0u,
        WebRendererTextureBinding::None,
    };
};

struct WebRendererOwnedDrawList
{
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<WebRendererDrawListDrawDesc> draws;
    std::uint32_t textureCount = 0u;
};

// On failure, destination remains completely unchanged.
WebRendererSurfaceResult WebRenderer_CopySurface(
    const WebRendererSurfaceDesc &surface,
    const WebRendererDrawDesc &draw,
    WebRendererOwnedSurface &destination);

// Copies an entire validated list atomically. Draw ranges may be disjoint, but
// every retained index is validated against the shared vertex array.
WebRendererSurfaceResult WebRenderer_CopyDrawList(
    const WebRendererDrawListDesc &drawList,
    WebRendererOwnedDrawList &destination);
