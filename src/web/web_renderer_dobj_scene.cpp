#include <web/web_renderer_dobj_scene.h>

#include <cgame/cg_pose.h>
#include <gfx_d3d/gfx_world_types.h>
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

bool Finite3(const float value[3]) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]);
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

const GfxImage *FindNormalImage(
    const Material *material, std::uint8_t &sampler) noexcept
{
    if (!material || !material->textureTable) return nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.semantic == 5u && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
    }
    return nullptr;
}

const GfxImage *FindSpecularImage(
    const Material *material, std::uint8_t &sampler) noexcept
{
    if (!material || !material->textureTable) return nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.semantic == 8u && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
    }
    return nullptr;
}

bool CopyMaterialConstant(const Material *material, std::uint32_t nameHash,
    float output[4]) noexcept
{
    if (!material || !material->constantTable) return false;
    for (std::uint32_t index = 0u; index < material->constantCount; ++index)
    {
        const MaterialConstantDef &constant = material->constantTable[index];
        if (constant.nameHash == nameHash)
        {
            std::copy_n(constant.literal, 4u, output);
            return true;
        }
    }
    return false;
}

std::uint32_t HashPixelShaderProgram(
    const MaterialPixelShader *shader) noexcept
{
    if (!shader || !shader->prog.loadDef.program ||
        shader->prog.loadDef.programSize == 0u) return 0u;
    std::uint32_t hash = 2166136261u;
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(
        shader->prog.loadDef.program);
    const std::size_t byteCount =
        static_cast<std::size_t>(shader->prog.loadDef.programSize) * 4u;
    for (std::size_t index = 0u; index < byteCount; ++index)
        hash = (hash ^ bytes[index]) * 16777619u;
    return hash;
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
        draw.pixelShaderProgramHash = HashPixelShaderProgram(pixelShader);
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
    std::vector<float> &positions,
    std::vector<float> &normals,
    std::vector<float> &tangents)
{
    if (!surface.vertInfo.vertsBlend) return false;
    positions.resize(static_cast<std::size_t>(surface.vertCount) * 3u);
    normals.resize(static_cast<std::size_t>(surface.vertCount) * 3u);
    tangents.resize(static_cast<std::size_t>(surface.vertCount) * 3u);
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
            float weightedSecondaryNormal[3]{};
            float weightedSecondaryTangent[3]{};
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
                float secondaryNormal[3]{};
                float secondaryTangent[3]{};
                TransformPosition(vertex.xyz, *secondary, secondaryPosition);
                TransformDirection(
                    unpackedNormal, *secondary, secondaryNormal);
                TransformDirection(
                    unpackedTangent, *secondary, secondaryTangent);
                for (std::size_t axis = 0u; axis < 3u; ++axis)
                {
                    weightedSecondary[axis] +=
                        weight * secondaryPosition[axis];
                    weightedSecondaryNormal[axis] +=
                        weight * secondaryNormal[axis];
                    weightedSecondaryTangent[axis] +=
                        weight * secondaryTangent[axis];
                }
                explicitWeight += weight;
            }
            const float primaryWeight = 1.0f - explicitWeight;
            for (std::size_t axis = 0u; axis < 3u; ++axis)
            {
                positions[vertexIndex * 3u + axis] =
                    primaryWeight * transformed[axis] +
                    weightedSecondary[axis];
                normals[vertexIndex * 3u + axis] =
                    primaryWeight * transformedNormal[axis] +
                    weightedSecondaryNormal[axis];
                tangents[vertexIndex * 3u + axis] =
                    primaryWeight * transformedTangent[axis] +
                    weightedSecondaryTangent[axis];
            }
            if (!NormalizeDirection(&normals[vertexIndex * 3u]) ||
                !NormalizeDirection(&tangents[vertexIndex * 3u]))
                return false;
            blend += 1u + influenceCount * 2u;
        }
    }
    return vertexIndex == surface.vertCount;
}

