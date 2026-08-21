#include <web/web_renderer_fx_model_scene.h>

#include <gfx_d3d/material_types.h>
#include <xanim/xmodel_types.h>
#include <xanim/xsurface_types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

void __cdecl Vec2UnpackTexCoords(PackedTexCoords in, float *out);
int __cdecl XModelGetLodForDist(const XModel *model, float dist);

namespace
{
constexpr float BYTE_TO_UNIT = 1.0f / 255.0f;
constexpr std::uint32_t TECHNIQUE_UNLIT_INDEX = 4u;
constexpr std::uint32_t TECHNIQUE_EMISSIVE_INDEX = 5u;
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;

bool Finite3(const float value[3]) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}

bool PlacementIsValid(const GfxScaledPlacement &placement) noexcept
{
    if (!Finite3(placement.base.origin) || !std::isfinite(placement.scale) ||
        placement.scale <= 0.0f)
    {
        return false;
    }
    float lengthSquared = 0.0f;
    for (const float component : placement.base.quat)
    {
        if (!std::isfinite(component)) return false;
        lengthSquared += component * component;
    }
    return std::isfinite(lengthSquared) &&
        std::fabs(lengthSquared - 1.0f) <= 0.002f;
}

void UnitQuatToAxisExact(const float quat[4], float axis[3][3]) noexcept
{
    // This is the same row-major formula as Kisak's UnitQuatToAxis. The
    // validity check above makes the canonical unit-quaternion precondition
    // explicit without invoking the assert-heavy common helper in tests.
    const float scaledX = quat[0] + quat[0];
    const float xx = scaledX * quat[0];
    const float xy = scaledX * quat[1];
    const float xz = scaledX * quat[2];
    const float xw = scaledX * quat[3];
    const float scaledY = quat[1] + quat[1];
    const float yy = scaledY * quat[1];
    const float yz = scaledY * quat[2];
    const float yw = scaledY * quat[3];
    const float scaledZ = quat[2] + quat[2];
    const float zz = scaledZ * quat[2];
    const float zw = scaledZ * quat[3];

    axis[0][0] = 1.0f - (yy + zz);
    axis[0][1] = xy + zw;
    axis[0][2] = xz - yw;
    axis[1][0] = xy - zw;
    axis[1][1] = 1.0f - (xx + zz);
    axis[1][2] = yz + xw;
    axis[2][0] = xz + yw;
    axis[2][1] = yz - xw;
    axis[2][2] = 1.0f - (xx + yy);
}

const GfxImage *FindBaseImage(
    const Material *material, std::uint8_t &sampler) noexcept
{
    if (!material || !material->textureTable) return nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.semantic == 2u && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
    }
    return nullptr;
}

bool SelectTechnique(
    const Material *material, std::uint32_t stateBits[2]) noexcept
{
    if (!material || !material->techniqueSet || !material->stateBitsTable)
        return false;
    for (const std::uint32_t type : {
        TECHNIQUE_LIT_INDEX, TECHNIQUE_UNLIT_INDEX, TECHNIQUE_EMISSIVE_INDEX})
    {
        const MaterialTechnique *technique =
            material->techniqueSet->techniques[type];
        const std::uint8_t entry = material->stateBitsEntry[type];
        if (!technique || technique->passCount == 0u || entry == 0xffu ||
            entry >= material->stateBitsCount)
        {
            continue;
        }
        stateBits[0] = material->stateBitsTable[entry].loadBits[0];
        stateBits[1] = material->stateBitsTable[entry].loadBits[1];
        return true;
    }
    return false;
}

