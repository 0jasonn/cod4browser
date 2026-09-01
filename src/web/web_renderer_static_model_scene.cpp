#include <web/web_renderer_static_model_scene.h>
#include <web/web_renderer_material_lookup.h>

#include <gfx_d3d/gfx_world_types.h>
#include <gfx_d3d/material_types.h>
#include <xanim/xmodel_types.h>
#include <xanim/xsurface_types.h>

#include <algorithm>
#include <array>
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
constexpr std::uint32_t TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX = 2u;
constexpr std::uint32_t TECHNIQUE_UNLIT_INDEX = 4u;
constexpr std::uint32_t TECHNIQUE_EMISSIVE_INDEX = 5u;
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;
constexpr std::uint32_t TECHNIQUE_LIT_INSTANCED_INDEX = 14u;
constexpr std::uint32_t TECHNIQUE_LIT_INSTANCED_SUN_INDEX = 15u;
constexpr std::uint32_t TECHNIQUE_LIT_INSTANCED_SUN_SHADOW_INDEX = 16u;
constexpr std::uint32_t ENV_MAP_PARMS_HASH = 0x3d9994dcu;
constexpr std::uint32_t DETAIL_SCALE_HASH = 0x08d36a09u;

struct ModelGroup
{
    const XModel *model = nullptr;
    std::uint8_t reflectionProbeIndex = 0u;
    std::uint8_t primaryLightIndex = 0u;
    std::vector<std::uint32_t> instanceIndices;
};

struct ModelSurfaceGeometry
{
    const XModel *model = nullptr;
    std::uint32_t modelSurfaceIndex = 0u;
    std::uint32_t firstIndex = 0u;
    std::uint32_t indexCount = 0u;
};

bool Finite3(const float value[3]) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}