bool SkinRigidSurface(
    const XSurface &surface,
    const std::vector<DObjSkelMat> &matrices,
    std::vector<float> &positions,
    std::vector<float> &normals,
    std::vector<float> &tangents)
{
    if (!surface.vertList || surface.vertListCount == 0u) return false;
    positions.resize(static_cast<std::size_t>(surface.vertCount) * 3u);
    normals.resize(static_cast<std::size_t>(surface.vertCount) * 3u);
    tangents.resize(static_cast<std::size_t>(surface.vertCount) * 3u);
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
            TransformPosition(surface.verts0[vertexIndex].xyz, *matrix,
                &positions[vertexIndex * 3u]);
            float unpackedNormal[3]{};
            Vec3UnpackUnitVec(
                surface.verts0[vertexIndex].normal, unpackedNormal);
            TransformDirection(unpackedNormal, *matrix,
                &normals[vertexIndex * 3u]);
            float unpackedTangent[3]{};
            Vec3UnpackUnitVec(
                surface.verts0[vertexIndex].tangent, unpackedTangent);
            TransformDirection(unpackedTangent, *matrix,
                &tangents[vertexIndex * 3u]);
            if (!NormalizeDirection(&normals[vertexIndex * 3u]) ||
                !NormalizeDirection(&tangents[vertexIndex * 3u]))
                return false;
        }
    }
    return vertexIndex == surface.vertCount;
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
    draw.sourceKind = WebRendererSceneBatchKind::DynamicDObj;
    draw.depthHack = depthHack;
    const MaterialTechniqueSet *shadowSet =
        material ? material->techniqueSet : nullptr;
    if (shadowSet && shadowSet->remappedTechniqueSet)
        shadowSet = shadowSet->remappedTechniqueSet;
    // Native R_AddXModelSurfaces checks the requested build-shadowmap
    // technique for scene and dynamic models; the material game flag is the
    // specialized static-model shortcut and must not reject these draws.
    draw.castsSunShadow = castsSunShadow && shadowSet &&
        shadowSet->techniques[TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX];
    draw.baseImage = FindBaseImage(material, draw.samplerState);
    draw.normalImage = FindNormalImage(material, draw.normalSamplerState);
    draw.specularImage = FindSpecularImage(
        material, draw.specularSamplerState);
    draw.reflectionProbeIndex = reflectionProbeIndex;
    draw.reflectionProbeImage = reflectionProbeImage;
    // The WebGL compatibility technique is deliberately a base-color subset
    // of the canonical material. Preserve canonical state when one of the
    // common passes supplies it, but a DB-owned color image remains enough to
    // use that supported subset even when the original shader itself is not.
    SelectTechnique(material, draw);
    const bool normalMapped = draw.pixelShaderName &&
        std::strstr(draw.pixelShaderName, "n0") != nullptr;
    if (!normalMapped)
        draw.normalImage = nullptr;
    const bool environmentSpecular = draw.pixelShaderName &&
        std::strncmp(draw.pixelShaderName, "lp_", 3u) == 0 &&
        std::strstr(draw.pixelShaderName, "s0_sm3.hlsl") != nullptr &&
        draw.specularImage && draw.reflectionProbeImage &&
        CopyMaterialConstant(material, ENV_MAP_PARMS_HASH, draw.envMapParms);
    draw.technique = !draw.baseImage
        ? WebRendererWorldTechnique::BackendFallback
        : WebRenderer_IsReflexSightTechnique(draw.techniqueName)
            ? WebRendererWorldTechnique::ReflexSight
            : environmentSpecular
                ? (normalMapped
                    ? WebRendererWorldTechnique::BaseTextureNormalSpecular
                    : WebRendererWorldTechnique::BaseTextureSpecular)
                : WebRendererWorldTechnique::BaseTexture;
    // Only the canonical lit pass consumes the model-light-grid constants.
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