WebRendererWorldBatchDesc MakeDraw(
    const XModel &model, Material *material, std::uint32_t surfaceIndex,
    std::uint32_t firstIndex, std::uint32_t indexCount) noexcept
{
    WebRendererWorldBatchDesc draw{};
    draw.firstIndex = firstIndex;
    draw.indexCount = indexCount;
    draw.surfaceCount = 1u;
    draw.firstSurfaceIndex = surfaceIndex;
    draw.lastSurfaceIndex = surfaceIndex;
    draw.materialIdentity = material;
    draw.materialName = material && material->info.name
        ? material->info.name : "<null-material>";
    draw.modelIdentity = &model;
    draw.modelName = model.name ? model.name : "<unnamed-xmodel>";
    draw.firstInstanceIndex = UINT32_MAX;
    draw.lastInstanceIndex = UINT32_MAX;
    draw.lightmapIndex = 31u;
    draw.sourceKind = WebRendererSceneBatchKind::FxXModel;
    draw.baseImage = FindBaseImage(material, draw.samplerState);
    const bool hasTechnique = SelectTechnique(material, draw.stateBits);
    draw.technique = hasTechnique && draw.baseImage
        ? WebRendererWorldTechnique::BaseTexture
        : WebRendererWorldTechnique::BackendFallback;
    return draw;
}
} // namespace

bool WebRenderer_FxModelPlacementIsValid(
    const GfxScaledPlacement &placement) noexcept
{
    return PlacementIsValid(placement);
}

WebRendererFxModelRetainResult WebRenderer_RetainFxModelSubmission(
    WebRendererFxModelSubmission *storage,
    std::uint32_t *count,
    const XModel *model,
    const GfxScaledPlacement *placement,
    std::uint16_t lod) noexcept
{
    if (!storage || !count || !model || !placement ||
        !PlacementIsValid(*placement))
    {
        return WebRendererFxModelRetainResult::InvalidSubmission;
    }
    if (*count >= WEB_RENDERER_MAX_FX_MODEL_SUBMISSIONS)
        return WebRendererFxModelRetainResult::LimitReached;
    WebRendererFxModelSubmission &retained = storage[(*count)++];
    retained.model = model;
    retained.placement = *placement;
    retained.lod = lod;
    return WebRendererFxModelRetainResult::Accepted;
}

void WebRenderer_ClearFxModelSubmissions(std::uint32_t *count) noexcept
{
    if (count) *count = 0u;
}

int WebRenderer_SelectFxModelLod(
    const XModel *model, const GfxScaledPlacement &placement,
    const float viewOrigin[3]) noexcept
{
    if (!model || !viewOrigin || model->numLods <= 0 ||
        model->numLods > MAX_LODS || !Finite3(viewOrigin) ||
        !Finite3(placement.base.origin) || !std::isfinite(placement.scale) ||
        placement.scale <= 0.0f)
    {
        return -1;
    }
    const float dx = viewOrigin[0] - placement.base.origin[0];
    const float dy = viewOrigin[1] - placement.base.origin[1];
    const float dz = viewOrigin[2] - placement.base.origin[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz) /
        placement.scale;
    if (!std::isfinite(distance)) return -1;
    return XModelGetLodForDist(model, distance);
}

