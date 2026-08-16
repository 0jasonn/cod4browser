#pragma once

#include <web/web_renderer_surface.h>
#include <web/web_shader_compatibility.h>

#include <cstddef>
#include <cstdint>

struct WebFrameInfo;

// The bootstrap renderer deliberately keeps one bounded CPU-side RGBA8 copy
// so its texture can be recreated after browser context loss. These limits
// bound that recovery allocation independently of the active graphics backend.
constexpr std::uint32_t WEB_RENDERER_MAX_RGBA8_DIMENSION = 2048u;
constexpr std::size_t WEB_RENDERER_MAX_RETAINED_TEXTURE_BYTES =
    4u * 1024u * 1024u;

struct WebRendererRgba8TextureDesc
{
    std::uint32_t width;
    std::uint32_t height;
    const std::uint8_t *pixels;
    std::size_t byteLength;
    // Original COD4 MaterialTextureDef sampler byte. Zero keeps the bootstrap
    // fallback behavior; imported material bindings provide the checked value.
    std::uint8_t samplerState = 0u;
};

struct WebRendererTextureState
{
    std::uint32_t width;
    std::uint32_t height;
    std::size_t retainedByteCount;
    std::uint32_t uploadGeneration;
    std::uint32_t rebuildGeneration;
    std::uint32_t recoveryCount;
    bool sourceTextureActive;
    bool resident;
};

struct WebRendererShaderState
{
    const char *substitutionId;
    std::uint32_t vertexSourceHash;
    std::uint32_t fragmentSourceHash;
    std::uint32_t submissionGeneration;
    std::uint32_t resourceGeneration;
    std::uint32_t recoveryCount;
    std::uint32_t drawCount;
    bool retained;
    bool resident;
    bool firstDrawCompleted;
};

enum class WebRendererShaderResult : std::uint8_t
{
    Success = 0,
    InvalidDescriptor,
    UnsupportedSubstitution,
    AllocationFailed,
    BackendFailure,
};

enum class WebRendererTextureResult : std::uint8_t
{
    Success = 0,
    InvalidDescriptor,
    UnsupportedDimensions,
    OutputTooLarge,
    AllocationFailed,
    BackendFailure,
};

const char *WebRenderer_TextureResultString(WebRendererTextureResult result) noexcept;
const char *WebRenderer_ShaderResultString(WebRendererShaderResult result) noexcept;

// Validates and copies one callback-scoped indexed surface plus its draw. The
// renderer retains only bounded backend-neutral values, never the caller's
// pointers. The previous surface remains active if validation, allocation, or
// immediate backend upload fails.
WebRendererSurfaceResult WebRenderer_SetSurface(
    const WebRendererSurfaceDesc &surface,
    const WebRendererDrawDesc &draw);

// Creates the browser renderer and backend resources from any retained surface
// and texture descriptions.
bool WebRenderer_Initialize();

// Copies a tightly packed RGBA8 image into bounded renderer-owned recovery
// storage and displays it on a submitted surface that requests the engine-image
// binding. Pixel rows are ordered from top to bottom. The previous image remains
// active if validation or an immediate backend upload fails.
WebRendererTextureResult WebRenderer_SetBootstrapTexture(
    const WebRendererRgba8TextureDesc &texture);

// Returns submitted surfaces to their vertex-color fallback and releases
// any imported recovery pixels. This is used when an asset generation is
// cancelled or replaced so stale content cannot remain visible.
bool WebRenderer_ClearBootstrapTexture();

// Reports only backend-neutral ownership/residency information. A retained
// texture can be non-resident while the browser WebGL context is lost.
WebRendererTextureState WebRenderer_GetBootstrapTextureState();
// Retains one registry-owned WebGL2 compatibility program and atomically
// replaces its GPU program when a context is available. Imported files select
// the stable ID only; shader source always comes from compiled-in port code.
WebRendererShaderResult WebRenderer_SetShaderCompatibility(
    const kisak::web::WebGL2ShaderSubstitution &substitution);

// Drops the retail compatibility program and returns drawing to the bootstrap
// pipeline. Context-loss recovery never keeps a stale imported generation.
bool WebRenderer_ClearShaderCompatibility();

WebRendererShaderState WebRenderer_GetShaderCompatibilityState();

// Draws one non-blocking browser frame. Engine work remains outside this seam.
void WebRenderer_DrawFrame(const WebFrameInfo &frame);
