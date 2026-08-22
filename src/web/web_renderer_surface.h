#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// The fixed layout and 16-bit indices remain backend-neutral; graphics handles
// and vertex declaration objects stay private to the renderer.
constexpr std::uint32_t WEB_RENDERER_MAX_SURFACE_VERTICES = 4096u;
constexpr std::uint32_t WEB_RENDERER_MAX_SURFACE_INDICES = 12288u;
constexpr std::size_t WEB_RENDERER_MAX_RETAINED_SURFACE_BYTES =
    static_cast<std::size_t>(WEB_RENDERER_MAX_SURFACE_VERTICES) * 18u * sizeof(float) +
    static_cast<std::size_t>(WEB_RENDERER_MAX_SURFACE_INDICES) * sizeof(std::uint16_t);

struct WebRendererSurfaceVertex
{
    float position[3];
    // FX code meshes carry the canonical packed alpha as well as RGB. Keep
    // the alpha in the portable vertex so sprite blending does not depend on
    // a texture's alpha channel alone.
    float color[4];
    float textureCoordinate[2];
    float lightmapCoordinate[2];
    // Canonical GfxPackedVertex/GfxWorldVertex unit normal. Model commands
    // consume it for native light-grid lookup; non-model producers leave it
    // zero and never enable the model-lighting shader branch.
    float normal[3];
    // Native XSurface/GfxWorld tangent basis. The sign reconstructs
    // binormal = cross(normal, tangent) * binormalSign for n0 techniques.
    // Producers that cannot carry a tangent leave it zero, which disables
    // normal-map perturbation for that vertex without affecting base passes.
    float tangent[3];
    float binormalSign;
};

static_assert(std::is_standard_layout_v<WebRendererSurfaceVertex>);
static_assert(sizeof(WebRendererSurfaceVertex) == 18u * sizeof(float));

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

const char *WebRenderer_SurfaceResultString(WebRendererSurfaceResult result) noexcept;
