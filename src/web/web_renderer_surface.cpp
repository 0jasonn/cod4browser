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

WebRendererSurfaceResult WebRenderer_ValidateDrawList(
    const WebRendererDrawListDesc &drawList) noexcept
{
    const WebRendererSurfaceDesc &surface = drawList.surface;
    if (!drawList.draws || drawList.drawCount == 0u ||
        drawList.drawCount > WEB_RENDERER_MAX_DRAW_LIST_DRAWS ||
        drawList.textureCount == 0u ||
        drawList.textureCount > WEB_RENDERER_MAX_DRAW_LIST_TEXTURES ||
        !surface.vertices || !surface.indices || surface.vertexCount == 0u ||
        surface.indexCount == 0u)
    {
        return WebRendererSurfaceResult::InvalidDescriptor;
    }
    if (surface.vertexCount > WEB_RENDERER_MAX_DRAW_LIST_VERTICES ||
        surface.indexCount > WEB_RENDERER_MAX_DRAW_LIST_INDICES)
    {
        return WebRendererSurfaceResult::OutputTooLarge;
    }
    for (std::uint32_t index = 0u; index < drawList.drawCount; ++index)
    {
        const WebRendererDrawListDrawDesc &entry = drawList.draws[index];
        const WebRendererDrawDesc &draw = entry.draw;
        if (draw.topology != WebRendererPrimitiveTopology::TriangleList)
            return WebRendererSurfaceResult::UnsupportedTopology;
        if (draw.textureBinding != WebRendererTextureBinding::None &&
            draw.textureBinding != WebRendererTextureBinding::EngineImage)
            return WebRendererSurfaceResult::UnsupportedTextureBinding;
        if (entry.textureSlot >= drawList.textureCount || draw.indexCount == 0u ||
            draw.firstIndex % 3u != 0u || draw.indexCount % 3u != 0u ||
            draw.firstIndex > surface.indexCount ||
            draw.indexCount > surface.indexCount - draw.firstIndex)
        {
            return WebRendererSurfaceResult::InvalidDescriptor;
        }
    }
    for (std::uint32_t vertexIndex = 0u; vertexIndex < surface.vertexCount; ++vertexIndex)
    {
        const WebRendererSurfaceVertex &vertex = surface.vertices[vertexIndex];
        for (float component : vertex.position)
            if (!std::isfinite(component)) return WebRendererSurfaceResult::NonFiniteVertex;
        for (float component : vertex.color)
            if (!std::isfinite(component)) return WebRendererSurfaceResult::NonFiniteVertex;
        for (float component : vertex.textureCoordinate)
            if (!std::isfinite(component)) return WebRendererSurfaceResult::NonFiniteVertex;
    }
    for (std::uint32_t index = 0u; index < surface.indexCount; ++index)
        if (surface.indices[index] >= surface.vertexCount)
            return WebRendererSurfaceResult::IndexOutOfRange;
    return WebRendererSurfaceResult::Success;
}

WebRendererSurfaceResult WebRenderer_CopyDrawList(
    const WebRendererDrawListDesc &drawList,
    WebRendererOwnedDrawList &destination)
{
    const WebRendererSurfaceResult validation = WebRenderer_ValidateDrawList(drawList);
    if (validation != WebRendererSurfaceResult::Success) return validation;

    WebRendererOwnedDrawList replacement;
    try
    {
        replacement.vertices.assign(
            drawList.surface.vertices,
            drawList.surface.vertices + drawList.surface.vertexCount);
        replacement.indices.assign(
            drawList.surface.indices,
            drawList.surface.indices + drawList.surface.indexCount);
        replacement.draws.assign(drawList.draws, drawList.draws + drawList.drawCount);
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererSurfaceResult::AllocationFailed;
    }
    replacement.textureCount = drawList.textureCount;
    destination = std::move(replacement);
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
