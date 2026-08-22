#include <gfx_d3d/gfx_world_types.h>
#include <web/web_renderer_mark_mesh.h>
#include <xanim/xsurface_types.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

void __cdecl Vec2UnpackTexCoords(PackedTexCoords in, float *out)
{
    out[0] = static_cast<float>(in.packed & 0xffu) / 255.0f;
    out[1] = static_cast<float>((in.packed >> 8u) & 0xffu) / 255.0f;
}

void __cdecl Vec3UnpackUnitVec(PackedUnitVec, float *out)
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 1.0f;
}

namespace
{
void TestWorldBrushLayoutPreservesLightmapCoordinates()
{
    GfxWorldVertex source[3]{};
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        source[index].xyz[0] = static_cast<float>(index);
        source[index].color.packed = 0x80402010u;
        source[index].texCoord[0] = 0.25f * index;
        source[index].texCoord[1] = 0.5f;
        source[index].lmapCoord[0] = 0.1f * index;
        source[index].lmapCoord[1] = 0.75f;
    }
    const std::uint16_t indices[3]{0u, 1u, 2u};
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> outputIndices;
    assert(WebRenderer_AppendMarkMeshBatch(source, 3u, indices, 3u, true,
        vertices, outputIndices) == WebRendererMarkMeshResult::Success);
    assert(vertices.size() == 3u);
    assert(outputIndices == std::vector<std::uint32_t>({0u, 1u, 2u}));
    assert(std::fabs(vertices[2].lightmapCoordinate[0] - 0.2f) < 0.0001f);
    assert(std::fabs(vertices[0].color[0] - 0x40 / 255.0f) < 0.0001f);
    assert(vertices[0].normal[2] == 1.0f);
}

void TestPackedModelLayoutAndAtomicFailure()
{
    GfxWorldVertex storage[3]{};
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        auto &packed = reinterpret_cast<GfxPackedVertex &>(storage[index]);
        packed.xyz[1] = static_cast<float>(index + 1u);
        packed.color.packed = 0xff112233u;
        packed.texCoord.packed = 0x00008040u;
    }
    const std::uint16_t indices[3]{0u, 1u, 2u};
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> outputIndices;
    assert(WebRenderer_AppendMarkMeshBatch(storage, 3u, indices, 3u, false,
        vertices, outputIndices) == WebRendererMarkMeshResult::Success);
    assert(std::fabs(vertices[0].textureCoordinate[0] - 64.0f / 255.0f) <
        0.0001f);
    assert(vertices[0].lightmapCoordinate[0] == 0.0f);

    const std::size_t vertexCount = vertices.size();
    const std::size_t indexCount = outputIndices.size();
    const std::uint16_t invalid[3]{0u, 1u, 3u};
    assert(WebRenderer_AppendMarkMeshBatch(storage, 3u, invalid, 3u, false,
        vertices, outputIndices) == WebRendererMarkMeshResult::IndexOutOfRange);
    assert(vertices.size() == vertexCount);
    assert(outputIndices.size() == indexCount);

    storage[0].xyz[0] = std::numeric_limits<float>::quiet_NaN();
    assert(WebRenderer_AppendMarkMeshBatch(storage, 3u, indices, 3u, true,
        vertices, outputIndices) == WebRendererMarkMeshResult::NonFiniteVertex);
    assert(vertices.size() == vertexCount);
    assert(outputIndices.size() == indexCount);
}
} // namespace

int main()
{
    TestWorldBrushLayoutPreservesLightmapCoordinates();
    TestPackedModelLayoutAndAtomicFailure();
    return 0;
}

