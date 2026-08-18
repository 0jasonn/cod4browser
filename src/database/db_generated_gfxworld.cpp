#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <gfx_d3d/gfx_world_types.h>

#include <cstdint>
#include <limits>

GfxWorld *varGfxWorld = nullptr;
GfxWorld **varGfxWorldPtr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical GfxWorld loader requires the IW3 32-bit ABI");
static_assert(sizeof(GfxWorld) == 732u);

bool CheckedBytes(std::uint64_t count, std::size_t stride,
    const char *stage, std::int32_t &bytes)
{
    const std::uint64_t total = count * stride;
    if (stride && total / stride != count ||
        total > static_cast<std::uint64_t>(
            (std::numeric_limits<std::int32_t>::max)()))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    bytes = static_cast<std::int32_t>(total);
    return true;
}

template <typename T>
bool LoadArray(T *&target, std::uint64_t count, std::int32_t alignment,
    const char *stage)
{
    std::int32_t bytes = 0;
    if (!CheckedBytes(count, sizeof(T), stage, bytes)) return false;
    target = reinterpret_cast<T *>(DB_AllocStreamPos(alignment));
    if (DB_RuntimeGeneratedLoadFailed() ||
        !DB_RuntimeStreamCanRead(static_cast<std::size_t>(bytes)))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(target), bytes);
    return !DB_RuntimeGeneratedLoadFailed();
}

template <typename T>
bool AllocateArray(T *&target, std::uint64_t count, std::int32_t alignment,
    const char *stage)
{
    std::int32_t bytes = 0;
    if (!CheckedBytes(count, sizeof(T), stage, bytes)) return false;
    target = reinterpret_cast<T *>(DB_AllocStreamPos(alignment));
    if (DB_RuntimeGeneratedLoadFailed() ||
        !DB_RuntimeStreamCanRead(static_cast<std::size_t>(bytes)))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    return true;
}

template <typename T>
bool LoadEmbeddedArray(T *target, std::uint64_t count, const char *stage)
{
    std::int32_t bytes = 0;
    if (!CheckedBytes(count, sizeof(T), stage, bytes)) return false;
    if (!DB_RuntimeStreamCanRead(static_cast<std::size_t>(bytes)))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(target), bytes);
    return !DB_RuntimeGeneratedLoadFailed();
}

template <typename T>
bool NonNegative(T value, const char *stage)
{
    if (value < 0)
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    return true;
}

bool LoadGfxLight(GfxLight *light, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(light),
        sizeof(GfxLight));
    if (DB_RuntimeGeneratedLoadFailed()) return false;
    varGfxLightDefPtr = &light->def;
    Load_GfxLightDefPtr(false);
    return !DB_RuntimeGeneratedLoadFailed();
}

bool LoadReflectionProbes(GfxReflectionProbe *probes, std::uint32_t count)
{
    if (!LoadEmbeddedArray(probes, count,
        "GfxWorld/reflection probe array")) return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        Load_Stream(false, reinterpret_cast<std::uint8_t *>(&probes[index]),
            sizeof(GfxReflectionProbe));
        varGfxImagePtr = &probes[index].reflectionImage;
        Load_GfxImagePtr(false);
        if (DB_RuntimeGeneratedLoadFailed()) return false;
    }
    return true;
}

bool LoadAabbTree(GfxAabbTree *tree, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(tree),
        sizeof(GfxAabbTree));
    if (DB_RuntimeGeneratedLoadFailed()) return false;
    if (!tree->smodelIndexes) return true;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
        tree->smodelIndexes);
    if (token == UINT32_MAX)
        return LoadArray(tree->smodelIndexes, tree->smodelIndexCount, 1,
            "GfxWorld/aabb smodel indexes");
    DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
        &tree->smodelIndexes));
    return !DB_RuntimeGeneratedLoadFailed();
}

bool LoadPortals(GfxPortal *portals, std::uint32_t count);

