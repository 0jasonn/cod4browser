#pragma once

#include <web/web_renderer.h>

#include <gfx_d3d/gfx_packed_vertex_types.h>

#include <cstdint>
#include <vector>

// These limits mirror the canonical browser code-mesh scratch storage. The
// converted output is still bounded independently by the dynamic-scene
// backend limits before it is uploaded to WebGL2.
constexpr std::uint32_t WEB_RENDERER_MAX_CODE_MESH_VERTICES = 65'536u;
constexpr std::uint32_t WEB_RENDERER_MAX_CODE_MESH_INDICES = 131'072u;

// D3D9 GfxStateBits fallback for ordinary translucent FX: RGB/alpha writes,
// additive src-alpha blending, LEQUAL depth testing, and no depth write.
constexpr std::uint32_t WEB_RENDERER_FX_FALLBACK_STATE_BITS0 = 0x19650165u;
constexpr std::uint32_t WEB_RENDERER_FX_FALLBACK_STATE_BITS1 = 0x0000000cu;

enum class WebRendererCodeMeshResult : std::uint8_t
{
    Success = 0,
    InvalidDescriptor,
    OutputTooLarge,
    NonFiniteVertex,
    IndexOutOfRange,
    AllocationFailed,
};

// Fastfile comma-prefixed material names are native DB reference stubs. The
// browser DB currently retains those serialized bodies for loader validation,
// so the FX frontend uses this narrow lookup key to recover the already-
// published canonical material without changing DB ownership.
const char *WebRenderer_CodeMeshMaterialLookupName(
    const char *serializedName) noexcept;

// Converts one canonical R_AddCodeMeshDrawSurf span into retained, backend-
// neutral vertices and 32-bit indices. The packed index ABI stores two
// uint16 indices per packed uint32; indexCount remains the canonical count of
// individual indices. Existing output is untouched until the complete span
// has passed validation. The frontend casts the canonical r_double_index_t
// storage at this narrow boundary to avoid pulling native FX ABI declarations
// into portable conversion tests.
WebRendererCodeMeshResult WebRenderer_AppendCodeMeshBatch(
    const GfxPackedVertex *vertices,
    std::uint32_t vertexCount,
    const std::uint32_t *indices,
    std::uint32_t indexCount,
    std::vector<WebRendererSurfaceVertex> &verticesOut,
    std::vector<std::uint32_t> &indicesOut) noexcept;
