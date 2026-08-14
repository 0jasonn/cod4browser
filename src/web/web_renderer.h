#pragma once

#include <web/web_renderer_surface.h>

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

// Draws one non-blocking browser frame. Engine work remains outside this seam.
void WebRenderer_DrawFrame(const WebFrameInfo &frame);
