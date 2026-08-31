#include <web/web_renderer_particle_cloud_scene.h>
#include <gfx_d3d/gfx_image_types.h>
#include <gfx_d3d/material_types.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace
{
int allocationsUntilFailure = -1;
}

void *operator new(std::size_t size)
{
    if (allocationsUntilFailure == 0) throw std::bad_alloc{};
    if (allocationsUntilFailure > 0) --allocationsUntilFailure;
    if (void *memory = std::malloc(size ? size : 1u)) return memory;
    throw std::bad_alloc{};
}

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }

namespace
{
constexpr std::uint32_t TECHNIQUE_EMISSIVE_INDEX = 5u;
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;

struct Fixture
{
    Material material{};
    GfxImage image{};
    MaterialTextureDef texture{};
    MaterialTechnique technique{};
    MaterialTechniqueSet techniqueSet{};
    GfxStateBits stateBits[1]{};
    WebRendererParticleCloudSubmission submission{};

    Fixture()
    {
        image.name = "particle-cloud-image";
        texture.semantic = 2u;
        texture.samplerState = 0x42u;
        texture.u.image = &image;
        technique.passCount = 1u;
        techniqueSet.techniques[TECHNIQUE_EMISSIVE_INDEX] = &technique;
        techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &technique;
        stateBits[0].loadBits[0] = 0x18008800u;
        stateBits[0].loadBits[1] = 0x0000000du;
        material.textureCount = 1u;
        material.textureTable = &texture;
        material.techniqueSet = &techniqueSet;
        material.stateBitsEntry[TECHNIQUE_EMISSIVE_INDEX] = 0u;
        material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
        material.stateBitsCount = 1u;
        material.stateBitsTable = stateBits;
        submission.material = &material;
        submission.cloud.placement.base.quat[3] = 1.0f;
        submission.cloud.placement.scale = 1.0f;
        submission.cloud.color.packed = 0x80402010u;
        submission.cloud.radius[0] = 2.0f;
        submission.cloud.radius[1] = 3.0f;
    }
};

WebRendererParticleCloudView IdentityView()
{
    WebRendererParticleCloudView view{};
    view.axis[0][0] = 1.0f;
    view.axis[1][1] = 1.0f;
    view.axis[2][2] = 1.0f;
    return view;
}

void TestRetentionCopySlotsAndClear()
{
    Fixture fixture;
    std::array<WebRendererParticleCloudSubmission,
        WEB_RENDERER_MAX_PARTICLE_CLOUD_SUBMISSIONS> storage{};
    std::uint32_t count = 0u;
    GfxParticleCloud *first = nullptr;
    GfxParticleCloud *second = nullptr;
    assert(WebRenderer_RetainParticleCloudSubmission(storage.data(), &count,
        &fixture.material, &first) ==
        WebRendererParticleCloudRetainResult::Accepted);
    assert(WebRenderer_RetainParticleCloudSubmission(storage.data(), &count,
        &fixture.material, &second) ==
        WebRendererParticleCloudRetainResult::Accepted);
    assert(count == 2u && first != second);
    first->placement.base.origin[0] = 7.0f;
    second->placement.base.origin[0] = 9.0f;
    assert(storage[0].cloud.placement.base.origin[0] == 7.0f);
    assert(storage[1].cloud.placement.base.origin[0] == 9.0f);
    count = WEB_RENDERER_MAX_PARTICLE_CLOUD_SUBMISSIONS;
    GfxParticleCloud *overflow = nullptr;
    assert(WebRenderer_RetainParticleCloudSubmission(storage.data(), &count,
        &fixture.material, &overflow) ==
        WebRendererParticleCloudRetainResult::LimitReached);
    assert(overflow == nullptr &&
        count == WEB_RENDERER_MAX_PARTICLE_CLOUD_SUBMISSIONS);
    WebRenderer_ClearParticleCloudSubmissions(&count);
    assert(count == 0u);
}

void TestDeterministicLayoutAndMaterialData()
{
    Fixture fixture;
    fixture.submission.cloud.placement.base.origin[0] = 10.0f;
    fixture.submission.cloud.placement.base.origin[1] = 20.0f;
    fixture.submission.cloud.placement.base.origin[2] = 30.0f;
    fixture.submission.cloud.endpos[0] = 10.0f;
    fixture.submission.cloud.endpos[1] = 20.0f;
    fixture.submission.cloud.endpos[2] = 30.0f;
    const WebRendererParticleCloudView view = IdentityView();
    WebRendererParticleCloudSceneCommand first;
    WebRendererParticleCloudSceneCommand second;
    assert(WebRenderer_BuildParticleCloudCommand(fixture.submission, view,
        first) == WebRendererParticleCloudSceneResult::Success);
    assert(WebRenderer_BuildParticleCloudCommand(fixture.submission, view,
        second) == WebRendererParticleCloudSceneResult::Success);
    assert(first.cloudCount == 1u && first.surfaceCount == 1u);
    assert(first.vertices.size() == WEB_RENDERER_PARTICLE_CLOUD_VERTICES);
    assert(first.indices.size() == WEB_RENDERER_PARTICLE_CLOUD_INDICES);
    assert(first.batches.size() == 1u);
    assert(first.indices[0] == 0u && first.indices[1] == 1u &&
        first.indices[2] == 2u && first.indices[3] == 2u &&
        first.indices[4] == 1u && first.indices[5] == 3u);
    assert(first.indices.back() == WEB_RENDERER_PARTICLE_CLOUD_VERTICES - 1u);
    assert(first.batches[0].firstIndex == 0u);
    assert(first.batches[0].indexCount == WEB_RENDERER_PARTICLE_CLOUD_INDICES);
    assert(first.batches[0].sourceKind ==
        WebRendererSceneBatchKind::FxParticleCloud);
    assert(first.batches[0].materialIdentity == &fixture.material);
    assert(first.batches[0].baseImage == &fixture.image);
    assert(first.batches[0].samplerState == 0x42u);
    assert(first.batches[0].stateBits[0] == 0x18008800u);
    assert(first.batches[0].technique == WebRendererWorldTechnique::BaseTexture);
    fixture.material.textureTable = nullptr;
    fixture.material.textureCount = 0u;
    WebRendererParticleCloudSceneCommand fallback;
    assert(WebRenderer_BuildParticleCloudCommand(fixture.submission, view,
        fallback) == WebRendererParticleCloudSceneResult::Success);
    assert(fallback.batches[0].baseImage == nullptr);
    assert(fallback.batches[0].sourceKind ==
        WebRendererSceneBatchKind::FxParticleCloud);
    assert(fallback.batches[0].technique ==
        WebRendererWorldTechnique::BackendFallback);
    assert(WebRenderer_IsFxVertexColorBatch(
        fallback.batches[0].sourceKind));
    assert(first.vertices[0].color[0] == 0x40 / 255.0f);
    assert(first.vertices[0].color[1] == 0x20 / 255.0f);
    assert(first.vertices[0].color[2] == 0x10 / 255.0f);
    assert(first.vertices[0].color[3] == 0x80 / 255.0f);
    assert(first.vertices[0].textureCoordinate[0] == 0.0f);
    assert(first.vertices[0].textureCoordinate[1] == 0.0f);
    assert(first.vertices[1].textureCoordinate[1] == 1.0f);
    assert(first.vertices[2].textureCoordinate[0] == 1.0f);
    for (const WebRendererSurfaceVertex &vertex : first.vertices)
        for (const float component : vertex.position)
            assert(std::isfinite(component));
    assert(first.vertices.size() == second.vertices.size());
    assert(std::memcmp(first.vertices.data(), second.vertices.data(),
        first.vertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
    assert(first.indices == second.indices);

    WebRendererParticleCloudSceneCommand preserved = first;
    fixture.techniqueSet.techniques[TECHNIQUE_EMISSIVE_INDEX] = nullptr;
    assert(WebRenderer_BuildParticleCloudCommand(fixture.submission, view,
        preserved) == WebRendererParticleCloudSceneResult::InvalidSubmission);
    assert(preserved.cloudCount == first.cloudCount);
    assert(preserved.surfaceCount == first.surfaceCount);
    assert(preserved.vertices.size() == first.vertices.size());
    assert(preserved.indices == first.indices);
    fixture.techniqueSet.techniques[TECHNIQUE_EMISSIVE_INDEX] =
        &fixture.technique;
}

void TestDirectedAxisAndMultiCloudOrdering()
{
    Fixture firstFixture;
    Fixture secondFixture;
    firstFixture.submission.cloud.radius[0] = 1.0f;
    firstFixture.submission.cloud.radius[1] = 4.0f;
    firstFixture.submission.cloud.endpos[0] = 0.0f;
    firstFixture.submission.cloud.endpos[1] = 1.0f;
    firstFixture.submission.cloud.radius[1] = 1.0f;
    WebRendererParticleCloudSceneCommand equalRadius;
    assert(WebRenderer_BuildParticleCloudCommand(firstFixture.submission,
        IdentityView(), equalRadius) ==
        WebRendererParticleCloudSceneResult::Success);
    firstFixture.submission.cloud.radius[1] = 4.0f;
    secondFixture.submission.cloud.placement.base.origin[0] = 9.0f;
    WebRendererParticleCloudSubmission submissions[2] = {
        firstFixture.submission, secondFixture.submission};
    const WebRendererParticleCloudView view = IdentityView();
    WebRendererParticleCloudSceneCommand command;
    std::uint32_t dropped = 0u;
    assert(WebRenderer_BuildParticleCloudSceneCommand(submissions, 2u, view,
        command, &dropped) == WebRendererParticleCloudSceneResult::Success);
    assert(dropped == 0u && command.cloudCount == 2u);
    assert(command.vertices.size() == 2u * WEB_RENDERER_PARTICLE_CLOUD_VERTICES);
    assert(command.indices.size() == 2u * WEB_RENDERER_PARTICLE_CLOUD_INDICES);
    assert(command.batches.size() == 2u);
    assert(command.batches[0].firstIndex == 0u);
    assert(command.batches[1].firstIndex == WEB_RENDERER_PARTICLE_CLOUD_INDICES);
    assert(command.indices[WEB_RENDERER_PARTICLE_CLOUD_INDICES] ==
        WEB_RENDERER_PARTICLE_CLOUD_VERTICES);
    assert(command.batches[1].sourceKind ==
        WebRendererSceneBatchKind::FxParticleCloud);
    assert(command.vertices[0].position[0] !=
        command.vertices[1u * WEB_RENDERER_PARTICLE_CLOUD_VERTICES].position[0]);

    const auto ReconstructAxis = [](const WebRendererSurfaceVertex &first,
        const WebRendererSurfaceVertex &second, float out[3]) {
        for (std::size_t component = 0u; component < 3u; ++component)
            out[component] = (second.position[component] -
                first.position[component]) * 0.5f;
    };
    float directedAxis0[3]{};
    float directedAxis1[3]{};
    ReconstructAxis(command.vertices[0], command.vertices[2], directedAxis0);
    ReconstructAxis(command.vertices[0], command.vertices[1], directedAxis1);
    assert(std::fabs(directedAxis0[0]) < 0.00001f);
    assert(std::fabs(directedAxis0[1]) < 0.00001f);
    assert(std::fabs(directedAxis0[2] + 1.0f) < 0.00001f);
    assert(std::fabs(directedAxis1[0]) < 0.00001f);
    assert(std::fabs(directedAxis1[1] - 4.0f) < 0.00001f);
    assert(std::fabs(directedAxis1[2]) < 0.00001f);

    float equalAxis0[3]{};
    float equalAxis1[3]{};
    ReconstructAxis(equalRadius.vertices[0], equalRadius.vertices[2],
        equalAxis0);
    ReconstructAxis(equalRadius.vertices[0], equalRadius.vertices[1],
        equalAxis1);
    assert(std::fabs(equalAxis0[0]) < 0.00001f);
    assert(std::fabs(equalAxis0[1] - 1.0f) < 0.00001f);
    assert(std::fabs(equalAxis0[2]) < 0.00001f);
    assert(std::fabs(equalAxis1[0]) < 0.00001f);
    assert(std::fabs(equalAxis1[1]) < 0.00001f);
    assert(std::fabs(equalAxis1[2] - 1.0f) < 0.00001f);

}

void TestFailureAtomicAppendAndCapacity()
{
    Fixture fixture;
    const WebRendererParticleCloudView view = IdentityView();
    WebRendererParticleCloudSceneCommand source;
    assert(WebRenderer_BuildParticleCloudCommand(fixture.submission, view,
        source) == WebRendererParticleCloudSceneResult::Success);

    std::vector<WebRendererSurfaceVertex> vertices(3u);
    std::vector<std::uint32_t> indices = {0u, 1u, 2u, 2u, 1u, 0u};
    std::vector<WebRendererWorldBatchDesc> batches(3u);
    batches[0].sourceKind = WebRendererSceneBatchKind::DynamicDObj;
    batches[1].sourceKind = WebRendererSceneBatchKind::FxXModel;
    batches[2].sourceKind = WebRendererSceneBatchKind::FxCodeMesh;
    std::uint32_t surfaceCount = 3u;
    const auto result = WebRenderer_AppendParticleCloudCommand(source,
        vertices, indices, batches, surfaceCount);
    assert(result == WebRendererParticleCloudAppendResult::Success);
    assert(vertices.size() == 3u + WEB_RENDERER_PARTICLE_CLOUD_VERTICES);
    assert(indices[6] == 3u);
    assert(batches.size() == 4u);
    assert(surfaceCount == 4u);
    assert(batches[3].firstIndex == 6u);
    assert(batches[3].sourceKind == WebRendererSceneBatchKind::FxParticleCloud);

    WebRendererParticleCloudSceneCommand invalid = source;
    invalid.indices[0] = static_cast<std::uint32_t>(invalid.vertices.size());
    const std::size_t beforeInvalidVertices = vertices.size();
    const std::size_t beforeInvalidIndices = indices.size();
    const std::size_t beforeInvalidBatches = batches.size();
    assert(WebRenderer_AppendParticleCloudCommand(invalid, vertices, indices,
        batches, surfaceCount) ==
        WebRendererParticleCloudAppendResult::InvalidCommand);
    assert(vertices.size() == beforeInvalidVertices);
    assert(indices.size() == beforeInvalidIndices);
    assert(batches.size() == beforeInvalidBatches);
    assert(surfaceCount == 4u);

    surfaceCount = 4u;
    const auto tooLarge = WebRenderer_ValidateParticleCloudAppendCounts(
        WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES - 1u,
        indices.size(), batches.size(), surfaceCount);
    assert(tooLarge == WebRendererParticleCloudAppendResult::OutputTooLarge);
    assert(vertices.size() == 3u + WEB_RENDERER_PARTICLE_CLOUD_VERTICES);
    assert(indices.size() == 6u + WEB_RENDERER_PARTICLE_CLOUD_INDICES);
    assert(batches.size() == 4u);
    assert(surfaceCount == 4u);
}

void TestRepeatedAppendAndAllocationRollback()
{
    Fixture fixture;
    WebRendererParticleCloudSceneCommand source;
    assert(WebRenderer_BuildParticleCloudCommand(fixture.submission,
        IdentityView(), source) == WebRendererParticleCloudSceneResult::Success);
    const std::vector<WebRendererSurfaceVertex> prefixVertices(3u);
    const std::vector<std::uint32_t> prefixIndices = {0u, 1u, 2u, 2u, 1u, 0u};
    std::vector<WebRendererSurfaceVertex> vertices = prefixVertices;
    std::vector<std::uint32_t> indices = prefixIndices;
    std::vector<WebRendererWorldBatchDesc> batches(1u);
    batches[0].sourceKind = WebRendererSceneBatchKind::DynamicDObj;
    std::uint32_t surfaceCount = 1u;
    for (std::uint32_t cloud = 0u; cloud < 24u; ++cloud)
    {
        assert(WebRenderer_AppendParticleCloudCommand(source, vertices,
            indices, batches, surfaceCount) ==
            WebRendererParticleCloudAppendResult::Success);
        assert(surfaceCount == cloud + 2u);
        assert(batches.size() == cloud + 2u);
        assert(vertices.size() == 3u + (cloud + 1u) * source.vertices.size());
        assert(indices.size() == 6u + (cloud + 1u) * source.indices.size());
    }
    assert(std::memcmp(vertices.data(), prefixVertices.data(),
        prefixVertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
    for (std::size_t index = 0u; index < prefixIndices.size(); ++index)
        assert(indices[index] == prefixIndices[index]);
    assert(batches[0].sourceKind == WebRendererSceneBatchKind::DynamicDObj);
    for (std::uint32_t cloud = 0u; cloud < 24u; ++cloud)
    {
        const std::size_t vertexBase = 3u + cloud * source.vertices.size();
        const std::size_t indexBase = 6u + cloud * source.indices.size();
        assert(std::memcmp(vertices.data() + vertexBase, source.vertices.data(),
            source.vertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
        for (std::size_t index = 0u; index < source.indices.size(); ++index)
            assert(indices[indexBase + index] == vertexBase + source.indices[index]);
        assert(batches[cloud + 1u].firstIndex == indexBase);
        assert(batches[cloud + 1u].indexCount == source.batches[0].indexCount);
        assert(batches[cloud + 1u].sourceKind == WebRendererSceneBatchKind::FxParticleCloud);
        assert(batches[cloud + 1u].baseImage == source.batches[0].baseImage);
        assert(batches[cloud + 1u].castsSunShadow == source.batches[0].castsSunShadow);
    }

    // Fail each successive allocation until an append succeeds, covering
    // failures after earlier vectors have already grown or received elements.
    bool completed = false;
    for (int failAfter = 0; failAfter < 32; ++failAfter)
    {
        auto failureVertices = prefixVertices;
        auto failureIndices = prefixIndices;
        std::vector<WebRendererWorldBatchDesc> failureBatches(1u);
        failureBatches[0].sourceKind = WebRendererSceneBatchKind::DynamicDObj;
        std::uint32_t failureSurfaces = 1u;
        allocationsUntilFailure = failAfter;
        const auto result = WebRenderer_AppendParticleCloudCommand(source,
            failureVertices, failureIndices, failureBatches, failureSurfaces);
        allocationsUntilFailure = -1;
        if (result == WebRendererParticleCloudAppendResult::Success)
        {
            completed = true;
            break;
        }
        assert(result == WebRendererParticleCloudAppendResult::AllocationFailed);
        assert(failureVertices.size() == prefixVertices.size());
        assert(std::memcmp(failureVertices.data(), prefixVertices.data(),
            prefixVertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
        assert(failureIndices == prefixIndices);
        assert(failureBatches.size() == 1u && failureSurfaces == 1u);
        assert(failureBatches[0].sourceKind == WebRendererSceneBatchKind::DynamicDObj);
    }
    assert(completed);
}
} // namespace

int main()
{
    TestRetentionCopySlotsAndClear();
    TestDeterministicLayoutAndMaterialData();
    TestDirectedAxisAndMultiCloudOrdering();
    TestFailureAtomicAppendAndCapacity();
    TestRepeatedAppendAndAllocationRollback();
    return 0;
}
