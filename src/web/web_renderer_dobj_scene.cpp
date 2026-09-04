#include <web/web_renderer_dobj_scene.h>
#include <web/web_frame_profile.h>
#include <web/web_renderer_material_lookup.h>

#include <cgame/cg_pose.h>
#include <cgame/cg_local.h>
#include <gfx_d3d/gfx_world_types.h>
#include <gfx_d3d/r_dpvs_core.h>
#include <gfx_d3d/r_light.h>
#include <gfx_d3d/r_model_pose_bounds.h>
#include <gfx_d3d/material_types.h>
#include <universal/com_math.h>
#include <xanim/dobj.h>
#include <xanim/dobj_utils.h>
#include <xanim/xmodel.h>
#include <xanim/xmodel_types.h>
#include <xanim/xsurface_types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

void __cdecl Vec2UnpackTexCoords(PackedTexCoords in, float *out);
void __cdecl Vec3UnpackUnitVec(PackedUnitVec in, float *out);

namespace
{
constexpr float BYTE_TO_UNIT = 1.0f / 255.0f;
constexpr float SHORT_WEIGHT_TO_UNIT = 1.0f / 65536.0f;
constexpr std::uint32_t TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX = 2u;
constexpr std::uint32_t TECHNIQUE_UNLIT_INDEX = 4u;
constexpr std::uint32_t TECHNIQUE_EMISSIVE_INDEX = 5u;
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;
constexpr std::uint32_t ENV_MAP_PARMS_HASH = 0x3d9994dcu;
constexpr std::uint32_t DETAIL_SCALE_HASH = 0x08d36a09u;

bool Finite3(const float value[3]) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}

bool SelectTechnique(
    const Material *material, WebRendererWorldBatchDesc &draw) noexcept
{
    if (!material || !material->techniqueSet || !material->stateBitsTable)
        return false;
    const MaterialTechniqueSet *techniqueSet =
        material->techniqueSet->remappedTechniqueSet
            ? material->techniqueSet->remappedTechniqueSet
            : material->techniqueSet;
    for (const std::uint32_t type : {
        TECHNIQUE_LIT_INDEX, TECHNIQUE_UNLIT_INDEX,
        TECHNIQUE_EMISSIVE_INDEX})
    {
        const MaterialTechnique *technique =
            techniqueSet->techniques[type];
        const std::uint8_t entry = material->stateBitsEntry[type];
        if (!technique || technique->passCount == 0u || entry == 0xffu ||
            entry >= material->stateBitsCount)
        {
            continue;
        }
        draw.stateBits[0] = material->stateBitsTable[entry].loadBits[0];
        draw.stateBits[1] = material->stateBitsTable[entry].loadBits[1];
        draw.techniqueName = technique->name;
        draw.techniqueType = static_cast<std::uint8_t>(type);
        draw.techniqueFlags = technique->flags;
        for (std::uint32_t pass = 0u; pass < technique->passCount; ++pass)
            draw.customSamplerFlags |=
                technique->passArray[pass].customSamplerFlags;
        const MaterialPixelShader *pixelShader =
            technique->passArray[0u].pixelShader;
        draw.pixelShaderName = pixelShader ? pixelShader->name : nullptr;
        return true;
    }
    if (material->techniqueSet->name &&
        material->techniqueSet->name[0] == ',')
    {
        for (const std::uint32_t type : {
            TECHNIQUE_LIT_INDEX, TECHNIQUE_UNLIT_INDEX,
            TECHNIQUE_EMISSIVE_INDEX})
        {
            const std::uint8_t entry = material->stateBitsEntry[type];
            if (entry == 0xffu || entry >= material->stateBitsCount)
                continue;
            draw.stateBits[0] = material->stateBitsTable[entry].loadBits[0];
            draw.stateBits[1] = material->stateBitsTable[entry].loadBits[1];
            draw.techniqueName = material->techniqueSet->name;
            draw.techniqueType = static_cast<std::uint8_t>(type);
            return true;
        }
    }
    return false;
}

void MultiplySkelMat(
    const DObjSkelMat &left, const DObjSkelMat &right,
    DObjSkelMat &output) noexcept
{
    for (std::size_t row = 0u; row < 3u; ++row)
    {
        for (std::size_t column = 0u; column < 3u; ++column)
        {
            output.axis[row][column] =
                left.axis[row][0] * right.axis[0][column] +
                left.axis[row][1] * right.axis[1][column] +
                left.axis[row][2] * right.axis[2][column];
        }
        output.axis[row][3] = 0.0f;
    }
    for (std::size_t column = 0u; column < 3u; ++column)
    {
        output.origin[column] =
            left.origin[0] * right.axis[0][column] +
            left.origin[1] * right.axis[1][column] +
            left.origin[2] * right.axis[2][column] +
            right.origin[column];
    }
    output.origin[3] = 1.0f;
}

void TransformPosition(
    const float input[3], const DObjSkelMat &matrix,
    float output[3]) noexcept
{
    for (std::size_t column = 0u; column < 3u; ++column)
    {
        output[column] =
            input[0] * matrix.axis[0][column] +
            input[1] * matrix.axis[1][column] +
            input[2] * matrix.axis[2][column] +
            matrix.origin[column];
    }
}

void TransformDirection(
    const float input[3], const DObjSkelMat &matrix,
    float output[3]) noexcept
{
    for (std::size_t column = 0u; column < 3u; ++column)
    {
        output[column] =
            input[0] * matrix.axis[0][column] +
            input[1] * matrix.axis[1][column] +
            input[2] * matrix.axis[2][column];
    }
}

bool NormalizeDirection(float direction[3]) noexcept
{
    const float lengthSquared = direction[0] * direction[0] +
        direction[1] * direction[1] + direction[2] * direction[2];
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f)
        return false;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    for (std::size_t axis = 0u; axis < 3u; ++axis)
        direction[axis] *= inverseLength;
    return Finite3(direction);
}

