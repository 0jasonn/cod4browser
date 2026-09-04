#include <web/web_renderer_particle_cloud_scene.h>

#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_particle_cloud.h>
#include <web/web_renderer_code_mesh.h>

#include <qcommon/qcommon_math.h>
#include <universal/com_random.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

namespace
{
constexpr float BYTE_TO_UNIT = 1.0f / 255.0f;
constexpr float EPSILON = 0.001f;
constexpr std::uint32_t TECHNIQUE_EMISSIVE_INDEX = 5u;
std::array<std::array<float, 3u>, WEB_RENDERER_PARTICLE_CLOUD_PARTICLES>
    g_particleCloudLayout{};
bool g_particleCloudLayoutReady = false;

void GenerateParticleCloudLayout() noexcept
{
    for (std::uint32_t x = 0u; x < 8u; ++x)
    {
        for (std::uint32_t y = 0u; y < 8u; ++y)
        {
            for (std::uint32_t z = 0u; z < 16u; ++z)
            {
                const std::uint32_t particleId = z + (x << 7u) + 16u * y;
                std::array<float, 3u> &position =
                    g_particleCloudLayout[particleId];
                const double random[3] = {
                    Q_RandomToInclusiveUnitDouble(
                        static_cast<std::uint32_t>(std::rand()), RAND_MAX),
                    Q_RandomToInclusiveUnitDouble(
                        static_cast<std::uint32_t>(std::rand()), RAND_MAX),
                    Q_RandomToInclusiveUnitDouble(
                        static_cast<std::uint32_t>(std::rand()), RAND_MAX),
                };
                R_CreateParticleCloudCellPosition(
                    static_cast<int>(x), static_cast<int>(y),
                    static_cast<int>(z), random, position.data());
            }
        }
    }
    g_particleCloudLayoutReady = true;
}

bool Finite3(const float value[3]) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}

bool FiniteAxis(const float axis[3][3]) noexcept
{
    for (std::size_t row = 0u; row < 3u; ++row)
        if (!Finite3(axis[row])) return false;
    return true;
}

bool Normalize3(float value[3]) noexcept
{
    const float lengthSquared = value[0] * value[0] +
        value[1] * value[1] + value[2] * value[2];
    if (!std::isfinite(lengthSquared) || lengthSquared <= EPSILON * EPSILON)
        return false;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    value[0] *= inverseLength;
    value[1] *= inverseLength;
    value[2] *= inverseLength;
    return Finite3(value);
}

bool PlacementIsValid(const GfxScaledPlacement &placement) noexcept
{
    if (!Finite3(placement.base.origin) || !std::isfinite(placement.scale) ||
        placement.scale <= 0.0f)
        return false;
    float lengthSquared = 0.0f;
    for (const float component : placement.base.quat)
    {
        if (!std::isfinite(component)) return false;
        lengthSquared += component * component;
    }
    return std::isfinite(lengthSquared) &&
        std::fabs(lengthSquared - 1.0f) <= 0.002f;
}