bool LoadCell(GfxCell *cell, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(cell),
        sizeof(GfxCell));
    if (DB_RuntimeGeneratedLoadFailed() ||
        !NonNegative(cell->aabbTreeCount, "GfxWorld/negative aabb count") ||
        !NonNegative(cell->portalCount, "GfxWorld/negative portal count") ||
        !NonNegative(cell->cullGroupCount,
            "GfxWorld/negative cell cull-group count")) return false;
    if (cell->aabbTree)
    {
        if (!LoadArray(cell->aabbTree,
            static_cast<std::uint32_t>(cell->aabbTreeCount), 3,
            "GfxWorld/aabb tree array")) return false;
        for (std::int32_t index = 0; index < cell->aabbTreeCount; ++index)
            if (!LoadAabbTree(&cell->aabbTree[index], false)) return false;
    }
    if (cell->portals)
    {
        if (!LoadArray(cell->portals,
            static_cast<std::uint32_t>(cell->portalCount), 3,
            "GfxWorld/portal array")) return false;
        if (!LoadPortals(cell->portals,
            static_cast<std::uint32_t>(cell->portalCount))) return false;
    }
    if (cell->cullGroups && !LoadArray(cell->cullGroups,
        static_cast<std::uint32_t>(cell->cullGroupCount), 3,
        "GfxWorld/cell cull groups")) return false;
    if (cell->reflectionProbes && !LoadArray(cell->reflectionProbes,
        cell->reflectionProbeCount, 0,
        "GfxWorld/cell reflection probes")) return false;
    return true;
}

bool LoadPortal(GfxPortal *portal, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(portal),
        sizeof(GfxPortal));
    if (DB_RuntimeGeneratedLoadFailed()) return false;
    if (portal->cell)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            portal->cell);
        if (token == UINT32_MAX)
        {
            portal->cell = reinterpret_cast<GfxCell *>(DB_AllocStreamPos(3));
            if (!LoadCell(portal->cell, true)) return false;
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &portal->cell));
            if (DB_RuntimeGeneratedLoadFailed()) return false;
        }
    }
    if (portal->vertices && !LoadArray(portal->vertices,
        portal->vertexCount, 3, "GfxWorld/portal vertices")) return false;
    return true;
}

bool LoadPortals(GfxPortal *portals, std::uint32_t count)
{
    for (std::uint32_t index = 0; index < count; ++index)
        if (!LoadPortal(&portals[index], false)) return false;
    return true;
}

bool LoadCells(GfxCell *cells, std::uint32_t count)
{
    if (!LoadEmbeddedArray(cells, count, "GfxWorld/cell array")) return false;
    for (std::uint32_t index = 0; index < count; ++index)
        if (!LoadCell(&cells[index], false)) return false;
    return true;
}

bool LoadLightmaps(GfxLightmapArray *lightmaps, std::uint32_t count)
{
    if (!LoadEmbeddedArray(lightmaps, count,
        "GfxWorld/lightmap array")) return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        Load_Stream(false, reinterpret_cast<std::uint8_t *>(&lightmaps[index]),
            sizeof(GfxLightmapArray));
        varGfxImagePtr = &lightmaps[index].primary;
        Load_GfxImagePtr(false);
        if (DB_RuntimeGeneratedLoadFailed()) return false;
        varGfxImagePtr = &lightmaps[index].secondary;
        Load_GfxImagePtr(false);
        if (DB_RuntimeGeneratedLoadFailed()) return false;
    }
    return true;
}

