#pragma once

#include <web/web_renderer.h>

#include <cstdint>
#include <vector>

struct GfxWorldVertex;

constexpr std::uint32_t WEB_RENDERER_MAX_MARK_MESH_VERTICES = 65'536u;
constexpr std::uint32_t WEB_RENDERER_MAX_MARK_MESH_INDICES = 131'072u;

enum class WebRendererMarkMeshResult : std::uint8_t
{
    Success = 0,
    InvalidDescriptor,
    OutputTooLarge,
    NonFiniteVertex,
    IndexOutOfRange,
    AllocationFailed,
};

// Converts one canonical R_AddMarkMeshDrawSurf span. EffectsCore writes world
// brush marks as GfxWorldVertex and model/entity marks as GfxPackedVertex in
// the same 44-byte mark-mesh slots; the context selects the corresponding
// native layout at this narrow renderer boundary.
WebRendererMarkMeshResult WebRenderer_AppendMarkMeshBatch(
    const GfxWorldVertex *vertices,
    std::uint32_t vertexCount,
    const std::uint16_t *indices,
    std::uint32_t indexCount,
    bool worldBrushLayout,
    std::vector<WebRendererSurfaceVertex> &verticesOut,
    std::vector<std::uint32_t> &indicesOut) noexcept;

