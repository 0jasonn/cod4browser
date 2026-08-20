#include <web/web_renderer_surface_storage.h>

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
    Require(sizeof(WebRendererSurfaceVertex) == 44u, "fixed vertex layout carries RGBA color");
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
            static_cast<std::size_t>(WEB_RENDERER_MAX_SURFACE_VERTICES) * 44u +
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
    runner.Run("surface result strings", TestErrorStrings);
    return runner.Result();
}
