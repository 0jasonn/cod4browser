#pragma once

#include <web/web_renderer_surface.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

// D3D-free mirror of the geometry fields consumed from upstream
// GfxWorldVertex. The offsets and 44-byte size match the runtime world-vertex
// layout, but no Direct3D declaration or buffer object crosses this boundary.
struct WebEngineWorldVertex
{
    float xyz[3];
    float binormalSign;
    std::uint32_t color;
    float textureCoordinate[2];
    float lightmapCoordinate[2];
    std::uint32_t normal;
    std::uint32_t tangent;
};

static_assert(std::is_standard_layout_v<WebEngineWorldVertex>);
static_assert(sizeof(WebEngineWorldVertex) == 44u);
static_assert(offsetof(WebEngineWorldVertex, xyz) == 0u);
static_assert(offsetof(WebEngineWorldVertex, binormalSign) == 12u);
static_assert(offsetof(WebEngineWorldVertex, color) == 16u);
static_assert(offsetof(WebEngineWorldVertex, textureCoordinate) == 20u);
static_assert(offsetof(WebEngineWorldVertex, lightmapCoordinate) == 28u);
static_assert(offsetof(WebEngineWorldVertex, normal) == 36u);
static_assert(offsetof(WebEngineWorldVertex, tangent) == 40u);

// Mirrors upstream srfTriangles_t. firstVertex and baseIndex remain signed so
// malformed native descriptions can be rejected before conversion to offsets.
struct WebEngineWorldSurfaceRange
{
    std::int32_t vertexLayerData;
    std::int32_t firstVertex;
    std::uint16_t vertexCount;
    std::uint16_t triangleCount;
    std::int32_t baseIndex;
};

static_assert(std::is_standard_layout_v<WebEngineWorldSurfaceRange>);
static_assert(sizeof(WebEngineWorldSurfaceRange) == 16u);
static_assert(offsetof(WebEngineWorldSurfaceRange, firstVertex) == 4u);
static_assert(offsetof(WebEngineWorldSurfaceRange, vertexCount) == 8u);
static_assert(offsetof(WebEngineWorldSurfaceRange, triangleCount) == 10u);
static_assert(offsetof(WebEngineWorldSurfaceRange, baseIndex) == 12u);

enum class WebEngineWorldVertexFormat : std::uint8_t
{
    // These are boundary semantics, not upstream TrisType numeric values.
    // A future GfxWorld adapter must map deliberately rather than static_cast.
    Base = 0,
    Layered,
};

struct WebEngineWorldGeometryView
{
    const WebEngineWorldVertex *vertices;
    std::uint32_t vertexCount;
    const std::uint16_t *indices;
    std::uint32_t indexCount;
};

struct WebEngineWorldSurfaceView
{
    WebEngineWorldGeometryView world;
    WebEngineWorldSurfaceRange surface;
    WebEngineWorldVertexFormat vertexFormat;
};

// The current renderer consumes two-dimensional clip positions. This explicit
// affine projection is the narrow engine-side bridge from xyz: each output row
// is {x, y, z, translation}. Perspective, clipping, and a camera remain later
// world-renderer work rather than implicit behavior in the WebGL backend.
struct WebEngineWorldProjection2D
{
    float clipXFromWorld[4];
    float clipYFromWorld[4];
};

struct WebEngineConvertedWorldSurface
{
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint16_t> indices;
    WebRendererDrawDesc draw{
        WebRendererPrimitiveTopology::TriangleList,
        0u,
        0u,
        WebRendererTextureBinding::EngineImage,
    };
};

enum class WebEngineWorldSurfaceResult : std::uint8_t
{
    Success = 0,
    InvalidDescriptor,
    UnsupportedVertexFormat,
    InvalidRange,
    OutputTooLarge,
    NonFiniteProjection,
    NonFiniteVertex,
    IndexOutOfRange,
    AllocationFailed,
};

// Converts exactly one surface-local slice. Source indices are local to
// firstVertex in upstream GfxWorld and remain local in the renderer output.
// Conversion is atomic: destination is unchanged on every failure.
WebEngineWorldSurfaceResult WebEngine_ConvertWorldSurface(
    const WebEngineWorldSurfaceView &source,
    const WebEngineWorldProjection2D &projection,
    WebEngineConvertedWorldSurface &destination);

const char *WebEngine_WorldSurfaceResultString(
    WebEngineWorldSurfaceResult result) noexcept;