bool FinishVertex(const GfxPackedVertex &source,
    WebRendererSurfaceVertex &vertex) noexcept
{
    if (!Finite3(vertex.position) ||
        !NormalizeDirection(vertex.normal) ||
        !NormalizeDirection(vertex.tangent) ||
        !std::isfinite(source.binormalSign))
        return false;
    vertex.binormalSign = source.binormalSign;
    vertex.color[0] = static_cast<float>((source.color.packed >> 16u) & 0xffu) * BYTE_TO_UNIT;
    vertex.color[1] = static_cast<float>((source.color.packed >> 8u) & 0xffu) * BYTE_TO_UNIT;
    vertex.color[2] = static_cast<float>(source.color.packed & 0xffu) * BYTE_TO_UNIT;
    vertex.color[3] = static_cast<float>((source.color.packed >> 24u) & 0xffu) * BYTE_TO_UNIT;
    Vec2UnpackTexCoords(source.texCoord, vertex.textureCoordinate);
    return std::isfinite(vertex.textureCoordinate[0]) &&
        std::isfinite(vertex.textureCoordinate[1]);
}

const DObjSkelMat *MatrixFromByteOffset(
    const std::vector<DObjSkelMat> &matrices,
    std::uint16_t byteOffset) noexcept
{
    if ((byteOffset % sizeof(DObjSkelMat)) != 0u) return nullptr;
    const std::size_t index = byteOffset / sizeof(DObjSkelMat);
    return index < matrices.size() ? &matrices[index] : nullptr;
}

bool SkinWeightedSurface(
    const XSurface &surface,
    const std::vector<DObjSkelMat> &matrices,
    WebRendererSurfaceVertex *vertices)
{
    if (!surface.vertInfo.vertsBlend) return false;
    const std::uint16_t *blend = surface.vertInfo.vertsBlend;
    std::uint32_t vertexIndex = 0u;
    for (std::uint32_t influenceCount = 0u;
         influenceCount < 4u; ++influenceCount)
    {
        const int signedCount = surface.vertInfo.vertCount[influenceCount];
        if (signedCount < 0) return false;
        const std::uint32_t count = static_cast<std::uint32_t>(signedCount);
        for (std::uint32_t local = 0u; local < count; ++local, ++vertexIndex)
        {
            if (vertexIndex >= surface.vertCount) return false;
            const GfxPackedVertex &vertex = surface.verts0[vertexIndex];
            float unpackedNormal[3]{};
            float unpackedTangent[3]{};
            Vec3UnpackUnitVec(vertex.normal, unpackedNormal);
            Vec3UnpackUnitVec(vertex.tangent, unpackedTangent);
            if (!Finite3(unpackedNormal) || !Finite3(unpackedTangent))
                return false;
            float transformed[3]{};
            float transformedNormal[3]{};
            float transformedTangent[3]{};
            const DObjSkelMat *primary =
                MatrixFromByteOffset(matrices, blend[0]);
            if (!primary) return false;
            TransformPosition(vertex.xyz, *primary, transformed);
            TransformDirection(unpackedNormal, *primary, transformedNormal);
            TransformDirection(unpackedTangent, *primary, transformedTangent);
            float weightedSecondary[3]{};
            float explicitWeight = 0.0f;
            for (std::uint32_t influence = 0u;
                 influence < influenceCount; ++influence)
            {
                const DObjSkelMat *secondary = MatrixFromByteOffset(
                    matrices, blend[1u + influence * 2u]);
                if (!secondary) return false;
                const float weight = static_cast<float>(
                    blend[2u + influence * 2u]) * SHORT_WEIGHT_TO_UNIT;
                float secondaryPosition[3]{};
                TransformPosition(vertex.xyz, *secondary, secondaryPosition);
                for (std::size_t axis = 0u; axis < 3u; ++axis)
                {
                    weightedSecondary[axis] +=
                        weight * secondaryPosition[axis];
                }
                explicitWeight += weight;
            }
            const float primaryWeight = 1.0f - explicitWeight;
            WebRendererSurfaceVertex &output = vertices[vertexIndex];
            for (std::size_t axis = 0u; axis < 3u; ++axis)
            {
                output.position[axis] =
                    primaryWeight * transformed[axis] +
                    weightedSecondary[axis];
                // Canonical Kisak skinning blends positions but transforms the
                // vertex basis with the primary bone only.
                output.normal[axis] = transformedNormal[axis];
                output.tangent[axis] = transformedTangent[axis];
            }
            if (!FinishVertex(vertex, output)) return false;
            blend += 1u + influenceCount * 2u;
        }
    }
    return vertexIndex == surface.vertCount;
}

struct RigidDecodedSurface
{
    const GfxPackedVertex *source = nullptr;
    std::vector<WebRendererSurfaceVertex> vertices;
};

bool DecodeRigidVertexStatic(
    const GfxPackedVertex &source,
    WebRendererSurfaceVertex &vertex) noexcept
{
    std::copy_n(source.xyz, 3u, vertex.position);
    Vec3UnpackUnitVec(source.normal, vertex.normal);
    Vec3UnpackUnitVec(source.tangent, vertex.tangent);
    if (!Finite3(vertex.position) || !Finite3(vertex.normal) ||
        !Finite3(vertex.tangent) || !std::isfinite(source.binormalSign))
    {
        return false;
    }
    vertex.binormalSign = source.binormalSign;
    vertex.color[0] = static_cast<float>(
        (source.color.packed >> 16u) & 0xffu) * BYTE_TO_UNIT;
    vertex.color[1] = static_cast<float>(
        (source.color.packed >> 8u) & 0xffu) * BYTE_TO_UNIT;
    vertex.color[2] = static_cast<float>(
        source.color.packed & 0xffu) * BYTE_TO_UNIT;
    vertex.color[3] = static_cast<float>(
        (source.color.packed >> 24u) & 0xffu) * BYTE_TO_UNIT;
    Vec2UnpackTexCoords(source.texCoord, vertex.textureCoordinate);
    return std::isfinite(vertex.textureCoordinate[0]) &&
        std::isfinite(vertex.textureCoordinate[1]);
}

