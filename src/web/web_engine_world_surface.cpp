#include <web/web_engine_world_surface.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>

namespace
{
constexpr float BYTE_TO_UNIT = 1.0f / 255.0f;

bool ProjectionIsFinite(const WebEngineWorldProjection2D &projection) noexcept
{
    for (float component : projection.clipXFromWorld)
    {
        if (!std::isfinite(component))
        {
            return false;
        }
    }
    for (float component : projection.clipYFromWorld)
    {
        if (!std::isfinite(component))
        {
            return false;
        }
    }
    return true;
}

bool VertexFloatsAreFinite(const WebEngineWorldVertex &vertex) noexcept
{
    for (float component : vertex.xyz)
    {
        if (!std::isfinite(component))
        {
            return false;
        }
    }
    if (!std::isfinite(vertex.binormalSign))
    {
        return false;
    }
    for (float component : vertex.textureCoordinate)
    {
        if (!std::isfinite(component))
        {
            return false;
        }
    }
    for (float component : vertex.lightmapCoordinate)
    {
        if (!std::isfinite(component))
        {
            return false;
        }
    }
    return true;
}

bool ProjectCoordinate(
    const float row[4],
    const float xyz[3],
    float &coordinateOut) noexcept
{
    const double coordinate =
        static_cast<double>(row[0]) * xyz[0] +
        static_cast<double>(row[1]) * xyz[1] +
        static_cast<double>(row[2]) * xyz[2] +
        static_cast<double>(row[3]);
    if (!std::isfinite(coordinate) ||
        coordinate < -static_cast<double>(std::numeric_limits<float>::max()) ||
        coordinate > static_cast<double>(std::numeric_limits<float>::max()))
    {
        return false;
    }
    coordinateOut = static_cast<float>(coordinate);
    return std::isfinite(coordinateOut);
}
} // namespace

WebEngineWorldSurfaceResult WebEngine_ConvertWorldSurface(
    const WebEngineWorldSurfaceView &source,
    const WebEngineWorldProjection2D &projection,
    WebEngineConvertedWorldSurface &destination)
{
    if (source.world.vertices == nullptr || source.world.indices == nullptr ||
        source.world.vertexCount == 0u || source.world.indexCount == 0u ||
        source.surface.vertexCount == 0u || source.surface.triangleCount == 0u)
    {
        return WebEngineWorldSurfaceResult::InvalidDescriptor;
    }
    if (source.vertexFormat != WebEngineWorldVertexFormat::Base)
    {
        return WebEngineWorldSurfaceResult::UnsupportedVertexFormat;
    }
    if (source.surface.vertexCount > WEB_RENDERER_MAX_SURFACE_VERTICES ||
        source.surface.triangleCount > WEB_RENDERER_MAX_SURFACE_INDICES / 3u)
    {
        return WebEngineWorldSurfaceResult::OutputTooLarge;
    }
    if (!ProjectionIsFinite(projection))
    {
        return WebEngineWorldSurfaceResult::NonFiniteProjection;
    }
    if (source.surface.firstVertex < 0 || source.surface.baseIndex < 0)
    {
        return WebEngineWorldSurfaceResult::InvalidRange;
    }

    const std::uint32_t firstVertex =
        static_cast<std::uint32_t>(source.surface.firstVertex);
    const std::uint32_t firstIndex =
        static_cast<std::uint32_t>(source.surface.baseIndex);
    const std::uint32_t vertexCount = source.surface.vertexCount;
    const std::uint32_t indexCount =
        static_cast<std::uint32_t>(source.surface.triangleCount) * 3u;
    if (firstVertex > source.world.vertexCount ||
        vertexCount > source.world.vertexCount - firstVertex ||
        firstIndex > source.world.indexCount ||
        indexCount > source.world.indexCount - firstIndex)
    {
        return WebEngineWorldSurfaceResult::InvalidRange;
    }

    WebEngineConvertedWorldSurface replacement;
    try
    {
        replacement.vertices.reserve(vertexCount);
        replacement.indices.reserve(indexCount);
        for (std::uint32_t localVertex = 0; localVertex < vertexCount; ++localVertex)
        {
            const WebEngineWorldVertex &sourceVertex =
                source.world.vertices[firstVertex + localVertex];
            if (!VertexFloatsAreFinite(sourceVertex))
            {
                return WebEngineWorldSurfaceResult::NonFiniteVertex;
            }

            WebRendererSurfaceVertex converted{};
            if (!ProjectCoordinate(
                    projection.clipXFromWorld,
                    sourceVertex.xyz,
                    converted.position[0]) ||
                !ProjectCoordinate(
                    projection.clipYFromWorld,
                    sourceVertex.xyz,
                    converted.position[1]))
            {
                return WebEngineWorldSurfaceResult::NonFiniteProjection;
            }

            // Upstream native GfxColor is numerically 0xAARRGGBB. Decode by
            // shifts so the conversion is independent of host byte order.
            converted.color[0] =
                static_cast<float>((sourceVertex.color >> 16u) & 0xffu) * BYTE_TO_UNIT;
            converted.color[1] =
                static_cast<float>((sourceVertex.color >> 8u) & 0xffu) * BYTE_TO_UNIT;
            converted.color[2] =
                static_cast<float>(sourceVertex.color & 0xffu) * BYTE_TO_UNIT;
            converted.textureCoordinate[0] = sourceVertex.textureCoordinate[0];
            converted.textureCoordinate[1] = sourceVertex.textureCoordinate[1];
            replacement.vertices.push_back(converted);
        }

        for (std::uint32_t localIndex = 0; localIndex < indexCount; ++localIndex)
        {
            const std::uint16_t index = source.world.indices[firstIndex + localIndex];
            if (index >= vertexCount)
            {
                return WebEngineWorldSurfaceResult::IndexOutOfRange;
            }
            replacement.indices.push_back(index);
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebEngineWorldSurfaceResult::AllocationFailed;
    }

    replacement.draw = {
        WebRendererPrimitiveTopology::TriangleList,
        0u,
        indexCount,
        WebRendererTextureBinding::EngineImage,
    };
    destination.vertices.swap(replacement.vertices);
    destination.indices.swap(replacement.indices);
    destination.draw = replacement.draw;
    return WebEngineWorldSurfaceResult::Success;
}

const char *WebEngine_WorldSurfaceResultString(
    WebEngineWorldSurfaceResult result) noexcept
{
    switch (result)
    {
    case WebEngineWorldSurfaceResult::Success: return "success";
    case WebEngineWorldSurfaceResult::InvalidDescriptor:
        return "invalid engine world-surface descriptor";
    case WebEngineWorldSurfaceResult::UnsupportedVertexFormat:
        return "unsupported engine world vertex format";
    case WebEngineWorldSurfaceResult::InvalidRange:
        return "engine world-surface range is outside its shared arrays";
    case WebEngineWorldSurfaceResult::OutputTooLarge:
        return "engine world surface exceeds the bounded renderer slice";
    case WebEngineWorldSurfaceResult::NonFiniteProjection:
        return "engine world-surface projection is not finite";
    case WebEngineWorldSurfaceResult::NonFiniteVertex:
        return "engine world surface contains a non-finite vertex";
    case WebEngineWorldSurfaceResult::IndexOutOfRange:
        return "engine world surface contains an out-of-range local index";
    case WebEngineWorldSurfaceResult::AllocationFailed:
        return "engine world-surface conversion allocation failed";
    }
    return "unknown engine world-surface conversion error";
}