bool LoadLightGrid(GfxLightGrid *grid)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(grid),
        sizeof(GfxLightGrid));
    if (DB_RuntimeGeneratedLoadFailed() || grid->rowAxis > 2u ||
        grid->maxs[grid->rowAxis] < grid->mins[grid->rowAxis])
    {
        DB_RuntimeGeneratedFailure("GfxWorld/invalid light grid");
        return false;
    }
    if (grid->rowDataStart)
    {
        const std::uint32_t count = static_cast<std::uint32_t>(
            grid->maxs[grid->rowAxis] - grid->mins[grid->rowAxis]) + 1u;
        if (!LoadArray(grid->rowDataStart, count, 1,
            "GfxWorld/light-grid row starts")) return false;
    }
    if (grid->rawRowData && !LoadArray(grid->rawRowData,
        grid->rawRowDataSize, 0, "GfxWorld/light-grid raw rows")) return false;
    if (grid->entries && !LoadArray(grid->entries, grid->entryCount, 3,
        "GfxWorld/light-grid entries")) return false;
    if (grid->colors && !LoadArray(grid->colors, grid->colorCount, 3,
        "GfxWorld/light-grid colors")) return false;
    return true;
}

bool LoadMaterialMemory(MaterialMemory *entries, std::uint32_t count)
{
    if (!LoadEmbeddedArray(entries, count,
        "GfxWorld/material memory array")) return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        Load_Stream(false, reinterpret_cast<std::uint8_t *>(&entries[index]),
            sizeof(MaterialMemory));
        varMaterialHandle = &entries[index].material;
        Load_MaterialHandle(false);
        if (DB_RuntimeGeneratedLoadFailed()) return false;
    }
    return true;
}

bool LoadSunflare(sunflare_t *sun)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(sun),
        sizeof(sunflare_t));
    varMaterialHandle = &sun->spriteMaterial;
    Load_MaterialHandle(false);
    if (DB_RuntimeGeneratedLoadFailed()) return false;
    varMaterialHandle = &sun->flareMaterial;
    Load_MaterialHandle(false);
    return !DB_RuntimeGeneratedLoadFailed();
}

bool LoadShadowGeometry(GfxShadowGeometry *geometry, std::uint32_t count)
{
    if (!LoadEmbeddedArray(geometry, count,
        "GfxWorld/shadow geometry array")) return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        GfxShadowGeometry &entry = geometry[index];
        Load_Stream(false, reinterpret_cast<std::uint8_t *>(&entry),
            sizeof(entry));
        if (entry.sortedSurfIndex && !LoadArray(entry.sortedSurfIndex,
            entry.surfaceCount, 1, "GfxWorld/shadow surface indexes"))
            return false;
        if (entry.smodelIndex && !LoadArray(entry.smodelIndex,
            entry.smodelCount, 1, "GfxWorld/shadow model indexes"))
            return false;
    }
    return true;
}

bool LoadLightRegions(GfxLightRegion *regions, std::uint32_t count)
{
    if (!LoadEmbeddedArray(regions, count,
        "GfxWorld/light region array")) return false;
    for (std::uint32_t regionIndex = 0; regionIndex < count; ++regionIndex)
    {
        GfxLightRegion &region = regions[regionIndex];
        Load_Stream(false, reinterpret_cast<std::uint8_t *>(&region),
            sizeof(region));
        if (!region.hulls) continue;
        if (!LoadArray(region.hulls, region.hullCount, 3,
            "GfxWorld/light region hulls")) return false;
        for (std::uint32_t hullIndex = 0; hullIndex < region.hullCount;
            ++hullIndex)
        {
            GfxLightRegionHull &hull = region.hulls[hullIndex];
            Load_Stream(false, reinterpret_cast<std::uint8_t *>(&hull),
                sizeof(hull));
            if (hull.axis && !LoadArray(hull.axis, hull.axisCount, 3,
                "GfxWorld/light region axes")) return false;
        }
    }
    return true;
}

bool LoadRuntimeBytes(void *&target, std::uint64_t count,
    std::size_t stride, std::int32_t alignment, const char *stage)
{
    std::int32_t bytes = 0;
    if (!CheckedBytes(count, stride, stage, bytes)) return false;
    target = DB_AllocStreamPos(alignment);
    if (DB_RuntimeGeneratedLoadFailed() ||
        !DB_RuntimeStreamCanRead(static_cast<std::size_t>(bytes)))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    Load_Stream(true, static_cast<std::uint8_t *>(target), bytes);
    return !DB_RuntimeGeneratedLoadFailed();
}