const WebRendererSurfaceVertex *ResolveRigidDecodedVertices(
    const XSurface &surface,
    std::vector<RigidDecodedSurface> &cache,
    std::uint32_t &cachedVertexCount)
{
    const auto found = std::find_if(cache.begin(), cache.end(),
        [&surface](const RigidDecodedSurface &entry) {
            return entry.source == surface.verts0 &&
                entry.vertices.size() == surface.vertCount;
        });
    if (found != cache.end()) return found->vertices.data();
    if (surface.vertCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
            cachedVertexCount)
    {
        return nullptr;
    }
    RigidDecodedSurface decoded;
    decoded.source = surface.verts0;
    decoded.vertices.resize(surface.vertCount);
    for (std::uint32_t index = 0u; index < surface.vertCount; ++index)
        if (!DecodeRigidVertexStatic(
                surface.verts0[index], decoded.vertices[index]))
            return nullptr;
    cache.push_back(std::move(decoded));
    cachedVertexCount += surface.vertCount;
    return cache.back().vertices.data();
}

bool SkinRigidSurface(
    const XSurface &surface,
    const std::vector<DObjSkelMat> &matrices,
    std::vector<RigidDecodedSurface> &cache,
    std::uint32_t &cachedVertexCount,
    WebRendererSurfaceVertex *vertices)
{
    if (!surface.vertList || surface.vertListCount == 0u) return false;
    const WebRendererSurfaceVertex *decoded = ResolveRigidDecodedVertices(
        surface, cache, cachedVertexCount);
    std::uint32_t vertexIndex = 0u;
    for (std::uint32_t listIndex = 0u;
         listIndex < surface.vertListCount; ++listIndex)
    {
        const XRigidVertList &list = surface.vertList[listIndex];
        const DObjSkelMat *matrix =
            MatrixFromByteOffset(matrices, list.boneOffset);
        if (!matrix || list.vertCount > surface.vertCount - vertexIndex)
            return false;
        for (std::uint32_t local = 0u; local < list.vertCount;
             ++local, ++vertexIndex)
        {
            WebRendererSurfaceVertex uncached;
            const WebRendererSurfaceVertex *source = decoded
                ? &decoded[vertexIndex] : &uncached;
            if (!decoded && !DecodeRigidVertexStatic(
                    surface.verts0[vertexIndex], uncached))
                return false;
            WebRendererSurfaceVertex &output = vertices[vertexIndex];
            output = *source;
            TransformPosition(source->position, *matrix, output.position);
            TransformDirection(source->normal, *matrix, output.normal);
            TransformDirection(source->tangent, *matrix, output.tangent);
            if (!Finite3(output.position) ||
                !NormalizeDirection(output.normal) ||
                !NormalizeDirection(output.tangent))
                return false;
        }
    }
    return vertexIndex == surface.vertCount;
}

struct DObjSkinningScratch
{
    struct ModelLightingCacheEntry
    {
        float origin[3]{};
        std::uint32_t nonSunPrimaryLightIndex = 0u;
        WebRendererModelLightingSample sample{};
        bool valid = false;
    };

    std::vector<DObjSkelMat> matrices;
    std::vector<RigidDecodedSurface> rigidDecodedSurfaces;
    std::uint32_t rigidDecodedVertexCount = 0u;
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<ModelLightingCacheEntry> modelLighting;
};

DObjSkinningScratch &SkinningScratch() noexcept
{
    // R_RenderScene rebuilds dynamic DObj geometry synchronously. Retain
    // bounded decoded immutable vertex records, numeric workspace, and
    // lighting-handle payloads; model, pose, and material pointers remain
    // callback-scoped. R_UnloadWorld releases the source-vertex identities.
    static thread_local DObjSkinningScratch scratch;
    return scratch;
}

bool ResolveModelLighting(
    DObjSkinningScratch &scratch,
    const WebRendererDObjSubmission &submission,
    const GfxLightGrid &lightGrid,
    std::uint32_t nonSunPrimaryLightIndex,
    const WebRendererModelLightingCallbacks *callbacks,
    WebRendererModelLightingSample &sample)
{
    DObjSkinningScratch::ModelLightingCacheEntry *entry = nullptr;
    std::uint16_t *const handle = submission.cachedLightingHandle;
    if (handle && *handle != 0u && *handle <= scratch.modelLighting.size())
    {
        entry = &scratch.modelLighting[*handle - 1u];
        if (entry->valid &&
            entry->nonSunPrimaryLightIndex == nonSunPrimaryLightIndex &&
            entry->origin[0] == submission.lightingOrigin[0] &&
            entry->origin[1] == submission.lightingOrigin[1] &&
            entry->origin[2] == submission.lightingOrigin[2])
        {
            sample = entry->sample;
            return true;
        }
    }

    WebRendererModelLightingSample replacement{};
    if (!WebRenderer_EvaluateModelLighting(
            lightGrid, submission.lightingOrigin,
            nonSunPrimaryLightIndex, callbacks, replacement))
    {
        return false;
    }

    if (handle)
    {
        if (!entry)
        {
            if (scratch.modelLighting.size() < UINT16_MAX)
            {
                scratch.modelLighting.emplace_back();
                *handle = static_cast<std::uint16_t>(
                    scratch.modelLighting.size());
                entry = &scratch.modelLighting.back();
            }
            else
            {
                *handle = 0u;
            }
        }
        if (entry)
        {
            std::copy_n(submission.lightingOrigin, 3u, entry->origin);
            entry->nonSunPrimaryLightIndex = nonSunPrimaryLightIndex;
            entry->sample = replacement;
            entry->valid = true;
        }
    }
    sample = replacement;
    return true;
}

