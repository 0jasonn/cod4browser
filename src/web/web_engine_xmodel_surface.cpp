#include <web/web_engine_xmodel_surface.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t PACKED_VERTEX_BYTES = 32u;
constexpr float CLIP_MARGIN = 0.82f;

std::uint16_t ReadU16(const std::uint8_t *bytes) noexcept
{
    return static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
}

std::uint32_t ReadU32(const std::uint8_t *bytes) noexcept
{
    return static_cast<std::uint32_t>(bytes[0]) |
        static_cast<std::uint32_t>(bytes[1]) << 8u |
        static_cast<std::uint32_t>(bytes[2]) << 16u |
        static_cast<std::uint32_t>(bytes[3]) << 24u;
}

float ReadF32(const std::uint8_t *bytes) noexcept
{
    return std::bit_cast<float>(ReadU32(bytes));
}

float UnpackHalf(std::uint16_t packed) noexcept
{
    const bool negative = (packed & 0x8000u) != 0u;
    const std::uint32_t exponent = (packed >> 10u) & 0x1fu;
    const std::uint32_t fraction = packed & 0x03ffu;
    float value = 0.0f;
    if (exponent == 0u)
    {
        value = fraction == 0u
            ? 0.0f
            : std::ldexp(static_cast<float>(fraction), -24);
    }
    else if (exponent == 0x1fu)
    {
        value = fraction == 0u
            ? std::numeric_limits<float>::infinity()
            : std::numeric_limits<float>::quiet_NaN();
    }
    else
    {
        value = std::ldexp(static_cast<float>(1024u + fraction),
            static_cast<int>(exponent) - 25);
    }
    return negative ? -value : value;
}
} // namespace

