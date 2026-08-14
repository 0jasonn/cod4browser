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

// On failure, destination remains completely unchanged.
WebRendererSurfaceResult WebRenderer_CopySurface(
    const WebRendererSurfaceDesc &surface,
    const WebRendererDrawDesc &draw,
    WebRendererOwnedSurface &destination);
