#pragma once

#include <gfx_d3d/gfx_particle_cloud_types.h>
#include <web/web_renderer.h>

#include <cstdint>
#include <vector>

constexpr std::uint32_t WEB_RENDERER_MAX_PARTICLE_CLOUD_SUBMISSIONS = 256u;
constexpr std::uint32_t WEB_RENDERER_PARTICLE_CLOUD_PARTICLES = 8u * 8u * 16u;
constexpr std::uint32_t WEB_RENDERER_PARTICLE_CLOUD_VERTICES =
    WEB_RENDERER_PARTICLE_CLOUD_PARTICLES * 4u;
constexpr std::uint32_t WEB_RENDERER_PARTICLE_CLOUD_INDICES =
    WEB_RENDERER_PARTICLE_CLOUD_PARTICLES * 6u;

struct WebRendererParticleCloudSubmission
{
    GfxParticleCloud cloud{};
    Material *material = nullptr;
};

struct WebRendererParticleCloudView
{
    float origin[3]{};
    float axis[3][3]{};
};

struct WebRendererParticleCloudSceneCommand
{
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<WebRendererWorldBatchDesc> batches;
    std::uint32_t cloudCount = 0u;
    std::uint32_t surfaceCount = 0u;
};

enum class WebRendererParticleCloudRetainResult : std::uint8_t
{
    Accepted = 0,
    InvalidSubmission,
    LimitReached,
};

enum class WebRendererParticleCloudSceneResult : std::uint8_t
{
    Success = 0,
    NoCloud,
    InvalidSubmission,
    OutputTooLarge,
    AllocationFailed,
};

enum class WebRendererParticleCloudAppendResult : std::uint8_t
{
    Success = 0,
    InvalidCommand,
    OutputTooLarge,
    AllocationFailed,
};

// Native R_CreateParticleCloudBuffer builds one randomized 8x8x16 lattice for
// the renderer lifetime. Call this after CL_Init seeds the CRT stream and on a
// renderer restart; command construction reuses the retained centers.
void WebRenderer_InitializeParticleCloudLayout() noexcept;

WebRendererParticleCloudRetainResult WebRenderer_RetainParticleCloudSubmission(
    WebRendererParticleCloudSubmission *storage,
    std::uint32_t *count,
    Material *material,
    GfxParticleCloud **cloudOut) noexcept;

void WebRenderer_ClearParticleCloudSubmissions(std::uint32_t *count) noexcept;

WebRendererParticleCloudSceneResult WebRenderer_BuildParticleCloudCommand(
    const WebRendererParticleCloudSubmission &submission,
    const WebRendererParticleCloudView &view,
    WebRendererParticleCloudSceneCommand &destination);

WebRendererParticleCloudSceneResult WebRenderer_BuildParticleCloudSceneCommand(
    const WebRendererParticleCloudSubmission *submissions,
    std::uint32_t submissionCount,
    const WebRendererParticleCloudView &view,
    WebRendererParticleCloudSceneCommand &destination,
    std::uint32_t *droppedCount = nullptr);

WebRendererParticleCloudAppendResult WebRenderer_AppendParticleCloudCommand(
    const WebRendererParticleCloudSceneCommand &source,
    std::vector<WebRendererSurfaceVertex> &vertices,
    std::vector<std::uint32_t> &indices,
    std::vector<WebRendererWorldBatchDesc> &batches,
    std::uint32_t &surfaceCount);

WebRendererParticleCloudAppendResult WebRenderer_ValidateParticleCloudAppendCounts(
    std::size_t destinationVertexCount,
    std::size_t destinationIndexCount,
    std::size_t destinationBatchCount,
    std::uint32_t destinationSurfaceCount) noexcept;