namespace
{
WebEngineXModelSurfaceResult ConvertPackedXModelSurface(
    const WebEnginePackedXSurfaceView &source,
    const WebEngineXModelProjectionBounds *projectionBounds,
    WebEngineConvertedXModelSurface &destination)
{
    if (source.packedVertices == nullptr || source.packedIndices == nullptr ||
        source.vertexCount == 0u || source.triangleCount == 0u)
    {
        return WebEngineXModelSurfaceResult::InvalidDescriptor;
    }
    if (source.materialIdentity == 0u)
    {
        return WebEngineXModelSurfaceResult::MissingMaterial;
    }
    if (source.vertexCount > WEB_RENDERER_MAX_SURFACE_VERTICES ||
        source.triangleCount > WEB_RENDERER_MAX_SURFACE_INDICES / 3u)
    {
        return WebEngineXModelSurfaceResult::OutputTooLarge;
    }
    const std::size_t expectedVertexBytes =
        static_cast<std::size_t>(source.vertexCount) * PACKED_VERTEX_BYTES;
    const std::size_t expectedIndexBytes =
        static_cast<std::size_t>(source.triangleCount) * 3u * sizeof(std::uint16_t);
    if (source.packedVertexBytes != expectedVertexBytes ||
        source.packedIndexBytes != expectedIndexBytes)
    {
        return WebEngineXModelSurfaceResult::InvalidDescriptor;
    }

    std::vector<WebEngineWorldVertex> decodedVertices;
    std::vector<std::uint16_t> decodedIndices;
    std::array<float, 3> mins{
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
    };
    std::array<float, 3> maxs{
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    try
    {
        decodedVertices.reserve(source.vertexCount);
        decodedIndices.reserve(source.triangleCount * 3u);
        for (std::uint32_t index = 0u; index < source.vertexCount; ++index)
        {
            const std::uint8_t *record =
                source.packedVertices + static_cast<std::size_t>(index) * PACKED_VERTEX_BYTES;
            WebEngineWorldVertex vertex{};
            for (std::size_t axis = 0u; axis < 3u; ++axis)
            {
                vertex.xyz[axis] = ReadF32(record + axis * sizeof(float));
                if (!std::isfinite(vertex.xyz[axis]))
                    return WebEngineXModelSurfaceResult::NonFiniteVertex;
                mins[axis] = std::min(mins[axis], vertex.xyz[axis]);
                maxs[axis] = std::max(maxs[axis], vertex.xyz[axis]);
            }
            vertex.binormalSign = ReadF32(record + 12u);
            vertex.color = ReadU32(record + 16u);
            const std::uint32_t packedTexCoord = ReadU32(record + 20u);
            // Vec2UnpackTexCoords stores U in the high half and V in the low
            // half of the packed little-endian dword.
            vertex.textureCoordinate[0] = UnpackHalf(
                static_cast<std::uint16_t>(packedTexCoord >> 16u));
            vertex.textureCoordinate[1] = UnpackHalf(
                static_cast<std::uint16_t>(packedTexCoord & 0xffffu));
            vertex.normal = ReadU32(record + 24u);
            vertex.tangent = ReadU32(record + 28u);
            if (!std::isfinite(vertex.binormalSign) ||
                !std::isfinite(vertex.textureCoordinate[0]) ||
                !std::isfinite(vertex.textureCoordinate[1]))
            {
                return WebEngineXModelSurfaceResult::NonFiniteVertex;
            }
            decodedVertices.push_back(vertex);
        }
        for (std::uint32_t index = 0u; index < source.triangleCount * 3u; ++index)
        {
            const std::uint16_t decoded = ReadU16(
                source.packedIndices + static_cast<std::size_t>(index) * 2u);
            if (decoded >= source.vertexCount)
                return WebEngineXModelSurfaceResult::IndexOutOfRange;
            decodedIndices.push_back(decoded);
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebEngineXModelSurfaceResult::AllocationFailed;
    }

    const std::array<float, 3> &projectionMins =
        projectionBounds ? projectionBounds->mins : mins;
    const std::array<float, 3> &projectionMaxs =
        projectionBounds ? projectionBounds->maxs : maxs;
    for (std::size_t axis = 0u; axis < 3u; ++axis)
    {
        if (!std::isfinite(projectionMins[axis]) ||
            !std::isfinite(projectionMaxs[axis]) ||
            projectionMins[axis] > projectionMaxs[axis])
        {
            return WebEngineXModelSurfaceResult::DegenerateProjection;
        }
    }

    std::array<std::uint8_t, 3> axes{0u, 1u, 2u};
    for (std::size_t left = 0u; left + 1u < axes.size(); ++left)
    {
        for (std::size_t right = left + 1u; right < axes.size(); ++right)
        {
            if (projectionMaxs[axes[right]] - projectionMins[axes[right]] >
                projectionMaxs[axes[left]] - projectionMins[axes[left]])
            {
                std::swap(axes[left], axes[right]);
            }
        }
    }
    const float horizontalExtent =
        projectionMaxs[axes[0]] - projectionMins[axes[0]];
    const float verticalExtent =
        projectionMaxs[axes[1]] - projectionMins[axes[1]];
    const float largestExtent = std::max(horizontalExtent, verticalExtent);
    if (!std::isfinite(largestExtent) || largestExtent <= 0.0f ||
        verticalExtent <= 0.0f)
    {
        return WebEngineXModelSurfaceResult::DegenerateProjection;
    }
    const float scale = 2.0f * CLIP_MARGIN / largestExtent;
    WebEngineWorldProjection2D projection{};
    projection.clipXFromWorld[axes[0]] = scale;
    projection.clipXFromWorld[3] =
        -0.5f * (projectionMins[axes[0]] + projectionMaxs[axes[0]]) * scale;
    projection.clipYFromWorld[axes[1]] = scale;
    projection.clipYFromWorld[3] =
        -0.5f * (projectionMins[axes[1]] + projectionMaxs[axes[1]]) * scale;

    WebEngineConvertedWorldSurface converted;
    const WebEngineWorldSurfaceView worldView{
        {
            decodedVertices.data(),
            static_cast<std::uint32_t>(decodedVertices.size()),
            decodedIndices.data(),
            static_cast<std::uint32_t>(decodedIndices.size()),
        },
        {
            0,
            0,
            static_cast<std::uint16_t>(decodedVertices.size()),
            static_cast<std::uint16_t>(source.triangleCount),
            0,
        },
        WebEngineWorldVertexFormat::Base,
    };
    const WebEngineWorldSurfaceResult conversion =
        WebEngine_ConvertWorldSurface(worldView, projection, converted);
    if (conversion != WebEngineWorldSurfaceResult::Success)
    {
        return conversion == WebEngineWorldSurfaceResult::AllocationFailed
            ? WebEngineXModelSurfaceResult::AllocationFailed
            : WebEngineXModelSurfaceResult::ConversionFailed;
    }

    const std::uint8_t depthAxis = axes[2];
    const float depthCenter =
        0.5f * (projectionMins[depthAxis] + projectionMaxs[depthAxis]);
    for (std::size_t index = 0u; index < decodedVertices.size(); ++index)
    {
        // Keep the third model-space axis for depth testing. It uses the same
        // model-wide scale as X/Y, so the orthographic preview preserves the
        // mesh's spatial proportions instead of flattening every triangle.
        converted.vertices[index].position[2] =
            (decodedVertices[index].xyz[depthAxis] - depthCenter) * scale;
    }

    WebEngineConvertedXModelSurface replacement;
    replacement.rendererSurface = std::move(converted);
    replacement.mins = mins;
    replacement.maxs = maxs;
    replacement.materialIdentity = source.materialIdentity;
    replacement.horizontalAxis = axes[0];
    replacement.verticalAxis = axes[1];
    destination = std::move(replacement);
    return WebEngineXModelSurfaceResult::Success;
}
} // namespace

WebEngineXModelSurfaceResult WebEngine_ConvertPackedXModelSurface(
    const WebEnginePackedXSurfaceView &source,
    WebEngineConvertedXModelSurface &destination)
{
    return ConvertPackedXModelSurface(source, nullptr, destination);
}

WebEngineXModelSurfaceResult WebEngine_ConvertPackedXModelSurfaceWithBounds(
    const WebEnginePackedXSurfaceView &source,
    const WebEngineXModelProjectionBounds &projectionBounds,
    WebEngineConvertedXModelSurface &destination)
{
    return ConvertPackedXModelSurface(source, &projectionBounds, destination);
}

const char *WebEngine_XModelSurfaceResultString(
    WebEngineXModelSurfaceResult result) noexcept
{
    switch (result)
    {
    case WebEngineXModelSurfaceResult::Success: return "success";
    case WebEngineXModelSurfaceResult::InvalidDescriptor:
        return "invalid packed XModel surface descriptor";
    case WebEngineXModelSurfaceResult::MissingMaterial:
        return "XModel surface has no resolved material identity";
    case WebEngineXModelSurfaceResult::OutputTooLarge:
        return "XModel surface exceeds the bounded renderer slice";
    case WebEngineXModelSurfaceResult::NonFiniteVertex:
        return "XModel surface contains a non-finite packed vertex";
    case WebEngineXModelSurfaceResult::DegenerateProjection:
        return "XModel surface has fewer than two non-degenerate spatial axes";
    case WebEngineXModelSurfaceResult::IndexOutOfRange:
        return "XModel surface contains an out-of-range local index";
    case WebEngineXModelSurfaceResult::AllocationFailed:
        return "XModel surface conversion allocation failed";
    case WebEngineXModelSurfaceResult::ConversionFailed:
        return "decoded XModel surface failed engine conversion";
    }
    return "unknown XModel surface conversion error";
}
