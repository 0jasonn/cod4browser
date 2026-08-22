#include <web/web_renderer_static_model_scene.h>

#include <gfx_d3d/gfx_world_types.h>
#include <gfx_d3d/material_types.h>
#include <xanim/xmodel_types.h>
#include <xanim/xsurface_types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <vector>

void __cdecl Vec2UnpackTexCoords(PackedTexCoords in, float *out);
void __cdecl Vec3UnpackUnitVec(PackedUnitVec in, float *out);

namespace
{
constexpr float BYTE_TO_UNIT = 1.0f / 255.0f;
constexpr std::uint32_t TECHNIQUE_UNLIT_INDEX = 4u;
constexpr std::uint32_t TECHNIQUE_EMISSIVE_INDEX = 5u;
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;

struct ModelGroup
{
    const XModel *model = nullptr;
    std::vector<std::uint32_t> instanceIndices;
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

const GfxImage *FindBaseImage(
    const Material *material,
    std::uint8_t &sampler) noexcept
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
    const Material *material,
    std::uint32_t stateBits[2],
    const char *&techniqueName,
    std::uint8_t &techniqueType) noexcept
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
        stateBits[0] = material->stateBitsTable[entry].loadBits[0];
        stateBits[1] = material->stateBitsTable[entry].loadBits[1];
        techniqueName = technique->name;
        techniqueType = static_cast<std::uint8_t>(type);
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
            stateBits[0] = material->stateBitsTable[entry].loadBits[0];
            stateBits[1] = material->stateBitsTable[entry].loadBits[1];
            techniqueName = material->techniqueSet->name;
            techniqueType = static_cast<std::uint8_t>(type);
            return true;
        }
    }
    return false;
}

WebRendererStaticModelInstanceDesc MakeInstance(
    const GfxPackedPlacement &placement,
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
    instance.canonicalInstanceIndex = canonicalIndex;
    return instance;
}

WebRendererWorldBatchDesc MakeDraw(
    const XModel &model,
    Material *material,
    std::uint32_t modelSurfaceIndex,
    std::uint32_t firstIndex,
    std::uint32_t indexCount,
    std::uint32_t firstInstance,
    std::uint32_t lastInstance) noexcept
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
    draw.sourceKind = WebRendererSceneBatchKind::StaticXModel;
    draw.baseImage = FindBaseImage(material, draw.samplerState);
    const bool hasTechnique = SelectTechnique(material, draw.stateBits,
        draw.techniqueName, draw.techniqueType);
    draw.technique = hasTechnique && draw.baseImage
        ? WebRendererWorldTechnique::BaseTexture
        : WebRendererWorldTechnique::BackendFallback;
    return draw;
}
} // namespace

WebRendererStaticModelSceneResult WebRenderer_BuildStaticModelSceneCommand(
    const GfxWorld &world,
    WebRendererStaticModelSceneCommand &destination,
    const WebRendererModelLightingCallbacks *lightingCallbacks)
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
            if (!PlacementIsFinite(instance.placement))
                return WebRendererStaticModelSceneResult::InvalidPlacement;
            auto group = std::find_if(groups.begin(), groups.end(),
                [model](const ModelGroup &candidate) {
                    return candidate.model == model;
                });
            if (group == groups.end())
            {
                groups.push_back({model, {}});
                group = std::prev(groups.end());
            }
            group->instanceIndices.push_back(instanceIndex);
        }

        std::uint32_t submittedInstanceCount = 0u;
        for (const ModelGroup &group : groups)
            submittedInstanceCount += static_cast<std::uint32_t>(
                group.instanceIndices.size());
        modelLightingComplete = replacement.modelLightingSourceAvailable &&
            WebRenderer_InitializeModelLightingAtlas(
                submittedInstanceCount, replacement.modelLightingAtlas);

        for (const ModelGroup &group : groups)
        {
            const XModel &model = *group.model;
            const std::uint32_t lod = static_cast<std::uint32_t>(
                std::min<int>(model.numLods - 1, MAX_LODS - 1));
            const XModelLodInfo &lodInfo = model.lodInfo[lod];
            if (lodInfo.surfIndex > model.numsurfs ||
                lodInfo.numsurfs > model.numsurfs - lodInfo.surfIndex)
            {
                return WebRendererStaticModelSceneResult::InvalidModel;
            }
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
                replacement.instances.push_back(MakeInstance(
                    world.dpvs.smodelDrawInsts[canonicalIndex].placement,
                    canonicalIndex,
                    lightingCoordinates));
            }

            bool modelSubmitted = false;
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
                if (replacement.vertices.size() + surface.vertCount >
                        WEB_RENDERER_MAX_STATIC_MODEL_VERTICES ||
                    replacement.indices.size() + indexCount >
                        WEB_RENDERER_MAX_STATIC_MODEL_INDICES)
                {
                    return WebRendererStaticModelSceneResult::OutputTooLarge;
                }
                const std::uint32_t vertexBase =
                    static_cast<std::uint32_t>(replacement.vertices.size());
                for (std::uint32_t vertexIndex = 0u;
                     vertexIndex < surface.vertCount; ++vertexIndex)
                {
                    const GfxPackedVertex &source = surface.verts0[vertexIndex];
                    if (!Finite3(source.xyz))
                        return WebRendererStaticModelSceneResult::InvalidModel;
                    WebRendererSurfaceVertex vertex{};
                    std::copy(std::begin(source.xyz), std::end(source.xyz),
                        std::begin(vertex.position));
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
                    Vec3UnpackUnitVec(source.normal, vertex.normal);
                    if (!std::isfinite(vertex.textureCoordinate[0]) ||
                        !std::isfinite(vertex.textureCoordinate[1]) ||
                        !Finite3(vertex.normal))
                    {
                        return WebRendererStaticModelSceneResult::InvalidModel;
                    }
                    replacement.vertices.push_back(vertex);
                }
                const std::uint32_t firstIndex =
                    static_cast<std::uint32_t>(replacement.indices.size());
                for (std::uint32_t index = 0u; index < indexCount; ++index)
                {
                    const std::uint32_t localIndex = surface.triIndices[index];
                    if (localIndex >= surface.vertCount)
                        return WebRendererStaticModelSceneResult::IndexOutOfRange;
                    replacement.indices.push_back(vertexBase + localIndex);
                }
                WebRendererStaticModelBatchDesc batch{};
                batch.draw = MakeDraw(
                    model,
                    model.materialHandles[modelSurfaceIndex],
                    modelSurfaceIndex,
                    firstIndex,
                    indexCount,
                    group.instanceIndices.front(),
                    group.instanceIndices.back());
                batch.instanceOffset = instanceOffset;
                batch.instanceCount = static_cast<std::uint32_t>(
                    group.instanceIndices.size());
                replacement.batches.push_back(batch);
                ++replacement.surfaceCount;
                modelSubmitted = true;
            }
            if (modelSubmitted)
                ++replacement.modelCount;
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererStaticModelSceneResult::AllocationFailed;
    }

    if (replacement.batches.empty())
        return WebRendererStaticModelSceneResult::NoStaticModels;
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