bool SurfaceHidden(
    const XSurface &surface, const std::uint32_t hideBits[4],
    std::uint32_t modelBoneOffset, std::uint32_t modelBoneCount) noexcept
{
    for (std::uint32_t localBone = 0u; localBone < modelBoneCount;
         ++localBone)
    {
        const std::uint32_t localMask =
            0x80000000u >> (localBone & 31u);
        if ((static_cast<std::uint32_t>(
                surface.partBits[localBone >> 5u]) & localMask) == 0u)
        {
            continue;
        }
        const std::uint32_t globalBone = modelBoneOffset + localBone;
        if (globalBone < 128u &&
            (hideBits[globalBone >> 5u] &
             (0x80000000u >> (globalBone & 31u))) != 0u)
        {
            return true;
        }
    }
    return false;
}

WebRendererWorldBatchDesc MakeDraw(
    const XModel &model, Material *material,
    std::uint32_t modelSurfaceIndex, std::uint32_t firstIndex,
    std::uint32_t indexCount,
    const float modelLightingCoordinates[3],
    bool modelLightingEnabled, bool depthHack, bool castsSunShadow,
    bool excludeTransientSpotLight,
    std::uint32_t entityNumber,
    std::uint8_t primaryLightIndex,
    std::uint8_t reflectionProbeIndex,
    const GfxImage *reflectionProbeImage,
    WebRendererMaterialResolver materialResolver) noexcept
{
    material = WebRenderer_ResolveDObjMaterial(material, materialResolver);
    WebRendererWorldBatchDesc draw{};
    draw.firstIndex = firstIndex;
    draw.indexCount = indexCount;
    draw.surfaceCount = 1u;
    draw.firstSurfaceIndex = modelSurfaceIndex;
    draw.lastSurfaceIndex = modelSurfaceIndex;
    draw.materialIdentity = material;
    draw.materialName = material && material->info.name
        ? material->info.name : "<null-material>";
    draw.modelIdentity = &model;
    draw.modelName = model.name ? model.name : "<unnamed-xmodel>";
    draw.firstInstanceIndex = UINT32_MAX;
    draw.lastInstanceIndex = UINT32_MAX;
    draw.lightmapIndex = 31u;
    draw.primaryLightIndex = primaryLightIndex;
    draw.sourceKind = WebRendererSceneBatchKind::DynamicDObj;
    draw.shadowEntityKind = WebRendererShadowEntityKind::SceneEntity;
    draw.shadowEntityId = entityNumber;
    draw.cameraRegion = material ? material->cameraRegion : 0u;
    draw.depthHack = depthHack;
    draw.excludeTransientSpotLight = excludeTransientSpotLight;
    const MaterialTechniqueSet *shadowSet =
        material ? material->techniqueSet : nullptr;
    if (shadowSet && shadowSet->remappedTechniqueSet)
        shadowSet = shadowSet->remappedTechniqueSet;
    // Native R_AddXModelSurfaces checks the requested build-shadowmap
    // technique for scene and dynamic models; the material game flag is the
    // specialized static-model shortcut and must not reject these draws.
    draw.castsSunShadow = castsSunShadow && shadowSet &&
        shadowSet->techniques[TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX];
    draw.castsSpotShadow = draw.castsSunShadow;
    if (draw.castsSunShadow && material && material->stateBitsTable)
    {
        const std::uint8_t shadowStateEntry = material->stateBitsEntry[
            TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX];
        if (shadowStateEntry != 0xffu &&
            shadowStateEntry < material->stateBitsCount)
        {
            draw.shadowStateBits0 =
                material->stateBitsTable[shadowStateEntry].loadBits[0];
        }
    }
    draw.baseImage = WebRenderer_FindBaseImage(material, draw.samplerState);
    draw.normalImage = WebRenderer_FindNormalImage(material, draw.normalSamplerState);
    draw.specularImage = WebRenderer_FindSpecularImage(
        material, draw.specularSamplerState);
    draw.reflectionProbeIndex = reflectionProbeIndex;
    draw.reflectionProbeImage = reflectionProbeImage;
    // The WebGL compatibility technique is deliberately a base-color subset
    // of the canonical material. Preserve canonical state when one of the
    // common passes supplies it, but a DB-owned color image remains enough to
    // use that supported subset even when the original shader itself is not.
    SelectTechnique(material, draw);
    draw.ambientProbeLighting = draw.pixelShaderName &&
        std::strncmp(draw.pixelShaderName, "lp_amb_", 7u) == 0;
    if (draw.pixelShaderName &&
        std::strstr(draw.pixelShaderName, "d0") != nullptr &&
        WebRenderer_CopyMaterialConstant(material, DETAIL_SCALE_HASH, draw.detailScale))
    {
        draw.detailImage = WebRenderer_FindDetailImage(
            material, draw.detailSamplerState);
    }
    const bool normalMapped = draw.pixelShaderName &&
        std::strstr(draw.pixelShaderName, "n0") != nullptr;
    if (!normalMapped)
        draw.normalImage = nullptr;
    const bool environmentSpecular = draw.pixelShaderName &&
        std::strncmp(draw.pixelShaderName, "lp_", 3u) == 0 &&
        std::strstr(draw.pixelShaderName, "s0_sm3.hlsl") != nullptr &&
        draw.specularImage && draw.reflectionProbeImage &&
        WebRenderer_CopyMaterialConstant(material, ENV_MAP_PARMS_HASH, draw.envMapParms);
    draw.technique = WebRenderer_IsCinematicMaterial(material, draw.techniqueType)
        ? WebRendererWorldTechnique::Cinematic
        : !draw.baseImage
        ? WebRendererWorldTechnique::BackendFallback
        : WebRenderer_IsReflexSightTechnique(draw.techniqueName)
            ? WebRendererWorldTechnique::ReflexSight
            : environmentSpecular
                ? (normalMapped
                    ? WebRendererWorldTechnique::BaseTextureNormalSpecular
                    : WebRendererWorldTechnique::BaseTextureSpecular)
                : WebRendererWorldTechnique::BaseTexture;
    // Only the canonical lit pass consumes the model-light-grid constants.
    if (draw.technique == WebRendererWorldTechnique::Cinematic)
        draw.samplerState = draw.normalSamplerState = draw.detailSamplerState = draw.specularSamplerState = 0x62;
    // Unlit reflex sights must preserve their emissive color and derived
    // opacity instead of being darkened by the viewmodel's lighting sample.
    if (modelLightingEnabled &&
        draw.techniqueType == TECHNIQUE_LIT_INDEX)
    {
        draw.lightingMode = WebRendererWorldLightingMode::ModelLightGrid;
        std::copy_n(modelLightingCoordinates, 3u,
            draw.modelLightingCoordinates);
    }
    return draw;
}
} // namespace

