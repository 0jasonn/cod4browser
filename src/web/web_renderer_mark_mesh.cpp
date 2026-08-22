#include <web/web_renderer_mark_mesh.h>

#include <gfx_d3d/gfx_world_types.h>
#include <xanim/xsurface_types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

void __cdecl Vec2UnpackTexCoords(PackedTexCoords in, float *out);
void __cdecl Vec3UnpackUnitVec(PackedUnitVec in, float *out);

namespace
{
constexpr float BYTE_TO_UNIT = 1.0f / 255.0f;

void ConvertColor(GfxColor source, float destination[4]) noexcept
{
    destination[0] = static_cast<float>(
        (source.packed >> 16u) & 0xffu) * BYTE_TO_UNIT;
    destination[1] = static_cast<float>(
        (source.packed >> 8u) & 0xffu) * BYTE_TO_UNIT;
    destination[2] = static_cast<float>(
        source.packed & 0xffu) * BYTE_TO_UNIT;
    destination[3] = static_cast<float>(
        (source.packed >> 24u) & 0xffu) * BYTE_TO_UNIT;
}

void ConvertWorldVertex(
    const GfxWorldVertex &source,
    WebRendererSurfaceVertex &destination) noexcept
{
    std::copy_n(source.xyz, 3u, destination.position);
    ConvertColor(source.color, destination.color);
    std::copy_n(source.texCoord, 2u, destination.textureCoordinate);
    std::copy_n(source.lmapCoord, 2u, destination.lightmapCoordinate);
    Vec3UnpackUnitVec(source.normal, destination.normal);
}

void ConvertPackedVertex(
    const GfxWorldVertex &storage,
    WebRendererSurfaceVertex &destination) noexcept
{
    const auto &source = reinterpret_cast<const GfxPackedVertex &>(storage);
    std::copy_n(source.xyz, 3u, destination.position);
    ConvertColor(source.color, destination.color);
    Vec2UnpackTexCoords(source.texCoord, destination.textureCoordinate);
    destination.lightmapCoordinate[0] = 0.0f;
    destination.lightmapCoordinate[1] = 0.0f;
    Vec3UnpackUnitVec(source.normal, destination.normal);
}

bool FiniteVertex(const WebRendererSurfaceVertex &vertex) noexcept
{
    const float *components = &vertex.position[0];
    for (std::size_t index = 0u; index < 14u; ++index)
        if (!std::isfinite(components[index])) return false;
    return true;
}
} // namespace

WebRendererMarkMeshResult WebRenderer_AppendMarkMeshBatch(
    const GfxWorldVertex *vertices,
    std::uint32_t vertexCount,
    const std::uint16_t *indices,
    std::uint32_t indexCount,
    bool worldBrushLayout,
    std::vector<WebRendererSurfaceVertex> &verticesOut,
    std::vector<std::uint32_t> &indicesOut) noexcept
{
    if (!vertices || !indices || vertexCount == 0u || indexCount == 0u ||
        vertexCount > WEB_RENDERER_MAX_MARK_MESH_VERTICES ||
        indexCount > WEB_RENDERER_MAX_MARK_MESH_INDICES ||
        indexCount % 3u != 0u)
    {
        return WebRendererMarkMeshResult::InvalidDescriptor;
    }

    const std::size_t originalVertexCount = verticesOut.size();
    const std::size_t originalIndexCount = indicesOut.size();
    std::vector<WebRendererSurfaceVertex> convertedVertices;
    std::vector<std::uint32_t> convertedIndices;
    std::vector<std::uint32_t> vertexRemap;
    try
    {
        convertedVertices.reserve(std::min(vertexCount, indexCount));
        convertedIndices.reserve(indexCount);
        vertexRemap.assign(vertexCount,
            std::numeric_limits<std::uint32_t>::max());
        for (std::uint32_t sourceOffset = 0u;
             sourceOffset < indexCount; ++sourceOffset)
        {
            const std::uint16_t sourceIndex = indices[sourceOffset];
            if (sourceIndex >= vertexCount)
                return WebRendererMarkMeshResult::IndexOutOfRange;
            std::uint32_t &localIndex = vertexRemap[sourceIndex];
            if (localIndex == std::numeric_limits<std::uint32_t>::max())
            {
                WebRendererSurfaceVertex converted{};
                if (worldBrushLayout)
                    ConvertWorldVertex(vertices[sourceIndex], converted);
                else
                    ConvertPackedVertex(vertices[sourceIndex], converted);
                if (!FiniteVertex(converted))
                    return WebRendererMarkMeshResult::NonFiniteVertex;
                localIndex = static_cast<std::uint32_t>(convertedVertices.size());
                convertedVertices.push_back(converted);
            }
            convertedIndices.push_back(localIndex);
        }
        if (verticesOut.size() > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
                convertedVertices.size() ||
            indicesOut.size() > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
                convertedIndices.size())
        {
            return WebRendererMarkMeshResult::OutputTooLarge;
        }
        const std::uint32_t vertexBase = static_cast<std::uint32_t>(
            originalVertexCount);
        verticesOut.reserve(originalVertexCount + convertedVertices.size());
        indicesOut.reserve(originalIndexCount + convertedIndices.size());
        verticesOut.insert(verticesOut.end(), convertedVertices.begin(),
            convertedVertices.end());
        for (const std::uint32_t index : convertedIndices)
            indicesOut.push_back(vertexBase + index);
    }
    catch (const std::bad_alloc &)
    {
        verticesOut.resize(originalVertexCount);
        indicesOut.resize(originalIndexCount);
        return WebRendererMarkMeshResult::AllocationFailed;
    }
    return WebRendererMarkMeshResult::Success;
}
