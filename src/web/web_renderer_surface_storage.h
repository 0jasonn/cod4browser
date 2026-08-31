#pragma once

#include <web/web_renderer_surface.h>

#include <cstdint>
#include <span>
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

// Copy already-validated, non-aliasing geometry into reusable staging buffers.
// Staging may change on allocation failure; publish only after backend success.
WebRendererSurfaceResult WebRenderer_CopyStagedGeometry(
    std::span<const WebRendererSurfaceVertex> sourceVertices,
    std::span<const std::uint32_t> sourceIndices,
    std::vector<WebRendererSurfaceVertex> &vertices,
    std::vector<std::uint32_t> &indices);