bool LoadDpvsPlanes(GfxWorldDpvsPlanes *planes)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(planes),
        sizeof(*planes));
    if (DB_RuntimeGeneratedLoadFailed() ||
        !NonNegative(planes->cellCount, "GfxWorld/negative cell count"))
        return false;
    if (planes->planes)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            planes->planes);
        if (token == UINT32_MAX)
        {
            if (!LoadArray(planes->planes,
                static_cast<std::uint32_t>(varGfxWorld->planeCount), 3,
                "GfxWorld/plane array")) return false;
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &planes->planes));
            if (DB_RuntimeGeneratedLoadFailed()) return false;
        }
    }
    if (planes->nodes && !LoadArray(planes->nodes,
        static_cast<std::uint32_t>(varGfxWorld->nodeCount), 1,
        "GfxWorld/dpvs nodes")) return false;
    DB_PushStreamPos(1);
    if (planes->sceneEntCellBits)
    {
        void *value = planes->sceneEntCellBits;
        if (!LoadRuntimeBytes(value,
            static_cast<std::uint64_t>(planes->cellCount) << 8u,
            sizeof(std::uint32_t), 3, "GfxWorld/scene entity cell bits"))
        {
            DB_PopStreamPos();
            return false;
        }
        planes->sceneEntCellBits = static_cast<std::uint32_t *>(value);
    }
    DB_PopStreamPos();
    return true;
}