void WebRenderer_RecycleDObjSceneGeometry(WebRendererDObjSceneCommand &command) noexcept
{
    DObjSkinningScratch &scratch = SkinningScratch();
    // Retain only the larger allocation; never hold canonical asset pointers.
    if (command.vertices.capacity() > scratch.vertices.capacity())
        scratch.vertices.swap(command.vertices);
    if (command.indices.capacity() > scratch.indices.capacity())
        scratch.indices.swap(command.indices);
    scratch.vertices.clear();
    scratch.indices.clear();
    command.vertices.clear();
    command.indices.clear();
}

void WebRenderer_ReleaseDObjSceneScratch() noexcept
{
    SkinningScratch() = {};
}

bool WebRenderer_ComputeDObjReceiverBounds(const DObj_s &obj,
    const DObjAnimMat *posedMats, const int partBits[4],
    const float viewOffset[3], float mins[3], float maxs[3]) noexcept
{
    if (!posedMats || !partBits || !viewOffset || !mins || !maxs ||
        !obj.models || obj.numBones == 0u || obj.numBones > DOBJ_MAX_PARTS ||
        !Finite3(viewOffset)) return false;
    float localMins[3]{131072.0f, 131072.0f, 131072.0f};
    float localMaxs[3]{-131072.0f, -131072.0f, -131072.0f};
    unsigned globalBone = 0u;
    bool accumulated = false;
    for (unsigned modelIndex = 0u; modelIndex < obj.numModels; ++modelIndex)
    {
        const XModel *model = obj.models[modelIndex];
        if (!model || !model->boneInfo || globalBone + model->numBones > obj.numBones)
            return false;
        for (unsigned localBone = 0u; localBone < model->numBones;
             ++localBone, ++globalBone)
        {
            if ((static_cast<unsigned>(partBits[globalBone >> 5u]) &
                    (0x80000000u >> (globalBone & 31u))) == 0u) continue;
            const DObjAnimMat &pose = posedMats[globalBone];
            const XBoneInfo &bone = model->boneInfo[localBone];
            for (float value : pose.quat) if (!std::isfinite(value)) return false;
            for (float value : pose.trans) if (!std::isfinite(value)) return false;
            if (!std::isfinite(pose.transWeight)) return false;
            for (unsigned axis = 0u; axis < 3u; ++axis)
                if (!std::isfinite(bone.bounds[0][axis]) ||
                    !std::isfinite(bone.bounds[1][axis]) ||
                    bone.bounds[0][axis] > bone.bounds[1][axis]) return false;
            kisak::model_pose::AccumulateBoneBounds(
                pose, bone, viewOffset, localMins, localMaxs);
            accumulated = true;
        }
    }
    if (!accumulated || globalBone != obj.numBones ||
        !Finite3(localMins) || !Finite3(localMaxs)) return false;
    std::copy_n(localMins, 3, mins);
    std::copy_n(localMaxs, 3, maxs);
    return true;
}