float Dot3(const float left[3], const float right[3]) noexcept
{
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

void Scale3(const float value[3], float scale, float out[3]) noexcept
{
    out[0] = value[0] * scale;
    out[1] = value[1] * scale;
    out[2] = value[2] * scale;
}

void AddScaled3(float out[3], const float value[3], float scale) noexcept
{
    out[0] += value[0] * scale;
    out[1] += value[1] * scale;
    out[2] += value[2] * scale;
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
    // Native R_AddParticleCloudDrawSurf gates this family on the active
    // gfxDrawMethod.emissiveTechType. The current web compatibility contract
    // maps that canonical slot to technique index 5; accepting lit/unlit here
    // would make a cloud visible that native Kisak would reject before draw.
    const MaterialTechnique *technique =
        material->techniqueSet->techniques[TECHNIQUE_EMISSIVE_INDEX];
    const std::uint8_t entry =
        material->stateBitsEntry[TECHNIQUE_EMISSIVE_INDEX];
    if (!technique || technique->passCount == 0u || entry == 0xffu ||
        entry >= material->stateBitsCount)
        return false;
    stateBits[0] = material->stateBitsTable[entry].loadBits[0];
    stateBits[1] = material->stateBitsTable[entry].loadBits[1];
    return true;
}

WebRendererWorldBatchDesc MakeDraw(Material *material) noexcept
{
    WebRendererWorldBatchDesc draw{};
    draw.surfaceCount = 1u;
    draw.firstSurfaceIndex = 0u;
    draw.lastSurfaceIndex = 0u;
    draw.materialIdentity = material;
    draw.materialName = material && material->info.name
        ? material->info.name : "<null-material>";
    draw.firstInstanceIndex = UINT32_MAX;
    draw.lastInstanceIndex = UINT32_MAX;
    draw.lightmapIndex = 31u;
    draw.sourceKind = WebRendererSceneBatchKind::FxParticleCloud;
    draw.baseImage = FindBaseImage(material, draw.samplerState);
    const bool hasTechnique = SelectTechnique(material, draw.stateBits);
    if (!hasTechnique)
    {
        draw.stateBits[0] = WEB_RENDERER_FX_FALLBACK_STATE_BITS0;
        draw.stateBits[1] = WEB_RENDERER_FX_FALLBACK_STATE_BITS1;
    }
    draw.technique = draw.baseImage && hasTechnique
        ? WebRendererWorldTechnique::BaseTexture
        : WebRendererWorldTechnique::BackendFallback;
    return draw;
}

bool BuildCloudAxes(
    const GfxParticleCloud &cloud,
    const WebRendererParticleCloudView &view,
    float axis0[3], float axis1[3]) noexcept
{
    if (!Finite3(view.origin) || !FiniteAxis(view.axis) ||
        !std::isfinite(cloud.radius[0]) || !std::isfinite(cloud.radius[1]) ||
        cloud.radius[0] < 0.0f || cloud.radius[1] < 0.0f)
        return false;

    // MatrixForViewer uses -refdef.axis[1] for view X and axis[2] for view Y.
    float cameraRight[3] = {-view.axis[1][0], -view.axis[1][1], -view.axis[1][2]};
    float cameraUp[3] = {view.axis[2][0], view.axis[2][1], view.axis[2][2]};
    if (!Normalize3(cameraRight) || !Normalize3(cameraUp)) return false;

    float direction[3] = {
        cloud.endpos[0] - cloud.placement.base.origin[0],
        cloud.endpos[1] - cloud.placement.base.origin[1],
        cloud.endpos[2] - cloud.placement.base.origin[2],
    };
    // R_SetParticleCloudConstants compares unnormalized components, then
    // projects the radius-scaled direction. The minimum length and signed
    // quadrant fallback belong to the shared native calculation.
    float viewUp[3]{};
    if (cloud.radius[0] != cloud.radius[1] &&
        (direction[0] * direction[0] > EPSILON * EPSILON ||
         direction[1] * direction[1] > EPSILON * EPSILON ||
         direction[2] * direction[2] > EPSILON * EPSILON))
    {
        if (!Normalize3(direction)) return false;
        Scale3(direction, cloud.radius[1], direction);
        viewUp[0] = Dot3(direction, cameraRight);
        viewUp[1] = Dot3(direction, cameraUp);
        if (!Finite3(viewUp)) return false;
    }
    float viewAxis[2][2]{};
    R_CreateParticleCloud2dAxis(cloud.radius, viewUp, viewAxis);
    for (std::size_t component = 0u; component < 3u; ++component)
    {
        axis0[component] = viewAxis[0][0] * cameraRight[component] +
            viewAxis[0][1] * cameraUp[component];
        axis1[component] = viewAxis[1][0] * cameraRight[component] +
            viewAxis[1][1] * cameraUp[component];
    }
    return Finite3(axis0) && Finite3(axis1);
}

WebRendererParticleCloudSceneResult BuildOne(
    const WebRendererParticleCloudSubmission &submission,
    const WebRendererParticleCloudView &view,
    WebRendererParticleCloudSceneCommand &destination)
{
    const GfxParticleCloud &cloud = submission.cloud;
    if (!submission.material || !PlacementIsValid(cloud.placement) ||
        !Finite3(cloud.endpos) || !Finite3(view.origin) ||
        !FiniteAxis(view.axis))
        return WebRendererParticleCloudSceneResult::InvalidSubmission;
    std::uint32_t emissiveStateBits[2]{};
    if (!SelectTechnique(submission.material, emissiveStateBits))
        return WebRendererParticleCloudSceneResult::InvalidSubmission;

    float placementAxis[3][3]{};
    Q_UnitQuatToAxis(cloud.placement.base.quat, placementAxis);
    float cloudAxis0[3]{};
    float cloudAxis1[3]{};
    if (!BuildCloudAxes(cloud, view, cloudAxis0, cloudAxis1))
        return WebRendererParticleCloudSceneResult::InvalidSubmission;

    if (destination.vertices.size() >
            WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
                WEB_RENDERER_PARTICLE_CLOUD_VERTICES ||
        destination.indices.size() > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
            WEB_RENDERER_PARTICLE_CLOUD_INDICES ||
        destination.batches.size() > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES - 1u)
        return WebRendererParticleCloudSceneResult::OutputTooLarge;

    const std::size_t vertexStart = destination.vertices.size();
    const std::size_t indexStart = destination.indices.size();
    const std::size_t batchStart = destination.batches.size();
    const std::uint32_t cloudStart = destination.cloudCount;
    const std::uint32_t surfaceStart = destination.surfaceCount;
    const auto rollback = [&]() {
        destination.vertices.resize(vertexStart);
        destination.indices.resize(indexStart);
        destination.batches.resize(batchStart);
        destination.cloudCount = cloudStart;
        destination.surfaceCount = surfaceStart;
    };
    try
    {
        if (!g_particleCloudLayoutReady) GenerateParticleCloudLayout();
        destination.vertices.reserve(vertexStart +
            WEB_RENDERER_PARTICLE_CLOUD_VERTICES);
        destination.indices.reserve(indexStart +
            WEB_RENDERER_PARTICLE_CLOUD_INDICES);
        destination.batches.reserve(batchStart + 1u);
        const float color[4] = {
            static_cast<float>((cloud.color.packed >> 16u) & 0xffu) *
                BYTE_TO_UNIT,
            static_cast<float>((cloud.color.packed >> 8u) & 0xffu) *
                BYTE_TO_UNIT,
            static_cast<float>(cloud.color.packed & 0xffu) * BYTE_TO_UNIT,
            static_cast<float>((cloud.color.packed >> 24u) & 0xffu) *
                BYTE_TO_UNIT,
        };
        const std::uint32_t vertexBase =
            static_cast<std::uint32_t>(vertexStart);
        const std::uint32_t indexBase = static_cast<std::uint32_t>(indexStart);
        const float texcoords[4][2] = {
            {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
        const std::uint32_t quadIndices[6] = {0u, 1u, 2u, 2u, 1u, 3u};
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            for (std::uint32_t y = 0u; y < 8u; ++y)
            {
                for (std::uint32_t z = 0u; z < 16u; ++z)
                {
                    const std::uint32_t particleId = z + (x << 7u) +
                        16u * y;
                    const std::array<float, 3u> &local =
                        g_particleCloudLayout[particleId];
                    float center[3] = {
                        cloud.placement.base.origin[0],
                        cloud.placement.base.origin[1],
                        cloud.placement.base.origin[2],
                    };
                    for (std::size_t row = 0u; row < 3u; ++row)
                    {
                        const float transformed = placementAxis[0][row] *
                                local[0] + placementAxis[1][row] * local[1] +
                            placementAxis[2][row] * local[2];
                        center[row] += transformed * cloud.placement.scale;
                    }
                    for (std::uint32_t corner = 0u; corner < 4u; ++corner)
                    {
                        WebRendererSurfaceVertex vertex{};
                        vertex.position[0] = center[0];
                        vertex.position[1] = center[1];
                        vertex.position[2] = center[2];
                        AddScaled3(vertex.position, cloudAxis0,
                            texcoords[corner][0] - 0.5f);
                        AddScaled3(vertex.position, cloudAxis1,
                            texcoords[corner][1] - 0.5f);
                        std::copy_n(color, 4u, vertex.color);
                        vertex.textureCoordinate[0] = texcoords[corner][0];
                        vertex.textureCoordinate[1] = texcoords[corner][1];
                        // The native outdoor vertex shader samples its lookup
                        // at the particle center, before billboard expansion.
                        // Particle clouds do not use surface normals, so keep
                        // that center in the existing portable normal slot.
                        std::copy_n(center, 3u, vertex.normal);
                        if (!Finite3(vertex.position))
                        {
                            rollback();
                            return WebRendererParticleCloudSceneResult::
                                InvalidSubmission;
                        }
                        destination.vertices.push_back(vertex);
                    }
                    for (const std::uint32_t index : quadIndices)
                        destination.indices.push_back(vertexBase +
                            particleId * 4u + index);
                }
            }
        }
        WebRendererWorldBatchDesc batch = MakeDraw(submission.material);
        batch.firstIndex = indexBase;
        batch.indexCount = WEB_RENDERER_PARTICLE_CLOUD_INDICES;
        destination.batches.push_back(batch);
        ++destination.cloudCount;
        ++destination.surfaceCount;
        return WebRendererParticleCloudSceneResult::Success;
    }
    catch (const std::bad_alloc &)
    {
        rollback();
        return WebRendererParticleCloudSceneResult::AllocationFailed;
    }
}
} // namespace

void WebRenderer_InitializeParticleCloudLayout() noexcept
{
    GenerateParticleCloudLayout();
}

WebRendererParticleCloudRetainResult
WebRenderer_RetainParticleCloudSubmission(
    WebRendererParticleCloudSubmission *storage,
    std::uint32_t *count,
    Material *material,
    GfxParticleCloud **cloudOut) noexcept
{
    if (cloudOut) *cloudOut = nullptr;
    if (!storage || !count || !cloudOut || !material)
        return WebRendererParticleCloudRetainResult::InvalidSubmission;
    if (*count >= WEB_RENDERER_MAX_PARTICLE_CLOUD_SUBMISSIONS)
        return WebRendererParticleCloudRetainResult::LimitReached;
    WebRendererParticleCloudSubmission &submission = storage[(*count)++];
    submission = {};
    submission.material = material;
    *cloudOut = &submission.cloud;
    return WebRendererParticleCloudRetainResult::Accepted;
}

void WebRenderer_ClearParticleCloudSubmissions(std::uint32_t *count) noexcept
{
    if (count) *count = 0u;
}

WebRendererParticleCloudSceneResult WebRenderer_BuildParticleCloudCommand(
    const WebRendererParticleCloudSubmission &submission,
    const WebRendererParticleCloudView &view,
    WebRendererParticleCloudSceneCommand &destination)
{
    WebRendererParticleCloudSceneCommand replacement;
    const WebRendererParticleCloudSceneResult result =
        BuildOne(submission, view, replacement);
    if (result == WebRendererParticleCloudSceneResult::Success)
        destination = std::move(replacement);
    return result;
}

WebRendererParticleCloudSceneResult
WebRenderer_BuildParticleCloudSceneCommand(
    const WebRendererParticleCloudSubmission *submissions,
    std::uint32_t submissionCount,
    const WebRendererParticleCloudView &view,
    WebRendererParticleCloudSceneCommand &destination,
    std::uint32_t *droppedCount)
{
    if (droppedCount) *droppedCount = 0u;
    if (submissionCount == 0u) return WebRendererParticleCloudSceneResult::NoCloud;
    if (!submissions) return WebRendererParticleCloudSceneResult::InvalidSubmission;
    WebRendererParticleCloudSceneCommand replacement;
    try
    {
        for (std::uint32_t index = 0u; index < submissionCount; ++index)
        {
            WebRendererParticleCloudSceneCommand one;
            const WebRendererParticleCloudSceneResult result = BuildOne(
                submissions[index], view, one);
            if (result == WebRendererParticleCloudSceneResult::Success)
            {
                const WebRendererParticleCloudAppendResult append =
                    WebRenderer_AppendParticleCloudCommand(
                        one, replacement.vertices, replacement.indices,
                        replacement.batches, replacement.surfaceCount);
                if (append == WebRendererParticleCloudAppendResult::Success)
                {
                    ++replacement.cloudCount;
                    continue;
                }
            }
            if (droppedCount && *droppedCount != UINT32_MAX) ++*droppedCount;
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererParticleCloudSceneResult::AllocationFailed;
    }
    if (replacement.batches.empty())
        return WebRendererParticleCloudSceneResult::NoCloud;
    destination = std::move(replacement);
    return WebRendererParticleCloudSceneResult::Success;
}

WebRendererParticleCloudAppendResult WebRenderer_AppendParticleCloudCommand(
    const WebRendererParticleCloudSceneCommand &source,
    std::vector<WebRendererSurfaceVertex> &vertices,
    std::vector<std::uint32_t> &indices,
    std::vector<WebRendererWorldBatchDesc> &batches,
    std::uint32_t &surfaceCount)
{
    if (source.vertices.size() != WEB_RENDERER_PARTICLE_CLOUD_VERTICES ||
        source.indices.size() != WEB_RENDERER_PARTICLE_CLOUD_INDICES ||
        source.batches.size() != 1u || source.cloudCount != 1u ||
        source.surfaceCount != 1u)
        return WebRendererParticleCloudAppendResult::InvalidCommand;
    for (const std::uint32_t index : source.indices)
        if (index >= source.vertices.size())
            return WebRendererParticleCloudAppendResult::InvalidCommand;
    const WebRendererWorldBatchDesc &batch = source.batches[0];
    if (batch.firstIndex > source.indices.size() ||
        batch.indexCount > source.indices.size() - batch.firstIndex)
        return WebRendererParticleCloudAppendResult::InvalidCommand;
    const WebRendererParticleCloudAppendResult countResult =
        WebRenderer_ValidateParticleCloudAppendCounts(
            vertices.size(), indices.size(), batches.size(), surfaceCount);
    if (countResult != WebRendererParticleCloudAppendResult::Success)
        return countResult;

    const std::size_t originalVertices = vertices.size();
    const std::size_t originalIndices = indices.size();
    const std::size_t originalBatches = batches.size();
    const std::uint32_t originalSurfaces = surfaceCount;
    try
    {
        // Let vector insertion grow capacity geometrically. Exact reserves
        // for every cloud repeatedly copy all previously assembled geometry.
        // The rollback below still preserves the command on allocation failure.
        const std::uint32_t vertexBase =
            static_cast<std::uint32_t>(originalVertices);
        const std::uint32_t indexBase =
            static_cast<std::uint32_t>(originalIndices);
        vertices.insert(vertices.end(), source.vertices.begin(),
            source.vertices.end());
        for (const std::uint32_t index : source.indices)
            indices.push_back(vertexBase + index);
        WebRendererWorldBatchDesc retained = batch;
        retained.firstIndex += indexBase;
        batches.push_back(retained);
        ++surfaceCount;
        return WebRendererParticleCloudAppendResult::Success;
    }
    catch (const std::bad_alloc &)
    {
        vertices.resize(originalVertices);
        indices.resize(originalIndices);
        batches.resize(originalBatches);
        surfaceCount = originalSurfaces;
        return WebRendererParticleCloudAppendResult::AllocationFailed;
    }
}

WebRendererParticleCloudAppendResult
WebRenderer_ValidateParticleCloudAppendCounts(
    std::size_t destinationVertexCount,
    std::size_t destinationIndexCount,
    std::size_t destinationBatchCount,
    std::uint32_t destinationSurfaceCount) noexcept
{
    if (destinationVertexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
            WEB_RENDERER_PARTICLE_CLOUD_VERTICES ||
        destinationIndexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
            WEB_RENDERER_PARTICLE_CLOUD_INDICES ||
        destinationBatchCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES - 1u ||
        destinationSurfaceCount == UINT32_MAX)
        return WebRendererParticleCloudAppendResult::OutputTooLarge;
    return WebRendererParticleCloudAppendResult::Success;
}
