#include <web/web_renderer_surface_storage.h>

#include <cmath>
#include <cstddef>
#include <new>
#include <utility>

WebRendererSurfaceResult WebRenderer_ValidateSurface(
    const WebRendererSurfaceDesc &surface,
    const WebRendererDrawDesc &draw) noexcept
{
    if (surface.vertices == nullptr || surface.indices == nullptr ||
        surface.vertexCount == 0u || surface.indexCount == 0u ||
        draw.indexCount == 0u)
    {
        return WebRendererSurfaceResult::InvalidDescriptor;
    }
    if (surface.vertexCount > WEB_RENDERER_MAX_SURFACE_VERTICES ||
        surface.indexCount > WEB_RENDERER_MAX_SURFACE_INDICES)
    {
        return WebRendererSurfaceResult::OutputTooLarge;
    }
    if (draw.topology != WebRendererPrimitiveTopology::TriangleList)
    {
        return WebRendererSurfaceResult::UnsupportedTopology;
    }
    if (draw.textureBinding != WebRendererTextureBinding::None &&
        draw.textureBinding != WebRendererTextureBinding::EngineImage)
    {
        return WebRendererSurfaceResult::UnsupportedTextureBinding;
    }
    if ((draw.firstIndex % 3u) != 0u || (draw.indexCount % 3u) != 0u ||
        draw.firstIndex > surface.indexCount ||
        draw.indexCount > surface.indexCount - draw.firstIndex)
    {
        return WebRendererSurfaceResult::InvalidDescriptor;
    }

    for (std::uint32_t vertexIndex = 0; vertexIndex < surface.vertexCount; ++vertexIndex)
    {
        const WebRendererSurfaceVertex &vertex = surface.vertices[vertexIndex];
        for (float component : vertex.position)
        {
            if (!std::isfinite(component))
            {
                return WebRendererSurfaceResult::NonFiniteVertex;
            }
        }
        for (float component : vertex.color)
        {
            if (!std::isfinite(component))
            {
                return WebRendererSurfaceResult::NonFiniteVertex;
            }
        }
        for (float component : vertex.textureCoordinate)
        {
            if (!std::isfinite(component))
            {
                return WebRendererSurfaceResult::NonFiniteVertex;
            }
        }
    }

    for (std::uint32_t index = 0; index < surface.indexCount; ++index)
    {
        if (surface.indices[index] >= surface.vertexCount)
        {
            return WebRendererSurfaceResult::IndexOutOfRange;
        }
    }
    return WebRendererSurfaceResult::Success;
}

WebRendererSurfaceResult WebRenderer_CopySurface(
    const WebRendererSurfaceDesc &surface,
    const WebRendererDrawDesc &draw,
    WebRendererOwnedSurface &destination)
{
    const WebRendererSurfaceResult validation =
        WebRenderer_ValidateSurface(surface, draw);
    if (validation != WebRendererSurfaceResult::Success)
    {
        return validation;
    }

    WebRendererOwnedSurface replacement;
    try
    {
        replacement.vertices.assign(
            surface.vertices,
            surface.vertices + surface.vertexCount);
        replacement.indices.assign(
            surface.indices,
            surface.indices + surface.indexCount);
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererSurfaceResult::AllocationFailed;
    }
    replacement.draw = draw;

    destination.vertices.swap(replacement.vertices);
    destination.indices.swap(replacement.indices);
    destination.draw = replacement.draw;
    return WebRendererSurfaceResult::Success;
}

const char *WebRenderer_SurfaceResultString(WebRendererSurfaceResult result) noexcept
{
    switch (result)
    {
    case WebRendererSurfaceResult::Success: return "success";
    case WebRendererSurfaceResult::InvalidDescriptor:
        return "invalid indexed surface descriptor";
    case WebRendererSurfaceResult::UnsupportedTopology:
        return "unsupported surface primitive topology";
    case WebRendererSurfaceResult::UnsupportedTextureBinding:
        return "unsupported surface texture binding";
    case WebRendererSurfaceResult::OutputTooLarge:
        return "surface exceeds the renderer recovery limit";
    case WebRendererSurfaceResult::NonFiniteVertex:
        return "surface contains a non-finite vertex component";
    case WebRendererSurfaceResult::IndexOutOfRange:
        return "surface index is outside the vertex array";
    case WebRendererSurfaceResult::AllocationFailed:
        return "renderer surface recovery allocation failed";
    case WebRendererSurfaceResult::BackendFailure:
        return "graphics backend surface upload failed";
    }
    return "unknown renderer surface error";
}