bool LoadDpvsStatic(GfxWorldDpvsStatic *dpvs)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(dpvs), sizeof(*dpvs));
    if (DB_RuntimeGeneratedLoadFailed() ||
        !NonNegative(varGfxWorld->surfaceCount,
            "GfxWorld/negative surface count") ||
        !NonNegative(varGfxWorld->cullGroupCount,
            "GfxWorld/negative cull group count")) return false;
    for (std::uint32_t index = 0; index < 3; ++index)
    {
        DB_PushStreamPos(1);
        if (dpvs->smodelVisData[index])
        {
            void *value = dpvs->smodelVisData[index];
            if (!LoadRuntimeBytes(value, dpvs->smodelCount, 1, 0,
                "GfxWorld/smodel visibility"))
            {
                DB_PopStreamPos(); return false;
            }
            dpvs->smodelVisData[index] = static_cast<std::uint8_t *>(value);
        }
        DB_PopStreamPos();
    }
    for (std::uint32_t index = 0; index < 3; ++index)
    {
        DB_PushStreamPos(1);
        if (dpvs->surfaceVisData[index])
        {
            void *value = dpvs->surfaceVisData[index];
            if (!LoadRuntimeBytes(value, dpvs->staticSurfaceCount, 1, 0,
                "GfxWorld/surface visibility"))
            {
                DB_PopStreamPos(); return false;
            }
            dpvs->surfaceVisData[index] = static_cast<std::uint8_t *>(value);
        }
        DB_PopStreamPos();
    }
    DB_PushStreamPos(1);
    if (dpvs->lodData)
    {
        void *value = dpvs->lodData;
        if (!LoadRuntimeBytes(value,
            static_cast<std::uint64_t>(dpvs->smodelVisDataCount) * 2u,
            sizeof(std::uint32_t), 127, "GfxWorld/lod data"))
        {
            DB_PopStreamPos(); return false;
        }
        dpvs->lodData = static_cast<std::uint32_t *>(value);
    }
    DB_PopStreamPos();
    if (dpvs->sortedSurfIndex && !LoadArray(dpvs->sortedSurfIndex,
        static_cast<std::uint64_t>(dpvs->staticSurfaceCountNoDecal) +
            dpvs->staticSurfaceCount,
        1, "GfxWorld/sorted surface indexes")) return false;
    if (dpvs->smodelInsts && !LoadArray(dpvs->smodelInsts,
        dpvs->smodelCount, 3, "GfxWorld/static model instances")) return false;
    if (dpvs->surfaces)
    {
        if (!LoadArray(dpvs->surfaces,
            static_cast<std::uint32_t>(varGfxWorld->surfaceCount), 3,
            "GfxWorld/surfaces")) return false;
        for (std::int32_t index = 0; index < varGfxWorld->surfaceCount; ++index)
        {
            Load_Stream(false,
                reinterpret_cast<std::uint8_t *>(&dpvs->surfaces[index]),
                sizeof(GfxSurface));
            varMaterialHandle = &dpvs->surfaces[index].material;
            Load_MaterialHandle(false);
            if (DB_RuntimeGeneratedLoadFailed()) return false;
        }
    }
    if (dpvs->cullGroups && !LoadArray(dpvs->cullGroups,
        static_cast<std::uint32_t>(varGfxWorld->cullGroupCount), 3,
        "GfxWorld/cull groups")) return false;
    if (dpvs->smodelDrawInsts)
    {
        if (!LoadArray(dpvs->smodelDrawInsts, dpvs->smodelCount, 3,
            "GfxWorld/static model draw instances")) return false;
        for (std::uint32_t index = 0; index < dpvs->smodelCount; ++index)
        {
            Load_Stream(false, reinterpret_cast<std::uint8_t *>(
                &dpvs->smodelDrawInsts[index]),
                sizeof(GfxStaticModelDrawInst));
            varXModelPtr = &dpvs->smodelDrawInsts[index].model;
            Load_XModelPtr(false);
            if (DB_RuntimeGeneratedLoadFailed()) return false;
        }
    }
    DB_PushStreamPos(1);
    if (dpvs->surfaceMaterials)
    {
        void *value = dpvs->surfaceMaterials;
        if (!LoadRuntimeBytes(value, dpvs->staticSurfaceCount,
            sizeof(GfxDrawSurf), 3, "GfxWorld/surface materials"))
        {
            DB_PopStreamPos(); return false;
        }
        dpvs->surfaceMaterials = static_cast<GfxDrawSurf *>(value);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (dpvs->surfaceCastsSunShadow)
    {
        void *value = dpvs->surfaceCastsSunShadow;
        if (!LoadRuntimeBytes(value, dpvs->surfaceVisDataCount,
            sizeof(std::uint32_t), 127,
            "GfxWorld/surface sun-shadow bits"))
        {
            DB_PopStreamPos(); return false;
        }
        dpvs->surfaceCastsSunShadow = static_cast<std::uint32_t *>(value);
    }
    DB_PopStreamPos();
    return true;
}

bool LoadDpvsDynamic(GfxWorldDpvsDynamic *dpvs)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(dpvs), sizeof(*dpvs));
    if (DB_RuntimeGeneratedLoadFailed()) return false;
    for (std::uint32_t kind = 0; kind < 2; ++kind)
    {
        DB_PushStreamPos(1);
        if (dpvs->dynEntCellBits[kind])
        {
            void *value = dpvs->dynEntCellBits[kind];
            const std::uint64_t count = static_cast<std::uint64_t>(
                varGfxWorld->dpvsPlanes.cellCount) *
                dpvs->dynEntClientWordCount[kind];
            if (!LoadRuntimeBytes(value, count, sizeof(std::uint32_t), 3,
                "GfxWorld/dynamic entity cell bits"))
            {
                DB_PopStreamPos(); return false;
            }
            dpvs->dynEntCellBits[kind] = static_cast<std::uint32_t *>(value);
        }
        DB_PopStreamPos();
    }
    for (std::uint32_t slot = 0; slot < 3; ++slot)
    {
        for (std::uint32_t kind = 0; kind < 2; ++kind)
        {
            DB_PushStreamPos(1);
            if (dpvs->dynEntVisData[kind][slot])
            {
                void *value = dpvs->dynEntVisData[kind][slot];
                const std::uint64_t bytes = static_cast<std::uint64_t>(32u) *
                    dpvs->dynEntClientWordCount[kind];
                if (!LoadRuntimeBytes(value, bytes, 1, 15,
                    "GfxWorld/dynamic entity visibility"))
                {
                    DB_PopStreamPos(); return false;
                }
                dpvs->dynEntVisData[kind][slot] =
                    static_cast<std::uint8_t *>(value);
            }
            DB_PopStreamPos();
        }
    }
    return true;
}

