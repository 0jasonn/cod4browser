#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// The original single-surface seam remains available for bootstrap callers.
// M29 adds explicit aggregate limits for a modest first-LOD draw list. The
// fixed layout and 16-bit indices remain backend-neutral; graphics handles and
// vertex declaration objects stay private to the renderer.
constexpr std::uint32_t WEB_RENDERER_MAX_SURFACE_VERTICES = 4096u;
constexpr std::uint32_t WEB_RENDERER_MAX_SURFACE_INDICES = 12288u;
constexpr std::size_t WEB_RENDERER_MAX_RETAINED_SURFACE_BYTES =
    static_cast<std::size_t>(WEB_RENDERER_MAX_SURFACE_VERTICES) * 8u * sizeof(float) +
    static_cast<std::size_t>(WEB_RENDERER_MAX_SURFACE_INDICES) * sizeof(std::uint16_t);

constexpr std::uint32_t WEB_RENDERER_MAX_DRAW_LIST_DRAWS = 16u;
constexpr std::uint32_t WEB_RENDERER_MAX_DRAW_LIST_TEXTURES = 8u;
constexpr std::uint32_t WEB_RENDERER_MAX_DRAW_LIST_VERTICES = 16384u;
constexpr std::uint32_t WEB_RENDERER_MAX_DRAW_LIST_INDICES = 49152u;
constexpr std::size_t WEB_RENDERER_MAX_RETAINED_DRAW_LIST_BYTES =
    static_cast<std::size_t>(WEB_RENDERER_MAX_DRAW_LIST_VERTICES) * 8u * sizeof(float) +
    static_cast<std::size_t>(WEB_RENDERER_MAX_DRAW_LIST_INDICES) * sizeof(std::uint16_t);

struct WebRendererSurfaceVertex
{
    float position[3];
    float color[3];
    float textureCoordinate[2];
};

static_assert(std::is_standard_layout_v<WebRendererSurfaceVertex>);
static_assert(sizeof(WebRendererSurfaceVertex) == 8u * sizeof(float));

enum class WebRendererPrimitiveTopology : std::uint8_t
{
    TriangleList = 0,
};

enum class WebRendererTextureBinding : std::uint8_t
{
    None = 0,
    EngineImage,
};

struct WebRendererSurfaceDesc
{
    const WebRendererSurfaceVertex *vertices;
    std::uint32_t vertexCount;
    const std::uint16_t *indices;
    std::uint32_t indexCount;
};

struct WebRendererDrawDesc
{
    WebRendererPrimitiveTopology topology;
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
    WebRendererTextureBinding textureBinding;
};

struct WebRendererDrawListDrawDesc
{
    WebRendererDrawDesc draw;
    std::uint32_t textureSlot;
};

struct WebRendererDrawListDesc
{
    WebRendererSurfaceDesc surface;
    const WebRendererDrawListDrawDesc *draws;
    std::uint32_t drawCount;
    std::uint32_t textureCount;
};

enum class WebRendererSurfaceResult : std::uint8_t
{
    Success = 0,
    InvalidDescriptor,
    UnsupportedTopology,
    UnsupportedTextureBinding,
    OutputTooLarge,
    NonFiniteVertex,
    IndexOutOfRange,
    AllocationFailed,
    BackendFailure,
};

// Validates the complete callback-scoped descriptor before the renderer copies
// or uploads any part of it. A failure leaves the currently active surface
// unchanged.
WebRendererSurfaceResult WebRenderer_ValidateSurface(
    const WebRendererSurfaceDesc &surface,
    const WebRendererDrawDesc &draw) noexcept;

WebRendererSurfaceResult WebRenderer_ValidateDrawList(
    const WebRendererDrawListDesc &drawList) noexcept;

const char *WebRenderer_SurfaceResultString(WebRendererSurfaceResult result) noexcept;
