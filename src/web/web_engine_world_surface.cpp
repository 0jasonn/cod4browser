#include <web/web_engine_world_surface.h>

#include <gfx_d3d/gfx_world_types.h>

#include <cmath>
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <functional>
#include <utility>

namespace
{
constexpr float BYTE_TO_UNIT = 1.0f / 255.0f;
constexpr float GFXWORLD_CLIP_MARGIN = 0.82f;

static_assert(std::is_standard_layout_v<GfxWorldVertex>);
static_assert(sizeof(GfxWorldVertex) == sizeof(WebEngineWorldVertex));
static_assert(offsetof(GfxWorldVertex, xyz) == offsetof(WebEngineWorldVertex, xyz));
static_assert(offsetof(GfxWorldVertex, binormalSign) ==
    offsetof(WebEngineWorldVertex, binormalSign));
static_assert(offsetof(GfxWorldVertex, color) == offsetof(WebEngineWorldVertex, color));
static_assert(offsetof(GfxWorldVertex, texCoord) ==
    offsetof(WebEngineWorldVertex, textureCoordinate));
static_assert(offsetof(GfxWorldVertex, lmapCoord) ==
    offsetof(WebEngineWorldVertex, lightmapCoordinate));
static_assert(offsetof(GfxWorldVertex, normal) == offsetof(WebEngineWorldVertex, normal));
static_assert(offsetof(GfxWorldVertex, tangent) == offsetof(WebEngineWorldVertex, tangent));
static_assert(sizeof(srfTriangles_t) == sizeof(WebEngineWorldSurfaceRange));

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

WebEngineGfxWorldSurfaceResult WebEngine_BuildGfxWorldSurface(
    const GfxWorld &world,
    WebEngineGfxWorldSurfacePublication &destination)
{
    if (world.vertexCount == 0u || world.indexCount <= 0 || world.surfaceCount <= 0 ||
        world.vd.vertices == nullptr || world.indices == nullptr ||
        world.dpvs.surfaces == nullptr)
    {
        return WebEngineGfxWorldSurfaceResult::InvalidWorld;
    }

    std::uint32_t selectedSurfaceIndex = UINT32_MAX;
    std::uint16_t selectedTriangleCount = 0u;
    for (std::uint32_t surfaceIndex = 0u;
         surfaceIndex < static_cast<std::uint32_t>(world.surfaceCount);
         ++surfaceIndex)
    {
        const GfxSurface &candidate = world.dpvs.surfaces[surfaceIndex];
        if (candidate.tris.vertexLayerData != 0 ||
            candidate.tris.vertexCount == 0u || candidate.tris.triCount == 0u ||
            candidate.tris.vertexCount > WEB_RENDERER_MAX_SURFACE_VERTICES ||
            candidate.tris.triCount > WEB_RENDERER_MAX_SURFACE_INDICES / 3u)
        {
            continue;
        }
        std::array<float, 3> extents{};
        bool validBounds = true;
        for (std::size_t axis = 0u; axis < extents.size(); ++axis)
        {
            const float minimum = candidate.bounds[0][axis];
            const float maximum = candidate.bounds[1][axis];
            if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
            {
                return WebEngineGfxWorldSurfaceResult::InvalidSurfaceBounds;
            }
            extents[axis] = maximum - minimum;
        }
        std::sort(extents.begin(), extents.end(), std::greater<float>());
        validBounds = extents[0] > 0.0f && extents[1] > 0.0f;
        if (!validBounds || candidate.tris.triCount <= selectedTriangleCount)
            continue;
        selectedSurfaceIndex = surfaceIndex;
        selectedTriangleCount = candidate.tris.triCount;
    }
    if (selectedSurfaceIndex == UINT32_MAX)
        return WebEngineGfxWorldSurfaceResult::NoRenderableSurface;

    for (std::uint32_t surfaceIndex = 0u;
         surfaceIndex < static_cast<std::uint32_t>(world.surfaceCount);
         ++surfaceIndex)
    {
        if (surfaceIndex != selectedSurfaceIndex) continue;
        const GfxSurface &surface = world.dpvs.surfaces[surfaceIndex];
        if (surface.tris.vertexLayerData != 0 || surface.tris.vertexCount == 0u ||
            surface.tris.triCount == 0u ||
            surface.tris.vertexCount > WEB_RENDERER_MAX_SURFACE_VERTICES ||
            surface.tris.triCount > WEB_RENDERER_MAX_SURFACE_INDICES / 3u)
        {
            continue;
        }

        std::array<std::uint8_t, 3> axes{0u, 1u, 2u};
        std::array<float, 3> extents{};
        for (std::size_t axis = 0u; axis < axes.size(); ++axis)
        {
            const float minimum = surface.bounds[0][axis];
            const float maximum = surface.bounds[1][axis];
            if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
            {
                return WebEngineGfxWorldSurfaceResult::InvalidSurfaceBounds;
            }
            extents[axis] = maximum - minimum;
        }
        std::stable_sort(axes.begin(), axes.end(), [&extents](std::uint8_t left,
                                                               std::uint8_t right) {
            return extents[left] > extents[right];
        });
        const float horizontalExtent = extents[axes[0]];
        const float verticalExtent = extents[axes[1]];
        const float largestExtent = std::max(horizontalExtent, verticalExtent);
        if (!std::isfinite(largestExtent) || largestExtent <= 0.0f ||
            verticalExtent <= 0.0f)
        {
            continue;
        }

        const float scale = 2.0f * GFXWORLD_CLIP_MARGIN / largestExtent;
        WebEngineWorldProjection2D projection{};
        projection.clipXFromWorld[axes[0]] = scale;
        projection.clipXFromWorld[3] = -0.5f *
            (surface.bounds[0][axes[0]] + surface.bounds[1][axes[0]]) * scale;
        projection.clipYFromWorld[axes[1]] = scale;
        projection.clipYFromWorld[3] = -0.5f *
            (surface.bounds[0][axes[1]] + surface.bounds[1][axes[1]]) * scale;

        const WebEngineWorldSurfaceView view{
            {
                reinterpret_cast<const WebEngineWorldVertex *>(world.vd.vertices),
                world.vertexCount,
                world.indices,
                static_cast<std::uint32_t>(world.indexCount),
            },
            {
                surface.tris.vertexLayerData,
                surface.tris.firstVertex,
                surface.tris.vertexCount,
                surface.tris.triCount,
                surface.tris.baseIndex,
            },
            WebEngineWorldVertexFormat::Base,
        };

        WebEngineConvertedWorldSurface converted;
        const WebEngineWorldSurfaceResult conversion =
            WebEngine_ConvertWorldSurface(view, projection, converted);
        if (conversion != WebEngineWorldSurfaceResult::Success)
        {
            return conversion == WebEngineWorldSurfaceResult::AllocationFailed
                ? WebEngineGfxWorldSurfaceResult::AllocationFailed
                : WebEngineGfxWorldSurfaceResult::ConversionFailed;
        }

        const std::uint8_t depthAxis = axes[2];
        const float depthCenter = 0.5f *
            (surface.bounds[0][depthAxis] + surface.bounds[1][depthAxis]);
        const auto *sourceVertices =
            world.vd.vertices + static_cast<std::uint32_t>(surface.tris.firstVertex);
        for (std::size_t index = 0u; index < converted.vertices.size(); ++index)
        {
            converted.vertices[index].position[2] =
                (sourceVertices[index].xyz[depthAxis] - depthCenter) * scale;
        }

        WebEngineGfxWorldSurfacePublication replacement;
        replacement.surfaceIndex = surfaceIndex;
        replacement.vertexCount = surface.tris.vertexCount;
        replacement.triangleCount = surface.tris.triCount;
        replacement.material = surface.material;
        replacement.materialName = surface.material ? surface.material->info.name : nullptr;
        replacement.horizontalAxis = axes[0];
        replacement.verticalAxis = axes[1];
        replacement.depthAxis = axes[2];
        std::copy(std::begin(surface.bounds[0]), std::end(surface.bounds[0]),
            replacement.mins.begin());
        std::copy(std::begin(surface.bounds[1]), std::end(surface.bounds[1]),
            replacement.maxs.begin());
        replacement.rendererSurface = std::move(converted);
        destination = std::move(replacement);
        return WebEngineGfxWorldSurfaceResult::Success;
    }
    return WebEngineGfxWorldSurfaceResult::NoRenderableSurface;
}

const char *WebEngine_GfxWorldSurfaceResultString(
    WebEngineGfxWorldSurfaceResult result) noexcept
{
    switch (result)
    {
    case WebEngineGfxWorldSurfaceResult::Success: return "success";
    case WebEngineGfxWorldSurfaceResult::InvalidWorld:
        return "invalid canonical GfxWorld geometry";
    case WebEngineGfxWorldSurfaceResult::NoRenderableSurface:
        return "GfxWorld contains no bounded base surface";
    case WebEngineGfxWorldSurfaceResult::InvalidSurfaceBounds:
        return "GfxWorld surface contains invalid bounds";
    case WebEngineGfxWorldSurfaceResult::ConversionFailed:
        return "GfxWorld surface conversion failed validation";
    case WebEngineGfxWorldSurfaceResult::AllocationFailed:
        return "GfxWorld surface conversion allocation failed";
    }
    return "unknown GfxWorld surface adapter error";
}