namespace
{
struct PreparedDObjPose
{
    std::array<int, DOBJ_MAX_SUBMODELS> selectedLods{};
    int partBits[4]{};
    DObjAnimMat *matrices = nullptr;
};

WebRendererDObjSceneResult PrepareDObjPose(
    const WebRendererDObjSubmission &submission, const WebRendererLodParms *lodParms,
    bool sceneEntity, PreparedDObjPose &prepared)
{
    const DObj_s *obj = submission.obj;
    if (!obj || !submission.pose || obj->numModels == 0u ||
        obj->numModels > DOBJ_MAX_SUBMODELS || !obj->models)
        return WebRendererDObjSceneResult::InvalidSubmission;
    auto &selectedLods = prepared.selectedLods;
    auto &posePartBits = prepared.partBits;
    selectedLods.fill(-1);
    std::uint32_t selectedBoneOffset = 0u;
    bool hasSelectedSurface = false;
    for (std::uint32_t modelIndex = 0u; modelIndex < obj->numModels; ++modelIndex)
    {
        const XModel *model = DObjGetModel(obj, modelIndex);
        if (!model || !model->surfs || !model->materialHandles ||
            !model->baseMat || (sceneEntity && !model->boneInfo) || model->numLods <= 0 ||
            model->numBones == 0u ||
            selectedBoneOffset + model->numBones > obj->numBones)
            return WebRendererDObjSceneResult::InvalidModel;
        const int selected = WebRenderer_SelectDObjLod(
            model, submission.pose->origin, lodParms);
        if (!sceneEntity && (!std::isfinite(model->radius) || model->radius < 0.0f))
            return WebRendererDObjSceneResult::InvalidModel;
        if (selected >= model->numLods || selected >= MAX_LODS)
            return WebRendererDObjSceneResult::InvalidModel;
        selectedLods[modelIndex] = selected;
        if (selected >= 0)
        {
            const XModelLodInfo &lod = model->lodInfo[selected];
            if (lod.surfIndex > model->numsurfs ||
                lod.numsurfs > model->numsurfs - lod.surfIndex)
                return WebRendererDObjSceneResult::InvalidModel;
            hasSelectedSurface |= lod.numsurfs != 0u;
            for (std::uint32_t localBone = 0u; localBone < model->numBones; ++localBone)
            {
                const std::uint32_t localMask = 0x80000000u >> (localBone & 31u);
                if ((static_cast<std::uint32_t>(lod.partBits[localBone >> 5u]) & localMask) == 0u)
                    continue;
                const std::uint32_t globalBone = selectedBoneOffset + localBone;
                posePartBits[globalBone >> 5u] |= static_cast<int>(
                    0x80000000u >> (globalBone & 31u));
            }
        }
        selectedBoneOffset += model->numBones;
    }
    if (selectedBoneOffset != obj->numBones)
        return WebRendererDObjSceneResult::InvalidModel;
    if (!hasSelectedSurface) return WebRendererDObjSceneResult::NoDObj;
    prepared.matrices =
        CG_DObjCalcPose(submission.pose, obj, posePartBits);
    return prepared.matrices ? WebRendererDObjSceneResult::Success
        : WebRendererDObjSceneResult::InvalidSubmission;
}
} // namespace

WebRendererDObjSceneResult WebRenderer_ComputeDObjVisibilityBounds(
    const WebRendererDObjSubmission &submission, const WebRendererLodParms *lodParms,
    const float viewOffset[3], float mins[3], float maxs[3])
{
    PreparedDObjPose prepared;
    const auto result = PrepareDObjPose(submission, lodParms, true, prepared);
    if (result != WebRendererDObjSceneResult::Success) return result;
    // Native CG_DObjCalcPose reuses the skeleton for the current frame. Mark
    // pose use even if the subsequent cell test rejects every draw.
    CG_UsedDObjCalcPose(const_cast<cpose_t *>(submission.pose));
    return WebRenderer_ComputeDObjReceiverBounds(*submission.obj,
        prepared.matrices, prepared.partBits, viewOffset, mins, maxs)
        ? WebRendererDObjSceneResult::Success : WebRendererDObjSceneResult::InvalidModel;
}

