#include <gfx_d3d/gfx_packed_vertex_types.h>
#include <web/web_renderer_code_mesh.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
void TestConversionRetainsPackedColorAndOrdering()
{
    std::array<GfxPackedVertex, 3> source{};
    source[0].xyz[0] = 1.0f;
    source[1].xyz[1] = 2.0f;
    source[2].xyz[2] = 3.0f;
    source[0].color.packed = 0x80402010u;
    source[1].color.packed = 0xffa0b0c0u;
    source[2].color.packed = 0x40102030u;
    // Canonical Vec2PackTexCoords(1.0, 0.5), retained as an explicit FX
    // packed-UV regression rather than relying only on the zero coordinate.
    source[0].texCoord.packed = 0x3c003800u;
    std::array<std::uint32_t, 3> packed{
        0x00010000u, 0x00020001u, 0x00000002u};
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    assert(WebRenderer_AppendCodeMeshBatch(source.data(),
        static_cast<std::uint32_t>(source.size()), packed.data(), 6u,
        vertices, indices) == WebRendererCodeMeshResult::Success);
    assert(vertices.size() == 3u && indices.size() == 6u);
    assert(vertices[0].color[0] == 0x40 / 255.0f);
    assert(vertices[0].color[3] == 0x80 / 255.0f);
    assert(std::fabs(vertices[0].textureCoordinate[0] - 1.0f) < 0.0001f);
    assert(std::fabs(vertices[0].textureCoordinate[1] - 0.5f) < 0.0001f);
    assert(indices[0] == 0u && indices[1] == 1u && indices[2] == 1u &&
        indices[3] == 2u && indices[4] == 2u && indices[5] == 0u);
}

void TestInvalidRangeLeavesExistingOutputUntouched()
{
    GfxPackedVertex source{};
    source.xyz[0] = 4.0f;
    const std::uint32_t invalid = 0x00000001u;
    std::vector<WebRendererSurfaceVertex> vertices(1u);
    vertices[0].position[0] = 99.0f;
    std::vector<std::uint32_t> indices{7u};
    assert(WebRenderer_AppendCodeMeshBatch(&source, 1u, &invalid, 2u,
        vertices, indices) == WebRendererCodeMeshResult::IndexOutOfRange);
    assert(vertices.size() == 1u && vertices[0].position[0] == 99.0f);
    assert(indices.size() == 1u && indices[0] == 7u);
    assert(WebRenderer_AppendCodeMeshBatch(&source, 1u, &invalid, 1u,
        vertices, indices) == WebRendererCodeMeshResult::InvalidDescriptor);

    const std::uint32_t valid = 0u;
    vertices.resize(WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES);
    indices.resize(WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES);
    const std::size_t vertexSize = vertices.size();
    const std::size_t indexSize = indices.size();
    assert(WebRenderer_AppendCodeMeshBatch(&source, 1u, &valid, 2u,
        vertices, indices) == WebRendererCodeMeshResult::OutputTooLarge);
    assert(vertices.size() == vertexSize && indices.size() == indexSize);
}

void TestFallbackStateWritesAndBlends()
{
    constexpr std::uint32_t state0 = WEB_RENDERER_FX_FALLBACK_STATE_BITS0;
    constexpr std::uint32_t state1 = WEB_RENDERER_FX_FALLBACK_STATE_BITS1;
    assert((state0 & 0x18000000u) == 0x18000000u); // RGB + alpha writes
    assert((state0 & 0xffu) == 0x65u);             // src-alpha / inv-src-alpha
    assert(((state0 >> 8u) & 7u) == 1u);           // additive RGB equation
    assert((state1 & 1u) == 0u);                   // no depth write
    assert((state1 & 0x0cu) == 0x0cu);             // LEQUAL depth test
}

void TestFxBatchHasDistinctRendererIdentity()
{
    assert(WebRendererSceneBatchKind::FxCodeMesh !=
        WebRendererSceneBatchKind::DynamicDObj);
}
} // namespace

int main()
{
    TestConversionRetainsPackedColorAndOrdering();
    TestInvalidRangeLeavesExistingOutputUntouched();
    TestFallbackStateWritesAndBlends();
    TestFxBatchHasDistinctRendererIdentity();
    return 0;
}
