#pragma once

#include <web/web_engine_world_surface.h>

#include <array>
#include <cstddef>
#include <cstdint>

// Callback-scoped view of one serialized COD4 GfxPackedVertex XSurface. The
// 32-byte records and little-endian indices remain parser-owned; conversion
// copies only validated, backend-neutral renderer values.
struct WebEnginePackedXSurfaceView
{
    const std::uint8_t *packedVertices = nullptr;
    std::size_t packedVertexBytes = 0u;
    std::uint32_t vertexCount = 0u;
    const std::uint8_t *packedIndices = nullptr;
    std::size_t packedIndexBytes = 0u;
    std::uint32_t triangleCount = 0u;
    std::uint32_t materialIdentity = 0u;
};

struct WebEngineConvertedXModelSurface
{
    WebEngineConvertedWorldSurface rendererSurface;
    std::array<float, 3> mins{};
    std::array<float, 3> maxs{};
    std::uint32_t materialIdentity = 0u;
    std::uint8_t horizontalAxis = 0u;
    std::uint8_t verticalAxis = 1u;
};

enum class WebEngineXModelSurfaceResult : std::uint8_t
{
    Success = 0,
    InvalidDescriptor,
    MissingMaterial,
    OutputTooLarge,
    NonFiniteVertex,
    DegenerateProjection,
    IndexOutOfRange,
    AllocationFailed,
    ConversionFailed,
};

// Decodes one complete packed surface and fits its two largest spatial axes to
// a conservative clip-space square. Destination is unchanged on every error.
WebEngineXModelSurfaceResult WebEngine_ConvertPackedXModelSurface(
    const WebEnginePackedXSurfaceView &source,
    WebEngineConvertedXModelSurface &destination);

const char *WebEngine_XModelSurfaceResultString(
    WebEngineXModelSurfaceResult result) noexcept;
