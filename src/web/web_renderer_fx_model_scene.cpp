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
    WebRendererFxModelSceneCommand &destination)
{
    if (submissionCount == 0u) return WebRendererFxModelSceneResult::NoFxModel;
    if (!submissions) return WebRendererFxModelSceneResult::InvalidSubmission;

    WebRendererFxModelSceneCommand replacement;
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
                return WebRendererFxModelSceneResult::InvalidModel;
            }
            if (!PlacementIsValid(submission.placement))
                return WebRendererFxModelSceneResult::InvalidPlacement;

            const XModelLodInfo &lod = model->lodInfo[submission.lod];
            if (lod.surfIndex > model->numsurfs ||
                lod.numsurfs > model->numsurfs - lod.surfIndex)
            {
                return WebRendererFxModelSceneResult::InvalidModel;
            }
            float axis[3][3]{};
            UnitQuatToAxisExact(submission.placement.base.quat, axis);
            bool modelSubmitted = false;
            for (std::uint32_t localSurface = 0u;
                 localSurface < lod.numsurfs; ++localSurface)
            {
                const std::uint32_t surfaceIndex = lod.surfIndex + localSurface;
                const XSurface &surface = model->surfs[surfaceIndex];
                if (surface.deformed)
                    return WebRendererFxModelSceneResult::UnsupportedSurface;
                if (surface.vertCount == 0u || surface.triCount == 0u)
                    continue;
                if (!surface.verts0 || !surface.triIndices)
                    return WebRendererFxModelSceneResult::InvalidModel;
                const std::uint32_t indexCount =
                    static_cast<std::uint32_t>(surface.triCount) * 3u;
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
                        return WebRendererFxModelSceneResult::InvalidModel;
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
                        return WebRendererFxModelSceneResult::InvalidModel;
                    }
                    replacement.vertices.push_back(vertex);
                }

                const std::uint32_t firstIndex = static_cast<std::uint32_t>(
                    replacement.indices.size());
                for (std::uint32_t index = 0u; index < indexCount; ++index)
                {
                    const std::uint32_t localIndex = surface.triIndices[index];
                    if (localIndex >= surface.vertCount)
                        return WebRendererFxModelSceneResult::IndexOutOfRange;
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
