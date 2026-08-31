#include <database/db_generated_gfxworld_platform.h>
#include <database/db_runtime_prefix.h>
#include <gfx_d3d/gfx_world_types.h>
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
    std::uint32_t assetIndex, std::uint32_t assetCount), {
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
            assetCount: assetCount >>> 0
        }
    }));
});

}

void DB_PlatformPublishGfxWorld(const GfxWorld *world)
{
    if (!world) return;

    const DBRuntimeTraceSnapshot &trace = DB_GetRuntimeTrace();
    DispatchCanonicalGfxWorld(
        "published", "The real DB-owned GfxWorld was published; rendering belongs to R_RenderScene",
        world->name, world->baseName,
        static_cast<std::uint32_t>(world->planeCount),
        static_cast<std::uint32_t>(world->nodeCount),
        static_cast<std::uint32_t>(world->dpvsPlanes.cellCount),
        world->vertexCount, static_cast<std::uint32_t>(world->indexCount),
        static_cast<std::uint32_t>(world->surfaceCount),
        world->dpvs.smodelCount,
        static_cast<std::uint32_t>(world->lightmapCount),
        static_cast<std::uint32_t>(world->materialMemoryCount),
        trace.decompressedBytesProduced, trace.assetIndex, trace.xassetCount);

    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Canonical DB GfxWorld '%s' published (%d surfaces).\n",
        world->name ? world->name : "", world->surfaceCount);
}