bool PlacementIsFinite(const GfxPackedPlacement &placement) noexcept
{
    if (!Finite3(placement.origin) || !std::isfinite(placement.scale) ||
        placement.scale == 0.0f)
    {
        return false;
    }
    for (const auto &axis : placement.axis)
        if (!Finite3(axis)) return false;
    return true;
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
    const Material *material,
    bool directionalPrimaryLight,
    WebRendererWorldBatchDesc &draw) noexcept
{
    if (!material || !material->techniqueSet || !material->stateBitsTable)
        return false;
    const MaterialTechniqueSet *techniqueSet =
        material->techniqueSet->remappedTechniqueSet
            ? material->techniqueSet->remappedTechniqueSet
            : material->techniqueSet;
    const std::array<std::uint32_t, 6u> preferredTypes =
        directionalPrimaryLight
        ? std::array<std::uint32_t, 6u>{
            TECHNIQUE_LIT_INSTANCED_SUN_SHADOW_INDEX,
            TECHNIQUE_LIT_INSTANCED_SUN_INDEX,
            TECHNIQUE_LIT_INSTANCED_INDEX,
            TECHNIQUE_LIT_INDEX,
            TECHNIQUE_UNLIT_INDEX,
            TECHNIQUE_EMISSIVE_INDEX}
        : std::array<std::uint32_t, 6u>{
            TECHNIQUE_LIT_INSTANCED_INDEX,
            TECHNIQUE_LIT_INDEX,
            TECHNIQUE_UNLIT_INDEX,
            TECHNIQUE_EMISSIVE_INDEX,
            TECHNIQUE_LIT_INSTANCED_SUN_INDEX,
            TECHNIQUE_LIT_INSTANCED_SUN_SHADOW_INDEX};
    for (const std::uint32_t type : preferredTypes)
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

WebRendererStaticModelInstanceDesc MakeInstance(
    const GfxPackedPlacement &placement,
    float cullDistance,
    std::uint32_t canonicalIndex,
    const float modelLightingCoordinates[3]) noexcept
{
    WebRendererStaticModelInstanceDesc instance{};
    for (std::size_t row = 0u; row < 3u; ++row)
        for (std::size_t column = 0u; column < 3u; ++column)
            instance.axis[row][column] =
                placement.axis[row][column] * placement.scale;
    std::copy(std::begin(placement.origin), std::end(placement.origin),
        std::begin(instance.origin));
    if (modelLightingCoordinates)
        std::copy_n(modelLightingCoordinates, 3u,
            instance.modelLightingCoordinates);
    instance.modelScale = placement.scale;
    instance.modelCullDistance = cullDistance;
    instance.canonicalInstanceIndex = canonicalIndex;
    return instance;
}

WebRendererStaticModelShadowBounds MakeShadowBounds(
    const GfxPackedPlacement &placement,
    const XModel &model,
    const GfxStaticModelInst *canonicalBounds) noexcept
{
    WebRendererStaticModelShadowBounds bounds{};
    if (canonicalBounds)
    {
        std::copy_n(canonicalBounds->mins, 3u, bounds.mins);
        std::copy_n(canonicalBounds->maxs, 3u, bounds.maxs);
    }
    else
    {
        float localCenter[3];
        float localExtent[3];
        for (std::size_t component = 0u; component < 3u; ++component)
        {
            localCenter[component] =
                (model.mins[component] + model.maxs[component]) * 0.5f;
            localExtent[component] =
                (model.maxs[component] - model.mins[component]) * 0.5f;
        }
        for (std::size_t component = 0u; component < 3u; ++component)
        {
            float center = placement.origin[component];
            float extent = 0.0f;
            for (std::size_t axis = 0u; axis < 3u; ++axis)
            {
                const float scaledAxis =
                    placement.axis[axis][component] * placement.scale;
                center += localCenter[axis] * scaledAxis;
                extent += std::fabs(scaledAxis) * localExtent[axis];
            }
            bounds.mins[component] = center - extent;
            bounds.maxs[component] = center + extent;
        }
    }
    return bounds;
}

WebRendererWorldBatchDesc MakeDraw(
    const GfxWorld &world,
    const XModel &model,
    Material *material,
    std::uint32_t modelSurfaceIndex,
    std::uint32_t firstIndex,
    std::uint32_t indexCount,
    std::uint32_t firstInstance,
    std::uint32_t lastInstance,
    std::uint8_t reflectionProbeIndex,
    std::uint8_t primaryLightIndex) noexcept
{
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
    draw.firstInstanceIndex = firstInstance;
    draw.lastInstanceIndex = lastInstance;
    draw.lightmapIndex = 31u;
    draw.primaryLightIndex = primaryLightIndex;
    draw.sourceKind = WebRendererSceneBatchKind::StaticXModel;
    draw.cameraRegion = material ? material->cameraRegion : 0u;
    draw.castsSunShadow = material &&
        (material->info.gameFlags & 0x40u) != 0u;
    if (material && material->stateBitsTable)
    {
        const std::uint8_t shadowStateEntry =
            material->stateBitsEntry[
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
    if (world.reflectionProbes &&
        reflectionProbeIndex < world.reflectionProbeCount)
        draw.reflectionProbeImage =
            world.reflectionProbes[reflectionProbeIndex].reflectionImage;
    const bool directionalPrimaryLight = primaryLightIndex != 0u &&
        primaryLightIndex == world.sunPrimaryLightIndex;
    const bool hasTechnique = SelectTechnique(
        material, directionalPrimaryLight, draw);
    draw.ambientProbeLighting = draw.pixelShaderName &&
        (std::strncmp(draw.pixelShaderName, "lp_amb_", 7u) == 0 ||
            std::strncmp(draw.pixelShaderName, "lp_i_amb_", 9u) == 0);
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
    const bool environmentSpecular = hasTechnique && draw.pixelShaderName &&
        std::strncmp(draw.pixelShaderName, "lp_", 3u) == 0 &&
        std::strstr(draw.pixelShaderName, "s0_sm3.hlsl") != nullptr &&
        draw.specularImage && draw.reflectionProbeImage &&
        WebRenderer_CopyMaterialConstant(material, ENV_MAP_PARMS_HASH, draw.envMapParms);
    draw.technique = !hasTechnique || !draw.baseImage
        ? WebRendererWorldTechnique::BackendFallback
        : environmentSpecular
            ? (normalMapped
                ? WebRendererWorldTechnique::BaseTextureNormalSpecular
                : WebRendererWorldTechnique::BaseTextureSpecular)
            : WebRendererWorldTechnique::BaseTexture;
    return draw;
}
} // namespace

WebRendererStaticModelSceneResult WebRenderer_BuildStaticModelSceneCommand(
    const GfxWorld &world,
    WebRendererStaticModelSceneCommand &destination,
    const WebRendererModelLightingCallbacks *lightingCallbacks,
    WebRendererStaticMaterialResolver materialResolver)
{
    if (world.dpvs.smodelCount == 0u)
        return WebRendererStaticModelSceneResult::NoStaticModels;
    if (!world.dpvs.smodelDrawInsts)
        return WebRendererStaticModelSceneResult::InvalidWorld;

    WebRendererStaticModelSceneCommand replacement;
    replacement.canonicalInstanceCount = world.dpvs.smodelCount;
    replacement.modelLightingSourceAvailable =
        world.dpvs.smodelInsts != nullptr;
    bool modelLightingComplete = false;
    try
    {
        std::vector<ModelGroup> groups;
        groups.reserve(256u);
        for (std::uint32_t instanceIndex = 0u;
             instanceIndex < world.dpvs.smodelCount; ++instanceIndex)
        {
            const GfxStaticModelDrawInst &instance =
                world.dpvs.smodelDrawInsts[instanceIndex];
            const XModel *model = instance.model;
            if (!model || !model->surfs || !model->materialHandles ||
                model->numLods <= 0)
            {
                continue;
            }
            if (!PlacementIsFinite(instance.placement) ||
                !std::isfinite(instance.cullDist))
                return WebRendererStaticModelSceneResult::InvalidPlacement;
            auto group = std::find_if(groups.begin(), groups.end(),
                [model, &instance](const ModelGroup &candidate) {
                    return candidate.model == model &&
                        candidate.reflectionProbeIndex ==
                            instance.reflectionProbeIndex &&
                        candidate.primaryLightIndex ==
                            instance.primaryLightIndex;
                });
            if (group == groups.end())
            {
                groups.push_back({
                    model, instance.reflectionProbeIndex,
                    instance.primaryLightIndex, {}});
                group = std::prev(groups.end());
            }
            group->instanceIndices.push_back(instanceIndex);
        }

        std::uint32_t submittedInstanceCount = 0u;
        std::vector<const XModel *> submittedModels;
        submittedModels.reserve(groups.size());
        std::vector<ModelSurfaceGeometry> retainedGeometry;
        retainedGeometry.reserve(512u);
        for (const ModelGroup &group : groups)
            submittedInstanceCount += static_cast<std::uint32_t>(
                group.instanceIndices.size());
        replacement.instances.reserve(submittedInstanceCount);
        replacement.shadowBounds.reserve(submittedInstanceCount);
        modelLightingComplete = replacement.modelLightingSourceAvailable &&
            WebRenderer_InitializeModelLightingAtlas(
                submittedInstanceCount, replacement.modelLightingAtlas);

        for (const ModelGroup &group : groups)
        {
            const XModel &model = *group.model;
            const std::uint32_t lodCount = static_cast<std::uint32_t>(
                std::min<int>(model.numLods, MAX_LODS));
            const std::uint32_t instanceOffset =
                static_cast<std::uint32_t>(replacement.instances.size());
            if (replacement.instances.size() + group.instanceIndices.size() >
                WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES)
            {
                return WebRendererStaticModelSceneResult::OutputTooLarge;
            }
            for (const std::uint32_t canonicalIndex : group.instanceIndices)
            {
                float lightingCoordinates[3]{};
                bool lightingReady = modelLightingComplete;
                const std::uint32_t lightingEntry =
                    static_cast<std::uint32_t>(replacement.instances.size());
                if (lightingReady)
                {
                    const GfxStaticModelInst &modelInstance =
                        world.dpvs.smodelInsts[canonicalIndex];
                    if (modelInstance.groundLighting.packed != 0u)
                    {
                        lightingReady =
                            WebRenderer_SetModelGroundLightingAtlasEntry(
                                replacement.modelLightingAtlas,
                                lightingEntry,
                                modelInstance.groundLighting.packed);
                    }
                    else
                    {
                        const float lightingOrigin[3]{
                            (modelInstance.mins[0] + modelInstance.maxs[0]) *
                                0.5f,
                            (modelInstance.mins[1] + modelInstance.maxs[1]) *
                                0.5f,
                            (modelInstance.mins[2] + modelInstance.maxs[2]) *
                                0.5f,
                        };
                        WebRendererModelLightingSample sample{};
                        lightingReady = WebRenderer_EvaluateModelLighting(
                            world.lightGrid,
                            lightingOrigin,
                            world.dpvs.smodelDrawInsts[canonicalIndex]
                                .primaryLightIndex,
                            lightingCallbacks,
                            sample) &&
                            WebRenderer_SetModelLightingAtlasEntry(
                                replacement.modelLightingAtlas,
                                lightingEntry,
                                sample.colors,
                                sample.primaryLightWeight);
                    }
                    if (lightingReady)
                        WebRenderer_GetModelLightingCoordinates(
                            replacement.modelLightingAtlas,
                            lightingEntry,
                            lightingCoordinates);
                }
                if (!lightingReady)
                    ++replacement.modelLightingFailureCount;
                modelLightingComplete = modelLightingComplete && lightingReady;
                const GfxPackedPlacement &placement =
                    world.dpvs.smodelDrawInsts[canonicalIndex].placement;
                replacement.instances.push_back(MakeInstance(
                    placement,
                    world.dpvs.smodelDrawInsts[canonicalIndex].cullDist,
                    canonicalIndex,
                    lightingCoordinates));
                replacement.shadowBounds.push_back(MakeShadowBounds(
                    placement,
                    model,
                    world.dpvs.smodelInsts
                        ? &world.dpvs.smodelInsts[canonicalIndex] : nullptr));
            }

            bool modelSubmitted = false;
            for (std::uint32_t lod = 0u; lod < lodCount; ++lod)
            {
                const XModelLodInfo &lodInfo = model.lodInfo[lod];
                if (lodInfo.surfIndex > model.numsurfs ||
                    lodInfo.numsurfs > model.numsurfs - lodInfo.surfIndex)
                {
                    return WebRendererStaticModelSceneResult::InvalidModel;
                }
                for (std::uint32_t localSurface = 0u;
                     localSurface < lodInfo.numsurfs; ++localSurface)
                {
                    const std::uint32_t modelSurfaceIndex =
                        lodInfo.surfIndex + localSurface;
                    const XSurface &surface = model.surfs[modelSurfaceIndex];
                    if (surface.deformed || surface.vertCount == 0u ||
                        surface.triCount == 0u)
                    {
                        continue;
                    }
                    if (!surface.verts0 || !surface.triIndices)
                        return WebRendererStaticModelSceneResult::InvalidModel;
                    const std::uint32_t indexCount =
                        static_cast<std::uint32_t>(surface.triCount) * 3u;
                    auto retained = std::find_if(
                        retainedGeometry.begin(), retainedGeometry.end(),
                        [&model, modelSurfaceIndex](
                            const ModelSurfaceGeometry &candidate) {
                            return candidate.model == &model &&
                                candidate.modelSurfaceIndex ==
                                    modelSurfaceIndex;
                        });
                    std::uint32_t firstIndex = 0u;
                    if (retained != retainedGeometry.end())
                    {
                        firstIndex = retained->firstIndex;
                    }
                    else
                    {
                        if (replacement.vertices.size() + surface.vertCount >
                                WEB_RENDERER_MAX_STATIC_MODEL_VERTICES ||
                            replacement.indices.size() + indexCount >
                                WEB_RENDERER_MAX_STATIC_MODEL_INDICES)
                        {
                            return WebRendererStaticModelSceneResult::OutputTooLarge;
                        }
                        const std::uint32_t vertexBase =
                            static_cast<std::uint32_t>(
                                replacement.vertices.size());
                        for (std::uint32_t vertexIndex = 0u;
                             vertexIndex < surface.vertCount; ++vertexIndex)
                        {
                            const GfxPackedVertex &source =
                                surface.verts0[vertexIndex];
                            if (!Finite3(source.xyz))
                                return WebRendererStaticModelSceneResult::InvalidModel;
                            WebRendererSurfaceVertex vertex{};
                            std::copy(std::begin(source.xyz),
                                std::end(source.xyz),
                                std::begin(vertex.position));
                            vertex.color[0] = static_cast<float>(
                                (source.color.packed >> 16u) & 0xffu) *
                                BYTE_TO_UNIT;
                            vertex.color[1] = static_cast<float>(
                                (source.color.packed >> 8u) & 0xffu) *
                                BYTE_TO_UNIT;
                            vertex.color[2] = static_cast<float>(
                                source.color.packed & 0xffu) * BYTE_TO_UNIT;
                            vertex.color[3] = static_cast<float>(
                                (source.color.packed >> 24u) & 0xffu) *
                                BYTE_TO_UNIT;
                            Vec2UnpackTexCoords(source.texCoord,
                                vertex.textureCoordinate);
                            Vec3UnpackUnitVec(source.normal, vertex.normal);
                            Vec3UnpackUnitVec(source.tangent, vertex.tangent);
                            vertex.binormalSign = source.binormalSign;
                            if (!std::isfinite(vertex.textureCoordinate[0]) ||
                                !std::isfinite(vertex.textureCoordinate[1]) ||
                                !Finite3(vertex.normal) ||
                                !Finite3(vertex.tangent) ||
                                !std::isfinite(vertex.binormalSign))
                            {
                                return WebRendererStaticModelSceneResult::InvalidModel;
                            }
                            replacement.vertices.push_back(vertex);
                        }
                        firstIndex = static_cast<std::uint32_t>(
                            replacement.indices.size());
                        for (std::uint32_t index = 0u; index < indexCount;
                             ++index)
                        {
                            const std::uint32_t localIndex =
                                surface.triIndices[index];
                            if (localIndex >= surface.vertCount)
                                return WebRendererStaticModelSceneResult::IndexOutOfRange;
                            replacement.indices.push_back(
                                vertexBase + localIndex);
                        }
                        retainedGeometry.push_back({
                            &model, modelSurfaceIndex, firstIndex, indexCount});
                    }
                    WebRendererStaticModelBatchDesc batch{};
                    Material *material =
                        model.materialHandles[modelSurfaceIndex];
                    if (materialResolver)
                    {
                        if (Material *canonical = materialResolver(material))
                            material = canonical;
                    }
                    batch.draw = MakeDraw(
                        world,
                        model,
                        material,
                        modelSurfaceIndex,
                        firstIndex,
                        indexCount,
                        group.instanceIndices.front(),
                        group.instanceIndices.back(),
                        group.reflectionProbeIndex,
                        group.primaryLightIndex);
                    batch.instanceOffset = instanceOffset;
                    batch.instanceCount = static_cast<std::uint32_t>(
                        group.instanceIndices.size());
                    batch.lodIndex = static_cast<std::uint8_t>(lod);
                    replacement.batches.push_back(batch);
                    ++replacement.surfaceCount;
                    modelSubmitted = true;
                }
            }
            if (modelSubmitted && std::find(submittedModels.begin(),
                    submittedModels.end(), &model) == submittedModels.end())
            {
                submittedModels.push_back(&model);
                ++replacement.modelCount;
            }
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererStaticModelSceneResult::AllocationFailed;
    }

    if (replacement.batches.empty())
        return WebRendererStaticModelSceneResult::NoStaticModels;
    if (replacement.shadowBounds.size() != replacement.instances.size())
        return WebRendererStaticModelSceneResult::InvalidWorld;
    if (modelLightingComplete &&
        replacement.modelLightingAtlas.entryCount ==
            replacement.instances.size())
    {
        for (WebRendererStaticModelBatchDesc &batch : replacement.batches)
            batch.draw.lightingMode =
                WebRendererWorldLightingMode::ModelLightGrid;
    }
    else
    {
        replacement.modelLightingAtlas = {};
        for (WebRendererStaticModelInstanceDesc &instance :
             replacement.instances)
            std::fill_n(instance.modelLightingCoordinates, 3u, 0.0f);
    }
    destination = std::move(replacement);
    return WebRendererStaticModelSceneResult::Success;
}

const char *WebRenderer_StaticModelSceneResultString(
    WebRendererStaticModelSceneResult result) noexcept
{
    switch (result)
    {
    case WebRendererStaticModelSceneResult::Success: return "success";
    case WebRendererStaticModelSceneResult::NoStaticModels:
        return "canonical world has no renderable static XModels";
    case WebRendererStaticModelSceneResult::InvalidWorld:
        return "canonical world has no static XModel draw instances";
    case WebRendererStaticModelSceneResult::InvalidModel:
        return "canonical static XModel has invalid LOD surface data";
    case WebRendererStaticModelSceneResult::InvalidPlacement:
        return "canonical static XModel has an invalid placement";
    case WebRendererStaticModelSceneResult::IndexOutOfRange:
        return "canonical XSurface index is outside its vertex range";
    case WebRendererStaticModelSceneResult::OutputTooLarge:
        return "static XModel command exceeds backend limits";
    case WebRendererStaticModelSceneResult::AllocationFailed:
        return "static XModel command allocation failed";
    }
    return "unknown static XModel scene error";
}

bool WebRenderer_StaticModelIntersectsShadowPartition(
    const WebRendererStaticModelShadowBounds &bounds,
    const std::array<float, 16> &shadowMatrix) noexcept
{
    return WebRenderer_ShadowBoundsIntersectPartition(bounds, shadowMatrix);
}

bool WebRenderer_ShadowBoundsIntersectPartition(
    const WebRendererStaticModelShadowBounds &bounds,
    const std::array<float, 16> &shadowMatrix) noexcept
{
    float center[3];
    float extent[3];
    for (std::size_t component = 0u; component < 3u; ++component)
    {
        center[component] =
            (bounds.mins[component] + bounds.maxs[component]) * 0.5f;
        extent[component] =
            (bounds.maxs[component] - bounds.mins[component]) * 0.5f;
    }
    float clipCenter[4];
    float clipExtent[3];
    for (std::size_t row = 0u; row < 4u; ++row)
    {
        clipCenter[row] = shadowMatrix[row] * center[0] +
            shadowMatrix[4u + row] * center[1] +
            shadowMatrix[8u + row] * center[2] +
            shadowMatrix[12u + row];
        if (row < 3u)
            clipExtent[row] = std::fabs(shadowMatrix[row]) * extent[0] +
                std::fabs(shadowMatrix[4u + row]) * extent[1] +
                std::fabs(shadowMatrix[8u + row]) * extent[2];
    }
    const float w = clipCenter[3];
    return clipCenter[0] + clipExtent[0] >= -w &&
        clipCenter[0] - clipExtent[0] <= w &&
        clipCenter[1] + clipExtent[1] >= -w &&
        clipCenter[1] - clipExtent[1] <= w &&
        clipCenter[2] + clipExtent[2] >= -w &&
        clipCenter[2] - clipExtent[2] <= w;
}

bool WebRenderer_BuildStaticModelSpotShadowVisibility(
    const WebRendererStaticModelInstanceDesc *instances,
    std::uint32_t instanceCount,
    const WebRendererSpotShadowStaticModelDesc *memberships,
    std::uint32_t membershipCount,
    std::uint32_t primaryLightIndex,
    std::uint8_t *visibility,
    std::uint32_t visibilityCount) noexcept
{
    if (!instances || !visibility || visibilityCount != instanceCount ||
        (membershipCount != 0u && !memberships))
        return false;
    if (membershipCount == 0u)
    {
        std::fill_n(visibility, instanceCount, 0u);
        return true;
    }
    const auto less = [](const WebRendererSpotShadowStaticModelDesc &left,
                         const WebRendererSpotShadowStaticModelDesc &right)
    {
        if (left.primaryLightIndex != right.primaryLightIndex)
            return left.primaryLightIndex < right.primaryLightIndex;
        return left.canonicalInstanceIndex < right.canonicalInstanceIndex;
    };
    const WebRendererSpotShadowStaticModelDesc firstTarget{
        primaryLightIndex, 0u};
    const WebRendererSpotShadowStaticModelDesc lastTarget{
        primaryLightIndex, UINT32_MAX};
    const auto begin = std::lower_bound(
        memberships, memberships + membershipCount, firstTarget, less);
    const auto end = std::upper_bound(
        begin, memberships + membershipCount, lastTarget, less);
    for (std::uint32_t index = 0u; index < instanceCount; ++index)
    {
        const WebRendererSpotShadowStaticModelDesc target{
            primaryLightIndex, instances[index].canonicalInstanceIndex};
        visibility[index] = std::binary_search(begin, end, target, less)
            ? 1u : 0u;
    }
    return true;
}

bool WebRenderer_PackStaticModelCameraInstances(
    const WebRendererStaticModelInstanceDesc *source, std::uint32_t sourceCount,
    const std::int8_t *selectedLods, const std::uint8_t *visibility,
    std::uint32_t visibilityCount, bool visibilityComputed,
    WebRendererStaticModelInstanceDesc *destination,
    std::array<std::uint32_t, 4> &lodOffsets,
    std::array<std::uint32_t, 4> &lodCounts) noexcept
{
    if (sourceCount && (!source || !selectedLods || !destination)) return false;
    if (visibilityComputed && visibilityCount && !visibility) return false;
    for (std::uint32_t index = 0; index < sourceCount; ++index)
    {
        if (selectedLods[index] < -1 || selectedLods[index] >= 4 ||
            (visibilityComputed && source[index].canonicalInstanceIndex >= visibilityCount))
            return false;
    }
    lodCounts.fill(0u);
    std::uint32_t writeOffset = 0u;
    for (std::size_t lod = 0; lod < lodOffsets.size(); ++lod)
    {
        lodOffsets[lod] = writeOffset;
        for (std::uint32_t index = 0; index < sourceCount; ++index)
        {
            if (selectedLods[index] != static_cast<std::int8_t>(lod)) continue;
            if (visibilityComputed && !visibility[source[index].canonicalInstanceIndex]) continue;
            destination[writeOffset++] = source[index];
            ++lodCounts[lod];
        }
    }
    return true;
}