WebRendererFxModelSceneResult WebRenderer_BuildFxModelSceneCommand(
    const WebRendererFxModelSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererFxModelSceneCommand &destination,
    std::uint32_t *droppedCount)
{
    if (droppedCount) *droppedCount = 0u;
    if (submissionCount == 0u) return WebRendererFxModelSceneResult::NoFxModel;
    if (!submissions) return WebRendererFxModelSceneResult::InvalidSubmission;

    WebRendererFxModelSceneCommand replacement;
    const auto dropSubmission = [&]() {
        if (droppedCount && *droppedCount != UINT32_MAX) ++*droppedCount;
    };
    try
    {
        for (std::uint32_t submissionIndex = 0u;
             submissionIndex < submissionCount; ++submissionIndex)
        {
            const WebRendererFxModelSubmission &submission =
                submissions[submissionIndex];
            const XModel *model = submission.model;
            if (!model || !model->surfs || !model->materialHandles ||
                model->numLods <= 0 || model->numLods > MAX_LODS ||
                submission.lod >= static_cast<std::uint16_t>(model->numLods))
            {
                dropSubmission();
                continue;
            }
            if (!PlacementIsValid(submission.placement))
            {
                dropSubmission();
                continue;
            }

            const XModelLodInfo &lod = model->lodInfo[submission.lod];
            if (lod.surfIndex > model->numsurfs ||
                lod.numsurfs > model->numsurfs - lod.surfIndex)
            {
                dropSubmission();
                continue;
            }
            const std::size_t submissionVertexStart = replacement.vertices.size();
            const std::size_t submissionIndexStart = replacement.indices.size();
            const std::size_t submissionBatchStart = replacement.batches.size();
            const std::uint32_t submissionSurfaceStart = replacement.surfaceCount;
            const auto rollbackSubmission = [&]() {
                replacement.vertices.resize(submissionVertexStart);
                replacement.indices.resize(submissionIndexStart);
                replacement.batches.resize(submissionBatchStart);
                replacement.surfaceCount = submissionSurfaceStart;
            };
            const auto dropAndRollback = [&]() {
                rollbackSubmission();
                dropSubmission();
            };
            float axis[3][3]{};
            UnitQuatToAxisExact(submission.placement.base.quat, axis);
            bool modelSubmitted = false;
            for (std::uint32_t localSurface = 0u;
                 localSurface < lod.numsurfs; ++localSurface)
            {
                const std::uint32_t surfaceIndex = lod.surfIndex + localSurface;
                const XSurface &surface = model->surfs[surfaceIndex];
                if (surface.deformed)
                {
                    dropAndRollback();
                    modelSubmitted = false;
                    break;
                }
                if (surface.vertCount == 0u || surface.triCount == 0u)
                    continue;
                if (!surface.verts0 || !surface.triIndices)
                {
                    dropAndRollback();
                    modelSubmitted = false;
                    break;
                }
                const std::uint32_t indexCount =
                    static_cast<std::uint32_t>(surface.triCount) * 3u;
                bool indicesValid = true;
                for (std::uint32_t index = 0u; index < indexCount; ++index)
                {
                    if (surface.triIndices[index] >= surface.vertCount)
                    {
                        indicesValid = false;
                        break;
                    }
                }
                if (!indicesValid)
                {
                    dropAndRollback();
                    modelSubmitted = false;
                    break;
                }
                if (replacement.vertices.size() >
                        WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
                        surface.vertCount ||
                    replacement.indices.size() >
                        WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES - indexCount)
                {
                    return WebRendererFxModelSceneResult::OutputTooLarge;
                }

                const std::uint32_t vertexBase = static_cast<std::uint32_t>(
                    replacement.vertices.size());
                for (std::uint32_t vertexIndex = 0u;
                     vertexIndex < surface.vertCount; ++vertexIndex)
                {
                    const GfxPackedVertex &source = surface.verts0[vertexIndex];
                    WebRendererSurfaceVertex vertex{};
                    // R_FilterXModelIntoScene supplies world-space placement;
                    // unlike DObj skin matrices, it must not receive a second
                    // refdef.viewOffset adjustment here.
                    for (std::size_t row = 0u; row < 3u; ++row)
                    {
                        vertex.position[row] = submission.placement.base.origin[row];
                        for (std::size_t column = 0u; column < 3u; ++column)
                        {
                            vertex.position[row] +=
                                source.xyz[column] * axis[column][row] *
                                submission.placement.scale;
                        }
                    }
                    if (!Finite3(vertex.position))
                    {
                        dropAndRollback();
                        modelSubmitted = false;
                        break;
                    }
                    vertex.color[0] = static_cast<float>(
                        (source.color.packed >> 16u) & 0xffu) * BYTE_TO_UNIT;
                    vertex.color[1] = static_cast<float>(
                        (source.color.packed >> 8u) & 0xffu) * BYTE_TO_UNIT;
                    vertex.color[2] = static_cast<float>(
                        source.color.packed & 0xffu) * BYTE_TO_UNIT;
                    vertex.color[3] = static_cast<float>(
                        (source.color.packed >> 24u) & 0xffu) * BYTE_TO_UNIT;
                    Vec2UnpackTexCoords(source.texCoord,
                        vertex.textureCoordinate);
                    if (!std::isfinite(vertex.textureCoordinate[0]) ||
                        !std::isfinite(vertex.textureCoordinate[1]))
                    {
                        dropAndRollback();
                        modelSubmitted = false;
                        break;
                    }
                    replacement.vertices.push_back(vertex);
                }

                if (!modelSubmitted && replacement.vertices.size() !=
                    vertexBase + surface.vertCount)
                    break;

                const std::uint32_t firstIndex = static_cast<std::uint32_t>(
                    replacement.indices.size());
                for (std::uint32_t index = 0u; index < indexCount; ++index)
                {
                    const std::uint32_t localIndex = surface.triIndices[index];
                    replacement.indices.push_back(vertexBase + localIndex);
                }
                replacement.batches.push_back(MakeDraw(
                    *model, model->materialHandles[surfaceIndex], surfaceIndex,
                    firstIndex, indexCount));
                ++replacement.surfaceCount;
                modelSubmitted = true;
            }
            if (modelSubmitted) ++replacement.modelCount;
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererFxModelSceneResult::AllocationFailed;
    }

    if (replacement.batches.empty())
        return WebRendererFxModelSceneResult::NoFxModel;
    destination = std::move(replacement);
    return WebRendererFxModelSceneResult::Success;
}

WebRendererFxModelAppendResult WebRenderer_ValidateFxModelAppendCounts(
    std::size_t sourceVertexCount,
    std::size_t sourceIndexCount,
    std::size_t sourceBatchCount,
    std::uint32_t sourceSurfaceCount,
    std::size_t destinationVertexCount,
    std::size_t destinationIndexCount,
    std::size_t destinationBatchCount,
    std::uint32_t destinationSurfaceCount) noexcept
{
    if (sourceVertexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES ||
        sourceIndexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES ||
        sourceBatchCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES ||
        destinationVertexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
            sourceVertexCount ||
        destinationIndexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
            sourceIndexCount ||
        destinationBatchCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
            sourceBatchCount ||
        destinationSurfaceCount > UINT32_MAX - sourceSurfaceCount)
    {
        return WebRendererFxModelAppendResult::OutputTooLarge;
    }
    return WebRendererFxModelAppendResult::Success;
}

WebRendererFxModelAppendResult WebRenderer_ValidateFxModelAdmissionCounts(
    std::size_t destinationVertexCount,
    std::size_t destinationIndexCount,
    std::size_t destinationBatchCount,
    std::uint32_t destinationSurfaceCount,
    std::size_t fxVertexCount,
    std::size_t fxIndexCount,
    std::size_t fxBatchCount,
    std::uint32_t fxSurfaceCount,
    std::size_t codeMeshVertexCount,
    std::size_t codeMeshIndexCount,
    std::size_t codeMeshBatchCount,
    std::uint32_t codeMeshSurfaceCount) noexcept
{
    if (fxVertexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES ||
        fxIndexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES ||
        fxBatchCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES ||
        codeMeshVertexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES ||
        codeMeshIndexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES ||
        codeMeshBatchCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES ||
        destinationVertexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
            fxVertexCount ||
        destinationVertexCount + fxVertexCount >
            WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES - codeMeshVertexCount ||
        destinationIndexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
            fxIndexCount ||
        destinationIndexCount + fxIndexCount >
            WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES - codeMeshIndexCount ||
        destinationBatchCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
            fxBatchCount ||
        destinationBatchCount + fxBatchCount >
            WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES - codeMeshBatchCount ||
        destinationSurfaceCount > UINT32_MAX - fxSurfaceCount ||
        destinationSurfaceCount + fxSurfaceCount >
            UINT32_MAX - codeMeshSurfaceCount)
    {
        return WebRendererFxModelAppendResult::OutputTooLarge;
    }
    return WebRendererFxModelAppendResult::Success;
}

WebRendererFxModelAppendResult WebRenderer_AppendFxModelSceneCommand(
    const WebRendererFxModelSceneCommand &source,
    std::vector<WebRendererSurfaceVertex> &vertices,
    std::vector<std::uint32_t> &indices,
    std::vector<WebRendererWorldBatchDesc> &batches,
    std::uint32_t &surfaceCount)
{
    const WebRendererFxModelAppendResult countResult =
        WebRenderer_ValidateFxModelAppendCounts(
            source.vertices.size(), source.indices.size(), source.batches.size(),
            source.surfaceCount, vertices.size(), indices.size(), batches.size(),
            surfaceCount);
    if (countResult != WebRendererFxModelAppendResult::Success)
        return countResult;
    for (const std::uint32_t index : source.indices)
        if (index >= source.vertices.size())
            return WebRendererFxModelAppendResult::InvalidCommand;
    for (const WebRendererWorldBatchDesc &batch : source.batches)
    {
        if (batch.firstIndex > source.indices.size() ||
            batch.indexCount > source.indices.size() - batch.firstIndex)
        {
            return WebRendererFxModelAppendResult::InvalidCommand;
        }
    }
    const std::size_t originalVertexCount = vertices.size();
    const std::size_t originalIndexCount = indices.size();
    const std::size_t originalBatchCount = batches.size();
    const std::uint32_t originalSurfaceCount = surfaceCount;
    try
    {
        vertices.reserve(originalVertexCount + source.vertices.size());
        indices.reserve(originalIndexCount + source.indices.size());
        batches.reserve(originalBatchCount + source.batches.size());
        vertices.insert(vertices.end(), source.vertices.begin(),
            source.vertices.end());
        for (const std::uint32_t index : source.indices)
            indices.push_back(static_cast<std::uint32_t>(originalVertexCount) +
                index);
        for (WebRendererWorldBatchDesc batch : source.batches)
        {
            batch.firstIndex += static_cast<std::uint32_t>(originalIndexCount);
            batches.push_back(batch);
        }
        surfaceCount += source.surfaceCount;
    }
    catch (const std::bad_alloc &)
    {
        vertices.resize(originalVertexCount);
        indices.resize(originalIndexCount);
        batches.resize(originalBatchCount);
        surfaceCount = originalSurfaceCount;
        return WebRendererFxModelAppendResult::AllocationFailed;
    }
    return WebRendererFxModelAppendResult::Success;
}

const char *WebRenderer_FxModelSceneResultString(
    WebRendererFxModelSceneResult result) noexcept
{
    switch (result)
    {
    case WebRendererFxModelSceneResult::Success: return "success";
    case WebRendererFxModelSceneResult::NoFxModel: return "no FX model";
    case WebRendererFxModelSceneResult::InvalidSubmission:
        return "invalid FX model submission";
    case WebRendererFxModelSceneResult::InvalidPlacement:
        return "invalid FX model placement";
    case WebRendererFxModelSceneResult::InvalidModel:
        return "invalid FX model or LOD surface data";
    case WebRendererFxModelSceneResult::UnsupportedSurface:
        return "deformed FX model surfaces are unsupported";
    case WebRendererFxModelSceneResult::IndexOutOfRange:
        return "FX model index is outside its vertex range";
    case WebRendererFxModelSceneResult::OutputTooLarge:
        return "FX model command exceeds dynamic backend limits";
    case WebRendererFxModelSceneResult::AllocationFailed:
        return "FX model command allocation failed";
    }
    return "unknown FX model scene error";
}