WebRendererDObjSceneResult WebRenderer_BuildDObjSceneCommand(
    const WebRendererDObjSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererDObjSceneCommand &destination,
    const WebRendererLodParms *lodParms,
    const GfxLightGrid *lightGrid,
    const WebRendererModelLightingCallbacks *lightingCallbacks,
    WebRendererMaterialResolver materialResolver,
    const float viewOffset[3],
    const DpvsPlane *cameraPlanes,
    int cameraPlaneCount)
{
    if (submissionCount == 0u) return WebRendererDObjSceneResult::NoDObj;
    if (!submissions || cameraPlaneCount < 0 ||
        (cameraPlaneCount > 0 && !cameraPlanes))
        return WebRendererDObjSceneResult::InvalidSubmission;

    WebRendererDObjSceneCommand replacement;
    DObjSkinningScratch &scratch = SkinningScratch();
    replacement.vertices.swap(scratch.vertices);
    replacement.indices.swap(scratch.indices);
    struct RecycleGeometry
    {
        WebRendererDObjSceneCommand &command;
        ~RecycleGeometry() { WebRenderer_RecycleDObjSceneGeometry(command); }
    } recycle{replacement};
#if KISAK_WEB_DIAGNOSTICS
    WebFrameProfileSample *const profile = WebFrameProfile_Current();
    double substageStarted = profile ? WebFrameProfile_Now() : 0.0;
#endif
    bool modelLightingComplete = lightGrid != nullptr &&
        WebRenderer_InitializeModelLightingAtlas(
            submissionCount, replacement.modelLightingAtlas);
#if KISAK_WEB_DIAGNOSTICS
    if (profile)
        profile->dobjLightingMs += WebFrameProfile_Now() - substageStarted;
#endif
    try
    {
        for (std::uint32_t submissionIndex = 0u;
             submissionIndex < submissionCount; ++submissionIndex)
        {
            const WebRendererDObjSubmission &submission =
                submissions[submissionIndex];
            const DObj_s *obj = submission.obj;
            if (!obj || !submission.pose || obj->numModels == 0u ||
                obj->numModels > DOBJ_MAX_SUBMODELS || !obj->models)
            {
                return WebRendererDObjSceneResult::InvalidSubmission;
            }

#if KISAK_WEB_DIAGNOSTICS
            if (profile) substageStarted = WebFrameProfile_Now();
#endif
            // Match R_AddDObjToScene's scene-entity/model split before applying
            // R_GetSceneEntLightSurfs' DObj-only spot receiver exclusions.
            const bool sceneEntity =
                (submission.renderFlags & 4u) != 0 || obj->tree || obj->numModels != 1;
            const bool excludeTransientSpotLight = sceneEntity &&
                ((submission.renderFlags & 8u) != 0 || R_SpotLightIsAttachedToDobj(obj));
            PreparedDObjPose prepared;
            const auto poseResult = PrepareDObjPose(submission, lodParms, sceneEntity, prepared);
            if (poseResult == WebRendererDObjSceneResult::NoDObj) continue;
            if (poseResult != WebRendererDObjSceneResult::Success) return poseResult;
            const auto &selectedLods = prepared.selectedLods;
            const auto &posePartBits = prepared.partBits;
            DObjAnimMat *posedMats = prepared.matrices;
            if (sceneEntity) CG_UsedDObjCalcPose(const_cast<cpose_t *>(submission.pose));
#if KISAK_WEB_DIAGNOSTICS
            if (profile)
                profile->dobjPoseMs += WebFrameProfile_Now() - substageStarted;
#endif
            if (!posedMats) return WebRendererDObjSceneResult::InvalidSubmission;
            float receiverMins[3]{}, receiverMaxs[3]{};
            const float zeroOffset[3]{};
            if (sceneEntity && !WebRenderer_ComputeDObjReceiverBounds(*obj,
                    posedMats, posePartBits, viewOffset ? viewOffset : zeroOffset,
                    receiverMins, receiverMaxs))
                return WebRendererDObjSceneResult::InvalidModel;
            // Native R_AddEntitySurfacesInFrustumCmd retests the updated pose
            // bounds before skinning. This catches animation that moved every
            // selected bone outside the camera after the linked sphere passed.
            if (sceneEntity && cameraPlaneCount > 0 &&
                kisak::dpvs::CullBox(receiverMins, receiverMaxs,
                    cameraPlanes, cameraPlaneCount))
                continue;
            if (sceneEntity) CG_CullIn(const_cast<cpose_t *>(submission.pose));
            // Native rejects the updated pose before skinning and lighting.
            // Culled submissions must not mutate lighting handles or disable
            // the atlas for other DObjs that actually reach the draw list.
#if KISAK_WEB_DIAGNOSTICS
            if (profile) substageStarted = WebFrameProfile_Now();
#endif
            float modelLightingCoordinates[3]{};
            std::uint8_t modelPrimaryLightIndex = 0u;
            bool submissionLightingReady = modelLightingComplete;
            if (submissionLightingReady)
            {
                WebRendererModelLightingSample lightingSample{};
                submissionLightingReady = ResolveModelLighting(
                    scratch, submission, *lightGrid,
                    lightGrid->sunPrimaryLightIndex, lightingCallbacks,
                    lightingSample) &&
                    WebRenderer_SetModelLightingAtlasEntry(
                        replacement.modelLightingAtlas,
                        submissionIndex,
                        lightingSample.colors,
                        lightingSample.primaryLightWeight);
                if (submissionLightingReady)
                    modelPrimaryLightIndex = lightingSample.primaryLightIndex;
                if (submissionLightingReady)
                    WebRenderer_GetModelLightingCoordinates(
                        replacement.modelLightingAtlas,
                        submissionIndex,
                        modelLightingCoordinates);
            }
            modelLightingComplete =
                modelLightingComplete && submissionLightingReady;
#if KISAK_WEB_DIAGNOSTICS
            if (profile)
            {
                profile->dobjLightingMs += WebFrameProfile_Now() - substageStarted;
                substageStarted = WebFrameProfile_Now();
            }
#endif

            std::uint32_t hideBits[4]{};
            DObjGetHidePartBits(obj, hideBits);
            std::uint32_t globalBoneOffset = 0u;
            bool submittedDObj = false;

            for (std::uint32_t modelIndex = 0u;
                 modelIndex < obj->numModels; ++modelIndex)
            {
                const XModel *model = DObjGetModel(obj, modelIndex);
                if (!model || !model->surfs || !model->materialHandles ||
                    !model->baseMat || model->numLods <= 0 ||
                    model->numBones == 0u ||
                    globalBoneOffset + model->numBones > obj->numBones)
                {
                    return WebRendererDObjSceneResult::InvalidModel;
                }
                const int selectedLod = selectedLods[modelIndex];
                if (selectedLod < 0)
                {
                    globalBoneOffset += model->numBones;
                    continue;
                }
                if (selectedLod >= model->numLods ||
                    selectedLod >= MAX_LODS)
                {
                    return WebRendererDObjSceneResult::InvalidModel;
                }
                const XModelLodInfo &lod = model->lodInfo[selectedLod];
                if (lod.surfIndex > model->numsurfs ||
                    lod.numsurfs > model->numsurfs - lod.surfIndex)
                {
                    return WebRendererDObjSceneResult::InvalidModel;
                }

#if KISAK_WEB_DIAGNOSTICS
                if (profile) substageStarted = WebFrameProfile_Now();
#endif
                scratch.matrices.resize(model->numBones);
                for (std::uint32_t bone = 0u; bone < model->numBones; ++bone)
                {
                    DObjSkelMat inverseBase{};
                    DObjSkelMat current{};
                    ConvertQuatToInverseSkelMat(
                        &model->baseMat[bone], &inverseBase);
                    ConvertQuatToSkelMat(
                        &posedMats[globalBoneOffset + bone], &current);
                    MultiplySkelMat(
                        inverseBase, current, scratch.matrices[bone]);
                }

#if KISAK_WEB_DIAGNOSTICS
                if (profile)
                {
                    const double elapsed =
                        WebFrameProfile_Now() - substageStarted;
                    profile->dobjSkinningMs += elapsed;
                    profile->dobjMatrixMs += elapsed;
                }
#endif
                bool submittedModel = false;
                for (std::uint32_t localSurface = 0u;
                     localSurface < lod.numsurfs; ++localSurface)
                {
                    const std::uint32_t modelSurfaceIndex =
                        lod.surfIndex + localSurface;
                    const XSurface &surface = model->surfs[modelSurfaceIndex];
                    if (surface.vertCount == 0u || surface.triCount == 0u)
                        continue;
                    if (!surface.verts0 || !surface.triIndices)
                        return WebRendererDObjSceneResult::InvalidModel;
                    if (SurfaceHidden(surface, hideBits, globalBoneOffset,
                        model->numBones))
                    {
                        continue;
                    }
                    const std::uint32_t indexCount =
                        static_cast<std::uint32_t>(surface.triCount) * 3u;
                    if (replacement.vertices.size() + surface.vertCount >
                            WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES ||
                        replacement.indices.size() + indexCount >
                            WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES)
                    {
                        return WebRendererDObjSceneResult::OutputTooLarge;
                    }

#if KISAK_WEB_DIAGNOSTICS
                    if (profile) substageStarted = WebFrameProfile_Now();
#endif
                    const std::uint32_t vertexBase = static_cast<std::uint32_t>(
                        replacement.vertices.size());
                    // Construct the final span once. Skin and decode directly into it;
                    // lightmap coordinates retain their value-initialized zeroes.
                    replacement.vertices.resize(vertexBase + surface.vertCount);
                    WebRendererSurfaceVertex *vertices =
                        replacement.vertices.data() + vertexBase;
                    const bool skinned = surface.deformed
                        ? SkinWeightedSurface(surface, scratch.matrices, vertices)
                        : SkinRigidSurface(surface, scratch.matrices,
                            scratch.rigidDecodedSurfaces,
                            scratch.rigidDecodedVertexCount, vertices);
#if KISAK_WEB_DIAGNOSTICS
                    if (profile)
                    {
                        // Includes vertex construction/attributes now that emission
                        // is fused with skinning. There is no separate vertex pass.
                        const double elapsed =
                            WebFrameProfile_Now() - substageStarted;
                        profile->dobjSkinningMs += elapsed;
                        if (surface.deformed)
                            profile->dobjWeightedSkinningMs += elapsed;
                        else
                            profile->dobjRigidSkinningMs += elapsed;
                        substageStarted = WebFrameProfile_Now();
                    }
#endif
                    if (!skinned)
                        return WebRendererDObjSceneResult::InvalidModel;
#if KISAK_WEB_DIAGNOSTICS
                    const double emitStarted = substageStarted;
#endif
                    const std::uint32_t firstIndex = static_cast<std::uint32_t>(
                        replacement.indices.size());
                    replacement.indices.resize(firstIndex + indexCount);
                    std::uint32_t *indices = replacement.indices.data() + firstIndex;
                    for (std::uint32_t index = 0u; index < indexCount; ++index)
                    {
                        const std::uint32_t localIndex =
                            surface.triIndices[index];
                        if (localIndex >= surface.vertCount)
                            return WebRendererDObjSceneResult::IndexOutOfRange;
                        indices[index] = vertexBase + localIndex;
                    }
#if KISAK_WEB_DIAGNOSTICS
                    if (profile)
                        profile->dobjIndexEmitMs += WebFrameProfile_Now() - emitStarted;
#endif
                    replacement.batches.push_back(MakeDraw(
                        *model, model->materialHandles[modelSurfaceIndex],
                        modelSurfaceIndex, firstIndex, indexCount,
                        modelLightingCoordinates,
                        submissionLightingReady,
                        WebRenderer_DObjUsesDepthHack(
                            submission.renderFlags),
                        WebRenderer_DObjIsSunShadowCandidate(
                            submission.renderFlags),
                        excludeTransientSpotLight,
                        submission.entityNumber,
                        modelPrimaryLightIndex,
                        submission.reflectionProbeIndex,
                        submission.reflectionProbeImage,
                        materialResolver));
                    replacement.batches.back().dynamicLightSurfType =
                        surface.deformed ? 9u : 7u;
                    if (!sceneEntity)
                    {
                        auto &draw = replacement.batches.back();
                        std::copy_n(submission.pose->origin, 3, draw.transientLightSphere);
                        draw.transientLightSphere[3] = model->radius;
                    }
                    else
                    {
                        auto &draw = replacement.batches.back();
                        std::copy_n(receiverMins, 3, draw.transientLightMins);
                        std::copy_n(receiverMaxs, 3, draw.transientLightMaxs);
                        draw.transientLightBoundsEnabled = true;
                    }
#if KISAK_WEB_DIAGNOSTICS
                    if (profile)
                        profile->dobjGeometryMs += WebFrameProfile_Now() - substageStarted;
#endif
                    ++replacement.surfaceCount;
                    submittedModel = true;
                    submittedDObj = true;
                }
                if (submittedModel) ++replacement.modelCount;
                globalBoneOffset += model->numBones;
            }
            if (submittedDObj) ++replacement.dobjCount;
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererDObjSceneResult::AllocationFailed;
    }

    if (replacement.batches.empty())
        return WebRendererDObjSceneResult::NoDObj;
    if (!modelLightingComplete)
    {
        replacement.modelLightingAtlas = {};
        for (WebRendererWorldBatchDesc &batch : replacement.batches)
        {
            batch.lightingMode = WebRendererWorldLightingMode::None;
            std::fill_n(batch.modelLightingCoordinates, 3u, 0.0f);
        }
    }
    destination = std::move(replacement);
    return WebRendererDObjSceneResult::Success;
}

const char *WebRenderer_DObjSceneResultString(
    WebRendererDObjSceneResult result) noexcept
{
    switch (result)
    {
    case WebRendererDObjSceneResult::Success: return "success";
    case WebRendererDObjSceneResult::NoDObj:
        return "no renderable canonical DObj was submitted";
    case WebRendererDObjSceneResult::InvalidSubmission:
        return "canonical DObj submission is invalid";
    case WebRendererDObjSceneResult::InvalidModel:
        return "canonical DObj XModel or skin data is invalid";
    case WebRendererDObjSceneResult::IndexOutOfRange:
        return "canonical DObj XSurface index is outside its vertex range";
    case WebRendererDObjSceneResult::OutputTooLarge:
        return "dynamic DObj command exceeds backend limits";
    case WebRendererDObjSceneResult::AllocationFailed:
        return "dynamic DObj command allocation failed";
    }
    return "unknown DObj scene error";
}