bool LoadWorld(GfxWorld *world, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(world),
        sizeof(GfxWorld));
    if (DB_RuntimeGeneratedLoadFailed() ||
        !NonNegative(world->planeCount, "GfxWorld/negative plane count") ||
        !NonNegative(world->nodeCount, "GfxWorld/negative node count") ||
        !NonNegative(world->indexCount, "GfxWorld/negative index count") ||
        !NonNegative(world->surfaceCount, "GfxWorld/negative surface count") ||
        !NonNegative(world->skySurfCount, "GfxWorld/negative sky count") ||
        !NonNegative(world->cullGroupCount,
            "GfxWorld/negative cull group count") ||
        !NonNegative(world->lightmapCount,
            "GfxWorld/negative lightmap count") ||
        !NonNegative(world->modelCount, "GfxWorld/negative model count") ||
        !NonNegative(world->materialMemoryCount,
            "GfxWorld/negative material memory count")) return false;
    varGfxWorld = world;
    DB_PushStreamPos(4);
    varXString = &world->name;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed()) { DB_PopStreamPos(); return false; }
    varXString = &world->baseName;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed()) { DB_PopStreamPos(); return false; }
    if (world->indices && !LoadArray(world->indices,
        static_cast<std::uint32_t>(world->indexCount), 1,
        "GfxWorld/index array")) { DB_PopStreamPos(); return false; }
    if (world->skyStartSurfs && !LoadArray(world->skyStartSurfs,
        static_cast<std::uint32_t>(world->skySurfCount), 3,
        "GfxWorld/sky surfaces")) { DB_PopStreamPos(); return false; }
    varGfxImagePtr = &world->skyImage;
    Load_GfxImagePtr(false);
    if (DB_RuntimeGeneratedLoadFailed()) { DB_PopStreamPos(); return false; }
    if (world->sunLight)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            world->sunLight);
        if (token == UINT32_MAX)
        {
            world->sunLight = reinterpret_cast<GfxLight *>(
                DB_AllocStreamPos(3));
            if (!LoadGfxLight(world->sunLight, true))
            { DB_PopStreamPos(); return false; }
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &world->sunLight));
            if (DB_RuntimeGeneratedLoadFailed())
            { DB_PopStreamPos(); return false; }
        }
    }
    if (world->reflectionProbes)
    {
        if (!AllocateArray(world->reflectionProbes, world->reflectionProbeCount, 3,
            "GfxWorld/reflection probes") ||
            !LoadReflectionProbes(world->reflectionProbes,
                world->reflectionProbeCount))
        { DB_PopStreamPos(); return false; }
    }
    DB_PushStreamPos(1);
    if (world->reflectionProbeTextures)
    {
        void *value = world->reflectionProbeTextures;
        if (!LoadRuntimeBytes(value, world->reflectionProbeCount,
            sizeof(GfxTexture), 3, "GfxWorld/reflection probe textures"))
        { DB_PopStreamPos(); DB_PopStreamPos(); return false; }
        world->reflectionProbeTextures = static_cast<GfxTexture *>(value);
    }
    DB_PopStreamPos();
    if (!LoadDpvsPlanes(&world->dpvsPlanes))
    { DB_PopStreamPos(); return false; }
    if (world->cells)
    {
        if (!AllocateArray(world->cells,
            static_cast<std::uint32_t>(world->dpvsPlanes.cellCount), 3,
            "GfxWorld/cells") ||
            !LoadCells(world->cells,
                static_cast<std::uint32_t>(world->dpvsPlanes.cellCount)))
        { DB_PopStreamPos(); return false; }
    }
    if (world->lightmaps)
    {
        if (!AllocateArray(world->lightmaps,
            static_cast<std::uint32_t>(world->lightmapCount), 3,
            "GfxWorld/lightmaps") ||
            !LoadLightmaps(world->lightmaps,
                static_cast<std::uint32_t>(world->lightmapCount)))
        { DB_PopStreamPos(); return false; }
    }
    if (!LoadLightGrid(&world->lightGrid))
    { DB_PopStreamPos(); return false; }
    GfxTexture **textureArrays[2] = {&world->lightmapPrimaryTextures,
        &world->lightmapSecondaryTextures};
    for (GfxTexture **textures : textureArrays)
    {
        DB_PushStreamPos(1);
        if (*textures)
        {
            void *value = *textures;
            if (!LoadRuntimeBytes(value,
                static_cast<std::uint32_t>(world->lightmapCount),
                sizeof(GfxTexture), 3, "GfxWorld/lightmap textures"))
            { DB_PopStreamPos(); DB_PopStreamPos(); return false; }
            *textures = static_cast<GfxTexture *>(value);
        }
        DB_PopStreamPos();
    }
    if (world->models && !LoadArray(world->models,
        static_cast<std::uint32_t>(world->modelCount), 3,
        "GfxWorld/brush models")) { DB_PopStreamPos(); return false; }
    if (world->materialMemory)
    {
        if (!AllocateArray(world->materialMemory,
            static_cast<std::uint32_t>(world->materialMemoryCount), 3,
            "GfxWorld/material memory") ||
            !LoadMaterialMemory(world->materialMemory,
                static_cast<std::uint32_t>(world->materialMemoryCount)))
        { DB_PopStreamPos(); return false; }
    }
    if (world->vd.vertices && !LoadArray(world->vd.vertices,
        world->vertexCount, 3, "GfxWorld/vertices"))
    { DB_PopStreamPos(); return false; }
    world->vd.worldVb = nullptr;
    if (world->vld.data && !LoadArray(world->vld.data,
        world->vertexLayerDataSize, 0, "GfxWorld/vertex layers"))
    { DB_PopStreamPos(); return false; }
    world->vld.layerVb = nullptr;
    if (!LoadSunflare(&world->sun)) { DB_PopStreamPos(); return false; }
    varGfxImagePtr = &world->outdoorImage;
    Load_GfxImagePtr(false);
    if (DB_RuntimeGeneratedLoadFailed()) { DB_PopStreamPos(); return false; }
    DB_PushStreamPos(1);
    if (world->cellCasterBits)
    {
        void *value = world->cellCasterBits;
        const std::uint64_t wordsPerCell =
            (static_cast<std::uint32_t>(world->dpvsPlanes.cellCount) + 31u) >> 5u;
        if (!LoadRuntimeBytes(value,
            static_cast<std::uint64_t>(world->dpvsPlanes.cellCount) *
                wordsPerCell,
            sizeof(std::uint32_t), 3, "GfxWorld/cell caster bits"))
        { DB_PopStreamPos(); DB_PopStreamPos(); return false; }
        world->cellCasterBits = static_cast<std::uint32_t *>(value);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (world->sceneDynModel)
    {
        void *value = world->sceneDynModel;
        if (!LoadRuntimeBytes(value, world->dpvsDyn.dynEntClientCount[0],
            sizeof(GfxSceneDynModel), 3, "GfxWorld/dynamic model scene"))
        { DB_PopStreamPos(); DB_PopStreamPos(); return false; }
        world->sceneDynModel = static_cast<GfxSceneDynModel *>(value);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (world->sceneDynBrush)
    {
        void *value = world->sceneDynBrush;
        if (!LoadRuntimeBytes(value, world->dpvsDyn.dynEntClientCount[1],
            sizeof(GfxSceneDynBrush), 3, "GfxWorld/dynamic brush scene"))
        { DB_PopStreamPos(); DB_PopStreamPos(); return false; }
        world->sceneDynBrush = static_cast<GfxSceneDynBrush *>(value);
    }
    DB_PopStreamPos();
    if (world->primaryLightCount <= world->sunPrimaryLightIndex)
    {
        DB_RuntimeGeneratedFailure("GfxWorld/invalid primary light range");
        DB_PopStreamPos(); return false;
    }
    const std::uint64_t nonSunLights = world->primaryLightCount -
        (world->sunPrimaryLightIndex + 1u);
    DB_PushStreamPos(1);
    if (world->primaryLightEntityShadowVis)
    {
        void *value = world->primaryLightEntityShadowVis;
        if (!LoadRuntimeBytes(value, nonSunLights << 12u,
            sizeof(std::uint32_t), 3,
            "GfxWorld/primary light entity shadows"))
        { DB_PopStreamPos(); DB_PopStreamPos(); return false; }
        world->primaryLightEntityShadowVis = static_cast<std::uint32_t *>(value);
    }
    DB_PopStreamPos();
    for (std::uint32_t kind = 0; kind < 2; ++kind)
    {
        DB_PushStreamPos(1);
        if (world->primaryLightDynEntShadowVis[kind])
        {
            void *value = world->primaryLightDynEntShadowVis[kind];
            if (!LoadRuntimeBytes(value,
                static_cast<std::uint64_t>(
                    world->dpvsDyn.dynEntClientCount[kind]) * nonSunLights,
                sizeof(std::uint32_t), 3,
                "GfxWorld/primary light dynamic shadows"))
            { DB_PopStreamPos(); DB_PopStreamPos(); return false; }
            world->primaryLightDynEntShadowVis[kind] =
                static_cast<std::uint32_t *>(value);
        }
        DB_PopStreamPos();
    }
    DB_PushStreamPos(1);
    if (world->nonSunPrimaryLightForModelDynEnt)
    {
        void *value = world->nonSunPrimaryLightForModelDynEnt;
        if (!LoadRuntimeBytes(value, world->dpvsDyn.dynEntClientCount[0],
            1, 0, "GfxWorld/non-sun model lights"))
        { DB_PopStreamPos(); DB_PopStreamPos(); return false; }
        world->nonSunPrimaryLightForModelDynEnt =
            static_cast<std::uint8_t *>(value);
    }
    DB_PopStreamPos();
    if (world->shadowGeom)
    {
        if (!AllocateArray(world->shadowGeom, world->primaryLightCount, 3,
            "GfxWorld/shadow geometry") ||
            !LoadShadowGeometry(world->shadowGeom, world->primaryLightCount))
        { DB_PopStreamPos(); return false; }
    }
    if (world->lightRegion)
    {
        if (!AllocateArray(world->lightRegion, world->primaryLightCount, 3,
            "GfxWorld/light regions") ||
            !LoadLightRegions(world->lightRegion, world->primaryLightCount))
        { DB_PopStreamPos(); return false; }
    }
    if (!LoadDpvsStatic(&world->dpvs) || !LoadDpvsDynamic(&world->dpvsDyn))
    { DB_PopStreamPos(); return false; }
    DB_PopStreamPos();
    return true;
}
} // namespace

void __cdecl Load_GfxWorldPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varGfxWorldPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varGfxWorldPtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varGfxWorldPtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varGfxWorldPtr = reinterpret_cast<GfxWorld *>(
                AllocLoad_FxElemVisStateSample());
            varGfxWorld = *varGfxWorldPtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            if (LoadWorld(varGfxWorld, true))
                Load_GfxWorldAsset(reinterpret_cast<XAssetHeader *>(
                    varGfxWorldPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varGfxWorldPtr)->name);
                if (inserted) *inserted = *varGfxWorldPtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varGfxWorldPtr));
        }
    }
    DB_PopStreamPos();
}