WebRendererDObjSceneResult WebRenderer_BuildDObjSceneCommand(
    const WebRendererDObjSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererDObjSceneCommand &destination,
    const WebRendererLodParms *lodParms,
    const GfxLightGrid *lightGrid,
    const WebRendererModelLightingCallbacks *lightingCallbacks,
    WebRendererMaterialResolver materialResolver)
{
    if (submissionCount == 0u) return WebRendererDObjSceneResult::NoDObj;
    if (!submissions) return WebRendererDObjSceneResult::InvalidSubmission;

    WebRendererDObjSceneCommand replacement;
    bool modelLightingComplete = lightGrid != nullptr &&
        WebRenderer_InitializeModelLightingAtlas(
            submissionCount, replacement.modelLightingAtlas);
    try
    {
        for (std::uint32_t submissionIndex = 0u;
             submissionIndex < submissionCount; ++submissionIndex)
        {
            const WebRendererDObjSubmission &submission =
                submissions[submissionIndex];
            const DObj_s *obj = submission.obj;
            if (!obj || !submission.pose || obj->numModels == 0u ||
                !obj->models)
            {
                return WebRendererDObjSceneResult::InvalidSubmission;
            }

            float modelLightingCoordinates[3]{};
            bool submissionLightingReady = modelLightingComplete;
            if (submissionLightingReady)
            {
                WebRendererModelLightingSample lightingSample{};
                submissionLightingReady = WebRenderer_EvaluateModelLighting(
                    *lightGrid,
                    submission.lightingOrigin,
                    lightGrid->sunPrimaryLightIndex,
                    lightingCallbacks,
                    lightingSample) &&
                    WebRenderer_SetModelLightingAtlasEntry(
                        replacement.modelLightingAtlas,
                        submissionIndex,
                        lightingSample.colors,
                        lightingSample.primaryLightWeight);
                if (submissionLightingReady)
                    WebRenderer_GetModelLightingCoordinates(
                        replacement.modelLightingAtlas,
                        submissionIndex,
                        modelLightingCoordinates);
            }
            modelLightingComplete =
                modelLightingComplete && submissionLightingReady;

            int posePartBits[4] = {-1, -1, -1, -1};
            DObjAnimMat *posedMats =
                CG_DObjCalcPose(submission.pose, obj, posePartBits);
            if (!posedMats) return WebRendererDObjSceneResult::InvalidSubmission;
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
                const int selectedLod = WebRenderer_SelectDObjLod(
                    model, submission.pose->origin, lodParms);
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

                std::vector<DObjSkelMat> skinMatrices(model->numBones);
                for (std::uint32_t bone = 0u; bone < model->numBones; ++bone)
                {
                    DObjSkelMat inverseBase{};
                    DObjSkelMat current{};
                    ConvertQuatToInverseSkelMat(
                        &model->baseMat[bone], &inverseBase);
                    ConvertQuatToSkelMat(
                        &posedMats[globalBoneOffset + bone], &current);
                    MultiplySkelMat(inverseBase, current, skinMatrices[bone]);
                }

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

                    std::vector<float> positions;
                    std::vector<float> normals;
                    std::vector<float> tangents;
                    const bool skinned = surface.deformed
                        ? SkinWeightedSurface(
                            surface, skinMatrices, positions, normals,
                            tangents)
                        : SkinRigidSurface(
                            surface, skinMatrices, positions, normals,
                            tangents);
                    if (!skinned)
                        return WebRendererDObjSceneResult::InvalidModel;

                    const std::uint32_t vertexBase = static_cast<std::uint32_t>(
                        replacement.vertices.size());
                    for (std::uint32_t vertexIndex = 0u;
                         vertexIndex < surface.vertCount; ++vertexIndex)
                    {
                        const GfxPackedVertex &source =
                            surface.verts0[vertexIndex];
                        WebRendererSurfaceVertex vertex{};
                        std::copy_n(&positions[vertexIndex * 3u], 3u,
                            vertex.position);
                        std::copy_n(&normals[vertexIndex * 3u], 3u,
                            vertex.normal);
                        std::copy_n(&tangents[vertexIndex * 3u], 3u,
                            vertex.tangent);
                        vertex.binormalSign = source.binormalSign;
                        if (!Finite3(vertex.position) || !Finite3(vertex.normal) ||
                            !Finite3(vertex.tangent) ||
                            !std::isfinite(vertex.binormalSign))
                            return WebRendererDObjSceneResult::InvalidModel;
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
                            return WebRendererDObjSceneResult::InvalidModel;
                        }
                        replacement.vertices.push_back(vertex);
                    }
                    const std::uint32_t firstIndex = static_cast<std::uint32_t>(
                        replacement.indices.size());
                    for (std::uint32_t index = 0u; index < indexCount; ++index)
                    {
                        const std::uint32_t localIndex =
                            surface.triIndices[index];
                        if (localIndex >= surface.vertCount)
                            return WebRendererDObjSceneResult::IndexOutOfRange;
                        replacement.indices.push_back(vertexBase + localIndex);
                    }
                    replacement.batches.push_back(MakeDraw(
                        *model, model->materialHandles[modelSurfaceIndex],
                        modelSurfaceIndex, firstIndex, indexCount,
                        modelLightingCoordinates,
                        submissionLightingReady,
                        WebRenderer_DObjUsesDepthHack(
                            submission.renderFlags),
                        WebRenderer_DObjIsSunShadowCandidate(
                            submission.renderFlags),
                        submission.reflectionProbeIndex,
                        submission.reflectionProbeImage,
                        materialResolver));
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
