#include <web/web_renderer_code_mesh.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace
{
float UnpackTexCoordComponent(std::uint16_t packed) noexcept
{
    if (packed == 0u) return 0.0f;
    const std::uint32_t source = packed;
    const std::uint32_t bits =
        ((source << 16u) & 0x80000000u) |
        (((((source << 14u) & 0x0fffc000u) -
            ((~source << 14u) & 0x10000000u)) ^ 0x80000000u) >> 1u);
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

void ConvertVertex(
    const GfxPackedVertex &source,
    WebRendererSurfaceVertex &destination) noexcept
{
    std::copy_n(source.xyz, 3u, destination.position);
    destination.color[0] = static_cast<float>(
        (source.color.packed >> 16u) & 0xffu) / 255.0f;
    destination.color[1] = static_cast<float>(
        (source.color.packed >> 8u) & 0xffu) / 255.0f;
    destination.color[2] = static_cast<float>(
        source.color.packed & 0xffu) / 255.0f;
    destination.color[3] = static_cast<float>(
        (source.color.packed >> 24u) & 0xffu) / 255.0f;
    destination.textureCoordinate[0] = UnpackTexCoordComponent(
        static_cast<std::uint16_t>(source.texCoord.packed >> 16u));
    destination.textureCoordinate[1] = UnpackTexCoordComponent(
        static_cast<std::uint16_t>(source.texCoord.packed & 0xffffu));
    destination.lightmapCoordinate[0] = 0.0f;
    destination.lightmapCoordinate[1] = 0.0f;
}

bool IsFiniteVertex(const WebRendererSurfaceVertex &vertex) noexcept
{
    const float *components = &vertex.position[0];
    for (std::size_t index = 0u; index < 11u; ++index)
        if (!std::isfinite(components[index])) return false;
    return true;
}
} // namespace

WebRendererCodeMeshResult WebRenderer_AppendCodeMeshBatch(
    const GfxPackedVertex *vertices,
    std::uint32_t vertexCount,
    const std::uint32_t *indices,
    std::uint32_t indexCount,
    std::vector<WebRendererSurfaceVertex> &verticesOut,
    std::vector<std::uint32_t> &indicesOut) noexcept
{
    if (!vertices || !indices || vertexCount == 0u || indexCount == 0u ||
        vertexCount > WEB_RENDERER_MAX_CODE_MESH_VERTICES ||
        indexCount > WEB_RENDERER_MAX_CODE_MESH_INDICES ||
        (indexCount & 1u) != 0u)
        return WebRendererCodeMeshResult::InvalidDescriptor;

    std::vector<std::uint16_t> sourceIndices;
    try
    {
        sourceIndices.reserve(indexCount);
        for (std::uint32_t pair = 0u; pair < indexCount / 2u; ++pair)
        {
            sourceIndices.push_back(static_cast<std::uint16_t>(
                indices[pair] & 0xffffu));
            sourceIndices.push_back(static_cast<std::uint16_t>(
                indices[pair] >> 16u));
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererCodeMeshResult::AllocationFailed;
    }

    std::vector<WebRendererSurfaceVertex> convertedVertices;
    std::vector<std::uint32_t> convertedIndices;
    try
    {
        convertedIndices.reserve(sourceIndices.size());
        for (const std::uint16_t sourceIndex : sourceIndices)
            if (sourceIndex >= vertexCount)
                return WebRendererCodeMeshResult::IndexOutOfRange;
        for (std::uint32_t sourceOffset = 0u;
             sourceOffset < sourceIndices.size(); ++sourceOffset)
        {
            const std::uint16_t sourceIndex = sourceIndices[sourceOffset];
            std::uint32_t localIndex = std::numeric_limits<std::uint32_t>::max();
            for (std::uint32_t prior = 0u; prior < sourceOffset; ++prior)
            {
                if (sourceIndices[prior] == sourceIndex)
                {
                    localIndex = convertedIndices[prior];
                    break;
                }
            }
            if (localIndex == std::numeric_limits<std::uint32_t>::max())
            {
                WebRendererSurfaceVertex converted{};
                ConvertVertex(vertices[sourceIndex], converted);
                if (!IsFiniteVertex(converted))
                    return WebRendererCodeMeshResult::NonFiniteVertex;
                localIndex = static_cast<std::uint32_t>(convertedVertices.size());
                convertedVertices.push_back(converted);
            }
            convertedIndices.push_back(localIndex);
        }
        if (verticesOut.size() > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
                convertedVertices.size() ||
            indicesOut.size() > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
                convertedIndices.size())
            return WebRendererCodeMeshResult::OutputTooLarge;
        verticesOut.insert(verticesOut.end(), convertedVertices.begin(),
            convertedVertices.end());
        const std::uint32_t vertexBase = static_cast<std::uint32_t>(
            verticesOut.size() - convertedVertices.size());
        for (const std::uint32_t index : convertedIndices)
            indicesOut.push_back(vertexBase + index);
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererCodeMeshResult::AllocationFailed;
    }
    return WebRendererCodeMeshResult::Success;
}
