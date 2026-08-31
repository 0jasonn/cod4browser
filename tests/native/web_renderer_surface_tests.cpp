#include <web/web_renderer_surface_storage.h>
#include <web/web_renderer_dynamic_textures.h>
#include <web/web_renderer_draw_state.h>
#include <web/web_renderer.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
class TestFailure final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw TestFailure(std::string(message));
    }
}

void RequireResult(
    WebRendererSurfaceResult actual,
    WebRendererSurfaceResult expected,
    std::string_view context)
{
    if (actual == expected)
    {
        return;
    }
    std::string message(context);
    message += ": expected ";
    message += WebRenderer_SurfaceResultString(expected);
    message += ", got ";
    message += WebRenderer_SurfaceResultString(actual);
    throw TestFailure(message);
}

using Vertices = std::array<WebRendererSurfaceVertex, 3>;
using Indices = std::array<std::uint16_t, 3>;

Vertices MakeVertices()
{
    return {{
        {{0.0f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.5f, 0.0f}},
        {{-0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    }};
}

WebRendererDrawDesc MakeDraw()
{
    return {
        WebRendererPrimitiveTopology::TriangleList,
        0u,
        3u,
        WebRendererTextureBinding::EngineImage,
    };
}

void TestValidTriangle()
{
    const Vertices vertices = MakeVertices();
    const Indices indices = {0u, 1u, 2u};
    const WebRendererSurfaceDesc surface{
        vertices.data(),
        static_cast<std::uint32_t>(vertices.size()),
        indices.data(),
        static_cast<std::uint32_t>(indices.size()),
    };
    RequireResult(
        WebRenderer_ValidateSurface(surface, MakeDraw()),
        WebRendererSurfaceResult::Success,
        "valid indexed triangle");

    WebRendererDrawDesc untextured = MakeDraw();
    untextured.textureBinding = WebRendererTextureBinding::None;
    RequireResult(
        WebRenderer_ValidateSurface(surface, untextured),
        WebRendererSurfaceResult::Success,
        "valid untextured indexed triangle");
    Require(sizeof(WebRendererSurfaceVertex) == 72u,
        "fixed vertex layout carries RGBA color and canonical normal");
}

void TestEmptyAndNullDescriptors()
{
    const Vertices vertices = MakeVertices();
    const Indices indices = {0u, 1u, 2u};
    const WebRendererDrawDesc draw = MakeDraw();
    for (const WebRendererSurfaceDesc surface : {
        WebRendererSurfaceDesc{nullptr, 3u, indices.data(), 3u},
        WebRendererSurfaceDesc{vertices.data(), 3u, nullptr, 3u},
        WebRendererSurfaceDesc{vertices.data(), 0u, indices.data(), 3u},
        WebRendererSurfaceDesc{vertices.data(), 3u, indices.data(), 0u},
    })
    {
        RequireResult(
            WebRenderer_ValidateSurface(surface, draw),
            WebRendererSurfaceResult::InvalidDescriptor,
            "empty or null surface");
    }

    WebRendererDrawDesc emptyDraw = draw;
    emptyDraw.indexCount = 0u;
    const WebRendererSurfaceDesc valid{
        vertices.data(), 3u, indices.data(), 3u,
    };
    RequireResult(
        WebRenderer_ValidateSurface(valid, emptyDraw),
        WebRendererSurfaceResult::InvalidDescriptor,
        "empty draw");
}

void TestBoundsBeforeDereference()
{
    const Vertices vertices = MakeVertices();
    const Indices indices = {0u, 1u, 2u};
    const WebRendererDrawDesc draw = MakeDraw();
    RequireResult(
        WebRenderer_ValidateSurface(
            {vertices.data(), WEB_RENDERER_MAX_SURFACE_VERTICES + 1u,
             indices.data(), 3u},
            draw),
        WebRendererSurfaceResult::OutputTooLarge,
        "oversized vertex count");
    RequireResult(
        WebRenderer_ValidateSurface(
            {vertices.data(), 3u,
             indices.data(), WEB_RENDERER_MAX_SURFACE_INDICES + 1u},
            draw),
        WebRendererSurfaceResult::OutputTooLarge,
        "oversized index count");
    Require(
        WEB_RENDERER_MAX_RETAINED_SURFACE_BYTES ==
            static_cast<std::size_t>(WEB_RENDERER_MAX_SURFACE_VERTICES) * 72u +
            static_cast<std::size_t>(WEB_RENDERER_MAX_SURFACE_INDICES) * 2u,
        "surface recovery ceiling covers both bounded arrays");
}

void TestDrawValidation()
{
    const Vertices vertices = MakeVertices();
    const Indices indices = {0u, 1u, 2u};
    const WebRendererSurfaceDesc surface{vertices.data(), 3u, indices.data(), 3u};

    WebRendererDrawDesc draw = MakeDraw();
    draw.firstIndex = 1u;
    RequireResult(
        WebRenderer_ValidateSurface(surface, draw),
        WebRendererSurfaceResult::InvalidDescriptor,
        "unaligned first index");

    draw = MakeDraw();
    draw.indexCount = 2u;
    RequireResult(
        WebRenderer_ValidateSurface(surface, draw),
        WebRendererSurfaceResult::InvalidDescriptor,
        "non-triangle index count");

    draw = MakeDraw();
    draw.firstIndex = 6u;
    draw.indexCount = UINT32_MAX - 3u;
    RequireResult(
        WebRenderer_ValidateSurface(surface, draw),
        WebRendererSurfaceResult::InvalidDescriptor,
        "subtraction-safe draw range");

    draw = MakeDraw();
    draw.topology = static_cast<WebRendererPrimitiveTopology>(0xffu);
    RequireResult(
        WebRenderer_ValidateSurface(surface, draw),
        WebRendererSurfaceResult::UnsupportedTopology,
        "unknown topology");

    draw = MakeDraw();
    draw.textureBinding = static_cast<WebRendererTextureBinding>(0xffu);
    RequireResult(
        WebRenderer_ValidateSurface(surface, draw),
        WebRendererSurfaceResult::UnsupportedTextureBinding,
        "unknown texture binding");

    const std::array<std::uint16_t, 6> twoTriangles = {0u, 1u, 2u, 2u, 1u, 0u};
    const WebRendererSurfaceDesc rangedSurface{
        vertices.data(),
        3u,
        twoTriangles.data(),
        static_cast<std::uint32_t>(twoTriangles.size()),
    };
    draw = MakeDraw();
    draw.firstIndex = 3u;
    RequireResult(
        WebRenderer_ValidateSurface(rangedSurface, draw),
        WebRendererSurfaceResult::Success,
        "valid nonzero triangle draw range");
}

void TestVertexAndIndexValidation()
{
    Vertices vertices = MakeVertices();
    Indices indices = {0u, 1u, 2u};
    WebRendererSurfaceDesc surface{vertices.data(), 3u, indices.data(), 3u};
    const WebRendererDrawDesc draw = MakeDraw();

    vertices[1].color[2] = std::numeric_limits<float>::infinity();
    RequireResult(
        WebRenderer_ValidateSurface(surface, draw),
        WebRendererSurfaceResult::NonFiniteVertex,
        "infinite vertex component");
    vertices = MakeVertices();
    vertices[2].textureCoordinate[0] = std::numeric_limits<float>::quiet_NaN();
    RequireResult(
        WebRenderer_ValidateSurface(surface, draw),
        WebRendererSurfaceResult::NonFiniteVertex,
        "NaN vertex component");

    vertices = MakeVertices();
    indices[2] = 3u;
    RequireResult(
        WebRenderer_ValidateSurface(surface, draw),
        WebRendererSurfaceResult::IndexOutOfRange,
        "out-of-range index");
}

bool SameVertices(
    const std::vector<WebRendererSurfaceVertex> &actual,
    const Vertices &expected)
{
    return actual.size() == expected.size() &&
        std::memcmp(
            actual.data(),
            expected.data(),
            expected.size() * sizeof(WebRendererSurfaceVertex)) == 0;
}

void TestOwnedCopyAndAtomicFailure()
{
    Vertices sourceVertices = MakeVertices();
    Indices sourceIndices = {0u, 1u, 2u};
    const Vertices expectedVertices = sourceVertices;
    const Indices expectedIndices = sourceIndices;
    const WebRendererSurfaceDesc surface{
        sourceVertices.data(), 3u, sourceIndices.data(), 3u,
    };
    const WebRendererDrawDesc draw = MakeDraw();
    WebRendererOwnedSurface owned;
    RequireResult(
        WebRenderer_CopySurface(surface, draw, owned),
        WebRendererSurfaceResult::Success,
        "copy callback-scoped surface");

    sourceVertices[0].position[0] = 0.91f;
    sourceVertices[1].textureCoordinate[1] = 0.37f;
    sourceIndices[2] = 0u;
    Require(SameVertices(owned.vertices, expectedVertices),
        "owned vertices do not alias the caller array");
    Require(owned.indices == std::vector<std::uint16_t>(
        expectedIndices.begin(), expectedIndices.end()),
        "owned indices do not alias the caller array");
    Require(owned.draw.firstIndex == draw.firstIndex &&
        owned.draw.indexCount == draw.indexCount &&
        owned.draw.topology == draw.topology &&
        owned.draw.textureBinding == draw.textureBinding,
        "owned draw is copied by value");

    const std::vector<WebRendererSurfaceVertex> beforeVertices = owned.vertices;
    const std::vector<std::uint16_t> beforeIndices = owned.indices;
    const WebRendererDrawDesc beforeDraw = owned.draw;
    sourceIndices[2] = 9u;
    RequireResult(
        WebRenderer_CopySurface(surface, draw, owned),
        WebRendererSurfaceResult::IndexOutOfRange,
        "reject invalid replacement");
    Require(owned.vertices.size() == beforeVertices.size() &&
        std::memcmp(
            owned.vertices.data(),
            beforeVertices.data(),
            beforeVertices.size() * sizeof(WebRendererSurfaceVertex)) == 0,
        "failed replacement preserves owned vertices");
    Require(owned.indices == beforeIndices,
        "failed replacement preserves owned indices");
    Require(owned.draw.firstIndex == beforeDraw.firstIndex &&
        owned.draw.indexCount == beforeDraw.indexCount &&
        owned.draw.topology == beforeDraw.topology &&
        owned.draw.textureBinding == beforeDraw.textureBinding,
        "failed replacement preserves owned draw");
}

void TestReusableStagedGeometry()
{
    std::vector<WebRendererSurfaceVertex> source(12u);
    std::vector<std::uint32_t> sourceIndices = {0u, 4u, 11u};
    source[0].position[2] = 7.0f;
    source[11].binormalSign = -1.0f;
    std::vector<WebRendererSurfaceVertex> staging;
    std::vector<std::uint32_t> stagingIndices;
    RequireResult(WebRenderer_CopyStagedGeometry(source, sourceIndices,
        staging, stagingIndices), WebRendererSurfaceResult::Success, "stage geometry");
    auto *const vertexStorage = staging.data();
    auto *const indexStorage = stagingIndices.data();
    source[0].position[2] = 9.0f;
    Require(staging[0].position[2] == 7.0f, "staging owns copied geometry");
    source.resize(6u);
    sourceIndices = {5u, 1u, 0u};
    RequireResult(WebRenderer_CopyStagedGeometry(source, sourceIndices,
        staging, stagingIndices), WebRendererSurfaceResult::Success, "replace smaller geometry");
    Require(staging.data() == vertexStorage && stagingIndices.data() == indexStorage,
        "a fitting replacement reuses both allocations");
    Require(staging.size() == source.size() && stagingIndices == sourceIndices,
        "replacement updates counts and indices without stale tails");
    Require(std::memcmp(staging.data(), source.data(),
        source.size() * sizeof(WebRendererSurfaceVertex)) == 0, "all vertex fields copied");
    source.resize(24u);
    sourceIndices = {0u, 23u, 2u, 2u, 23u, 1u};
    RequireResult(WebRenderer_CopyStagedGeometry(source, sourceIndices,
        staging, stagingIndices), WebRendererSurfaceResult::Success, "grow geometry");
    Require(staging.size() == source.size() && stagingIndices == sourceIndices,
        "growth preserves the complete new command");
    RequireResult(WebRenderer_CopyStagedGeometry({}, {}, staging, stagingIndices),
        WebRendererSurfaceResult::Success, "clear staged geometry");
    Require(staging.empty() && stagingIndices.empty(), "empty input clears logical storage");
}

void TestDynamicTextureBindings()
{
    // Model texture-object sampler state separately from unit bindings, as GL
    // does. Compare each draw against unconditional ordered binding, including
    // aliases with conflicting samplers and every single-field transition.
    struct State
    {
        std::array<std::uint32_t, 10> units{};
        std::array<std::uint8_t, 8> textureSamplers{};
        unsigned calls = 0;
        void Bind(std::uint32_t unit, std::uint32_t texture, std::uint8_t sampler)
        {
            units.at(unit) = texture;
            textureSamplers.at(texture) = sampler;
            ++calls;
        }
    } actual, reference;
    WebRendererDynamicTextures bindings;
    const auto apply = [&](const WebRendererDynamicTextureSet &set) {
        reference.Bind(0, set.textures[0], set.samplers[0]);
        reference.Bind(1, set.textures[1], set.samplers[1]);
        reference.Bind(4, set.textures[2], set.samplers[2]);
        reference.Bind(5, set.textures[3], set.samplers[3]);
        reference.Bind(2, set.textures[4], 0x62);
        reference.Bind(9, set.textures[5], 0x62);
        bindings.Apply(set, [&](auto unit, auto texture, auto sampler, bool) {
            actual.Bind(unit, texture, sampler);
        });
        Require(actual.units == reference.units &&
            actual.textureSamplers == reference.textureSamplers,
            "draw sees unchanged texture bindings and object sampler state");
    };
    const WebRendererDynamicTextureSet base{{1, 2, 3, 4, 5, 6}, {1, 2, 3, 4}};
    apply(base);
    Require(actual.calls == 6, "first set binds every unit");
    apply(base);
    Require(actual.calls == 6, "identical set needs no GL calls");
    for (std::size_t i = 0; i < base.textures.size(); ++i)
    {
        auto changed = base;
        changed.textures[i] = 7;
        apply(changed);
        apply(base);
    }
    for (std::size_t i = 0; i < base.samplers.size(); ++i)
    {
        auto changed = base;
        changed.samplers[i] = 0x62;
        apply(changed);
        apply(base);
    }
    const WebRendererDynamicTextureSet aliases{{1, 1, 1, 1, 1, 1}, {1, 2, 3, 4}};
    apply(aliases);
    apply(aliases);
    apply(base);
    bindings = {}; // Another pass/frame/context must bind afresh.
    const unsigned before = actual.calls;
    apply(base);
    Require(actual.calls == before + 6, "new pass cannot reuse old GL state");
    bindings = {};
    apply({});
    Require(actual.calls == before + 12, "zero-valued first set still binds");
}

void TestDynamicDrawState()
{
    WebRendererDrawState<WebRendererWorldBatchDesc> state;
    const std::array<float, 16> scene{1.0f}, depthHack{2.0f}, sun{3.0f};
    std::array<float, 16> uploaded{};
    unsigned projectionUpdates = 0;
    for (const auto *matrix : {&scene, &scene, &sun, &scene, &depthHack, &depthHack})
    {
        if (state.NeedsProjection(matrix->data()))
        {
            uploaded = *matrix;
            ++projectionUpdates;
        }
        Require(uploaded == *matrix, "each draw keeps its projection despite skipped uploads");
    }
    Require(projectionUpdates == 4, "only repeated projections are omitted");
    uploaded = sun; // Direct GL override, as in the sun-query path.
    state.Reset();
    Require(state.NeedsProjection(depthHack.data()), "sun override requires projection restoration");

    const WebRendererWorldBatchDesc base{};
    Require(state.NeedsMaterial(base), "first material is always applied");
    Require(!state.NeedsMaterial(base), "repeated material needs no state calls");
    const auto requireMaterialChange = [&](auto modify) {
        auto changed = base;
        modify(changed);
        state.Reset();
        Require(state.NeedsMaterial(base), "new pass applies baseline state");
        Require(state.NeedsMaterial(changed), "changed material input cannot be omitted");
        Require(!state.NeedsMaterial(changed), "unchanged material can be omitted");
        Require(state.NeedsMaterial(base), "return to earlier material restores its state");
        state.Reset(); // Do not retain the local changed batch beyond its lifetime.
    };
    // Identities are opaque and never dereferenced by the backend state helper.
    requireMaterialChange([](auto &b) { b.materialIdentity = reinterpret_cast<const Material *>(1); });
    for (unsigned word = 0; word < 2; ++word)
        for (unsigned bit = 0; bit < 32; ++bit)
            requireMaterialChange([&](auto &b) { b.stateBits[word] = 1u << bit; });
    requireMaterialChange([](auto &b) { b.technique = WebRendererWorldTechnique::VertexColorDistanceFalloff; });
    requireMaterialChange([](auto &b) { b.sourceKind = WebRendererSceneBatchKind::SunSprite; });
    requireMaterialChange([](auto &b) { b.ambientProbeLighting = true; });
    for (unsigned component = 0; component < 4; ++component)
    {
        requireMaterialChange([&](auto &b) { b.falloffParms[component] = 0.5f; });
        requireMaterialChange([&](auto &b) { b.falloffBeginColor[component] = 0.5f; });
        requireMaterialChange([&](auto &b) { b.falloffEndColor[component] = 0.5f; });
    }
    requireMaterialChange([](auto &b) { b.falloffParms[0] = -0.0f; });
    auto differentDraw = base;
    differentDraw.firstIndex = 3;
    differentDraw.modelLightingCoordinates[0] = 0.25f;
    state.NeedsMaterial(base);
    Require(!state.NeedsMaterial(differentDraw),
        "geometry and per-model lighting remain per-draw, not material state");

    const std::array<bool, 9> disabled{};
    Require(state.NeedsFeatures(disabled), "all-disabled features are still uploaded first");
    Require(!state.NeedsFeatures(disabled), "unchanged feature flags need no uploads");
    for (unsigned bit = 0; bit < disabled.size(); ++bit)
    {
        auto enabled = disabled;
        enabled[bit] = true;
        Require(state.NeedsFeatures(enabled), "each feature transition must upload");
        Require(!state.NeedsFeatures(enabled), "repeated feature group can be omitted");
        Require(state.NeedsFeatures(disabled), "disabling a feature must upload");
    }
    // Sun sprite depth overrides and a new camera pass/frame/context invalidate
    // all three groups even when the requested values are identical.
    state.Reset();
    Require(state.NeedsProjection(depthHack.data()) && state.NeedsMaterial(differentDraw) &&
        state.NeedsFeatures(disabled), "reset cannot retain any stale GL state");
}

void TestShadowState()
{
    WebRendererShadowState state;
    for (int alpha = 0; alpha < 4; ++alpha)
    {
        Require(state.NeedsAlpha(alpha, false), "new shadow alpha mode must upload");
        Require(!state.NeedsAlpha(alpha, false), "repeated shadow alpha can be omitted");
        Require(state.NeedsAlpha(alpha, true), "texture availability must upload");
        Require(!state.NeedsAlpha(alpha, true), "unchanged sampling can be omitted");
        Require(state.NeedsAlpha(alpha, false), "missing texture must disable sampling");
    }
    for (std::uint32_t cull : {0u, 0x8000u, 0xc000u, 0x4000u, 0u})
    {
        Require(state.NeedsCull(cull), "cull-mode transition must be applied");
        Require(!state.NeedsCull(cull | 0x18000000u), "unrelated write bits do not alter cull mode");
    }
    state = {}; // Every sun near/far and spot partition starts unknown.
    Require(state.NeedsAlpha(0, false) && state.NeedsCull(0),
        "new shadow partition must restore alpha and culling even for zero state");
}

void TestErrorStrings()
{
    for (const WebRendererSurfaceResult result : {
        WebRendererSurfaceResult::Success,
        WebRendererSurfaceResult::InvalidDescriptor,
        WebRendererSurfaceResult::UnsupportedTopology,
        WebRendererSurfaceResult::UnsupportedTextureBinding,
        WebRendererSurfaceResult::OutputTooLarge,
        WebRendererSurfaceResult::NonFiniteVertex,
        WebRendererSurfaceResult::IndexOutOfRange,
        WebRendererSurfaceResult::AllocationFailed,
        WebRendererSurfaceResult::BackendFailure,
    })
    {
        Require(std::strlen(WebRenderer_SurfaceResultString(result)) > 0u,
            "every surface result has a printable description");
    }
}

class Runner
{
public:
    void Run(const char *name, const std::function<void()> &test)
    {
        try
        {
            test();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        }
        catch (const std::exception &error)
        {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }

    int Result() const
    {
        std::cout << passed_ << " passed, " << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

private:
    int passed_ = 0;
    int failed_ = 0;
};
} // namespace

int main()
{
    Runner runner;
    runner.Run("valid indexed triangle", TestValidTriangle);
    runner.Run("empty and null descriptors", TestEmptyAndNullDescriptors);
    runner.Run("bounded counts", TestBoundsBeforeDereference);
    runner.Run("draw validation", TestDrawValidation);
    runner.Run("vertex and index validation", TestVertexAndIndexValidation);
    runner.Run("owned copy and atomic failure", TestOwnedCopyAndAtomicFailure);
    runner.Run("reusable staged geometry", TestReusableStagedGeometry);
    runner.Run("dynamic texture binding equivalence", TestDynamicTextureBindings);
    runner.Run("dynamic draw state transitions", TestDynamicDrawState);
    runner.Run("shadow state transitions", TestShadowState);
    runner.Run("surface result strings", TestErrorStrings);
    return runner.Result();
}
