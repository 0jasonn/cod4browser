#include <database/db_generated_gfxworld_platform.h>
#include <database/db_runtime_prefix.h>
#include <gfx_d3d/gfx_world_types.h>
#include <web/web_engine_world_surface.h>
#include <web/web_renderer.h>
#include <web/web_system.h>

#include <emscripten.h>

#include <cstdint>

namespace
{
EM_JS(void, DispatchCanonicalGfxWorld, (
    const char *state, const char *message, const char *name,
    const char *baseName, std::uint32_t planeCount, std::uint32_t nodeCount,
    std::uint32_t cellCount, std::uint32_t vertexCount,
    std::uint32_t indexCount, std::uint32_t surfaceCount,
    std::uint32_t staticModelCount, std::uint32_t lightmapCount,
    std::uint32_t materialMemoryCount, std::uint32_t inflatedOffset,
    std::uint32_t assetIndex, std::uint32_t assetCount,
    std::uint32_t selectedSurface, std::uint32_t selectedVertexCount,
    std::uint32_t selectedTriangleCount, const char *materialName,
    const char *adapterResult, const char *submissionResult), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:canonical-gfxworld", {
        detail: {
            state: UTF8ToString(state),
            message: UTF8ToString(message),
            sourceRepresentation: "real-kisak-db-gfxworld",
            databaseOwned: true,
            browserWorldRepresentation: false,
            name: name ? UTF8ToString(name) : "",
            baseName: baseName ? UTF8ToString(baseName) : "",
            planeCount: planeCount >>> 0,
            nodeCount: nodeCount >>> 0,
            cellCount: cellCount >>> 0,
            vertexCount: vertexCount >>> 0,
            indexCount: indexCount >>> 0,
            surfaceCount: surfaceCount >>> 0,
            staticModelCount: staticModelCount >>> 0,
            lightmapCount: lightmapCount >>> 0,
            materialMemoryCount: materialMemoryCount >>> 0,
            inflatedOffset: inflatedOffset >>> 0,
            assetIndex: assetIndex >>> 0,
            assetCount: assetCount >>> 0,
            selectedSurface: selectedSurface >>> 0,
            selectedVertexCount: selectedVertexCount >>> 0,
            selectedTriangleCount: selectedTriangleCount >>> 0,
            materialName: materialName ? UTF8ToString(materialName) : "",
            adapterResult: UTF8ToString(adapterResult),
            submissionResult: UTF8ToString(submissionResult)
        }
    }));
});

}

void DB_PlatformPublishGfxWorld(const GfxWorld *world)
{
    if (!world) return;

    const DBRuntimeTraceSnapshot &trace = DB_GetRuntimeTrace();
    WebEngineGfxWorldSurfacePublication publication;
    const WebEngineGfxWorldSurfaceResult adapter =
        WebEngine_BuildGfxWorldSurface(*world, publication);
    WebRendererSurfaceResult submission =
        WebRendererSurfaceResult::InvalidDescriptor;
    if (adapter == WebEngineGfxWorldSurfaceResult::Success)
    {
        const auto &converted = publication.rendererSurface;
        const WebRendererSurfaceDesc descriptor{
            converted.vertices.data(),
            static_cast<std::uint32_t>(converted.vertices.size()),
            converted.indices.data(),
            static_cast<std::uint32_t>(converted.indices.size()),
        };
        submission = WebRenderer_SetSurface(descriptor, converted.draw);
    }

    const bool submitted =
        submission == WebRendererSurfaceResult::Success;
    const bool proved = submitted;
    const char *state = proved ? "submitted" : "failed";
    const char *message = proved
        ? "The real DB-owned GfxWorld bounded surface was submitted to WebGL2"
        : "The canonical GfxWorld failed its bounded WebGL2 submission";

    DispatchCanonicalGfxWorld(
        state, message, world->name, world->baseName,
        static_cast<std::uint32_t>(world->planeCount),
        static_cast<std::uint32_t>(world->nodeCount),
        static_cast<std::uint32_t>(world->dpvsPlanes.cellCount),
        world->vertexCount, static_cast<std::uint32_t>(world->indexCount),
        static_cast<std::uint32_t>(world->surfaceCount),
        world->dpvs.smodelCount,
        static_cast<std::uint32_t>(world->lightmapCount),
        static_cast<std::uint32_t>(world->materialMemoryCount),
        trace.decompressedBytesProduced, trace.assetIndex, trace.xassetCount,
        publication.surfaceIndex,
        publication.vertexCount, publication.triangleCount,
        publication.materialName ? publication.materialName : "",
        WebEngine_GfxWorldSurfaceResultString(adapter),
        WebRenderer_SurfaceResultString(submission));

    Web_Log(proved ? WebLogLevel::Info : WebLogLevel::Error,
        "[kisakcod-web] Canonical DB GfxWorld '%s': WebGL2 submission=%s.\n",
        world->name ? world->name : "",
        WebRenderer_SurfaceResultString(submission));
}
