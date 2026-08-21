#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_registry_publication.h>
#include <database/db_runtime_prefix.h>
#include <DynEntity/DynEntity_client.h>
#include <qcommon/cm_types.h>
#include <xanim/xmodel.h>

#include <cstdint>
#include <limits>

clipMap_t *varclipMap_t = nullptr;
clipMap_t **varclipMap_ptr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical ClipMap loader requires the IW3 32-bit ABI");
static_assert(sizeof(clipMap_t) == 284u);
static_assert(sizeof(DynEntityDef) == 96u);

template <typename T>
T *Alloc(std::uint32_t alignmentMask = 3u)
{
    return reinterpret_cast<T *>(DB_AllocStreamPos(alignmentMask));
}

bool CheckedBytes(std::int64_t count, std::size_t stride,
    const char *stage, std::size_t &bytes)
{
    if (count < 0 || static_cast<std::uint64_t>(count) * stride >
        static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)()))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    bytes = static_cast<std::size_t>(count) * stride;
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    return true;
}

template <typename T>
bool LoadPlainArray(T *&field, std::int64_t count, std::uint32_t alignmentMask,
    const char *stage)
{
    std::size_t bytes = 0;
    if (!CheckedBytes(count, sizeof(T), stage, bytes)) return false;
    field = Alloc<T>(alignmentMask);
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(field),
        static_cast<std::int32_t>(bytes));
    return !DB_RuntimeGeneratedLoadFailed();
}

void LoadPlanePointer(cplane_s *&plane)
{
    if (!plane) return;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(plane);
    if (token == UINT32_MAX)
    {
        plane = Alloc<cplane_s>();
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(plane),
            sizeof(cplane_s));
    }
    else
    {
        DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(&plane));
    }
}

void LoadStaticModels(cStaticModel_s *&models, std::uint32_t count)
{
    if (!LoadPlainArray(models, count, 3, "ClipMap/static models")) return;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        Load_Stream(false, reinterpret_cast<std::uint8_t *>(&models[index]),
            sizeof(cStaticModel_s));
        varXModelPtr = &models[index].xmodel;
        Load_XModelPtr(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadBrushSides(cbrushside_t *&sides, std::uint32_t count)
{
    if (!LoadPlainArray(sides, count, 3, "ClipMap/brush sides")) return;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        Load_Stream(false, reinterpret_cast<std::uint8_t *>(&sides[index]),
            sizeof(cbrushside_t));
        LoadPlanePointer(sides[index].plane);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadNodes(cNode_t *&nodes, std::uint32_t count)
{
    if (!LoadPlainArray(nodes, count, 3, "ClipMap/nodes")) return;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        Load_Stream(false, reinterpret_cast<std::uint8_t *>(&nodes[index]),
            sizeof(cNode_t));
        LoadPlanePointer(nodes[index].plane);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadLeafBrushNodes(cLeafBrushNode_s *&nodes, std::uint32_t count)
{
    if (!LoadPlainArray(nodes, count, 3, "ClipMap/leaf brush nodes")) return;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        cLeafBrushNode_s &node = nodes[index];
        Load_Stream(false, &node.axis, sizeof(cLeafBrushNode_s));
        if (node.leafBrushCount <= 0 || !node.data.leaf.brushes) continue;
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            node.data.leaf.brushes);
        if (token == UINT32_MAX)
        {
            if (!LoadPlainArray(node.data.leaf.brushes, node.leafBrushCount, 1,
                "ClipMap/leaf brush node brushes")) return;
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &node.data.leaf.brushes));
        }
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadPartitions(CollisionPartition *&partitions, std::int32_t count)
{
    if (!LoadPlainArray(partitions, count, 3, "ClipMap/partitions")) return;
    for (std::int32_t index = 0; index < count; ++index)
    {
        CollisionPartition &partition = partitions[index];
        Load_Stream(false, &partition.triCount, sizeof(CollisionPartition));
        if (!partition.borders) continue;
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            partition.borders);
        if (token == UINT32_MAX)
        {
            partition.borders = Alloc<CollisionBorder>();
            Load_Stream(true, reinterpret_cast<std::uint8_t *>(
                partition.borders), sizeof(CollisionBorder));
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &partition.borders));
        }
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadBrush(cbrush_t &brush, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(&brush),
        sizeof(cbrush_t));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (brush.sides)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            brush.sides);
        if (token == UINT32_MAX)
        {
            brush.sides = Alloc<cbrushside_t>();
            Load_Stream(true, reinterpret_cast<std::uint8_t *>(brush.sides),
                sizeof(cbrushside_t));
            if (!DB_RuntimeGeneratedLoadFailed())
                LoadPlanePointer(brush.sides->plane);
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &brush.sides));
        }
    }
    if (DB_RuntimeGeneratedLoadFailed() || !brush.baseAdjacentSide) return;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
        brush.baseAdjacentSide);
    if (token == UINT32_MAX)
    {
        brush.baseAdjacentSide = Alloc<std::uint8_t>(0);
        Load_Stream(true, brush.baseAdjacentSide, 1);
    }
    else
    {
        DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
            &brush.baseAdjacentSide));
    }
}

void LoadBrushes(cbrush_t *&brushes, std::uint16_t count)
{
    if (!LoadPlainArray(brushes, count, 15, "ClipMap/brushes")) return;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        LoadBrush(brushes[index], false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadXModelPiecesPtr(XModelPieces *&pieces)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(&pieces),
        sizeof(XModelPieces *));
    if (!pieces || DB_RuntimeGeneratedLoadFailed()) return;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(pieces);
    if (token != UINT32_MAX)
    {
        DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(&pieces));
        return;
    }
    pieces = Alloc<XModelPieces>();
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(pieces),
        sizeof(XModelPieces));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &pieces->name;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed() || !pieces->pieces) return;
    std::size_t bytes = 0;
    if (!CheckedBytes(pieces->numpieces, sizeof(XModelPiece),
        "ClipMap/dynent destruction pieces", bytes)) return;
    pieces->pieces = Alloc<XModelPiece>();
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(pieces->pieces),
        static_cast<std::int32_t>(bytes));
    for (std::int32_t index = 0; index < pieces->numpieces; ++index)
    {
        Load_Stream(false, reinterpret_cast<std::uint8_t *>(
            &pieces->pieces[index]), sizeof(XModelPiece));
        varXModelPtr = &pieces->pieces[index].model;
        Load_XModelPtr(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadDynEntityDefs(DynEntityDef *&defs, std::uint16_t count)
{
    if (!LoadPlainArray(defs, count, 3, "ClipMap/dynamic entity defs")) return;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        DynEntityDef &def = defs[index];
        Load_Stream(false, reinterpret_cast<std::uint8_t *>(&def),
            sizeof(DynEntityDef));
        varXModelPtr = &def.xModel;
        Load_XModelPtr(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
        varFxEffectDefHandle = &def.destroyFx;
        Load_FxEffectDefHandle(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
        LoadXModelPiecesPtr(def.destroyPieces);
        if (DB_RuntimeGeneratedLoadFailed()) return;
        varPhysPresetPtr = &def.physPreset;
        Load_PhysPresetPtrGenerated(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

template <typename T>
void LoadRuntimeArray(T *&field, std::uint16_t count, const char *stage)
{
    DB_PushStreamPos(1);
    if (field) LoadPlainArray(field, count, 3, stage);
    DB_PopStreamPos();
}

void LoadClipMap(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varclipMap_t),
        sizeof(clipMap_t));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varclipMap_t->name;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed()) { DB_PopStreamPos(); return; }

    if (varclipMap_t->planes)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            varclipMap_t->planes);
        if (token == UINT32_MAX)
            LoadPlainArray(varclipMap_t->planes, varclipMap_t->planeCount, 3,
                "ClipMap/planes");
        else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
            &varclipMap_t->planes));
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->staticModelList)
        LoadStaticModels(varclipMap_t->staticModelList,
            varclipMap_t->numStaticModels);
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->materials)
        LoadPlainArray(varclipMap_t->materials, varclipMap_t->numMaterials, 3,
            "ClipMap/materials");
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->brushsides)
        LoadBrushSides(varclipMap_t->brushsides, varclipMap_t->numBrushSides);
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->brushEdges)
        LoadPlainArray(varclipMap_t->brushEdges, varclipMap_t->numBrushEdges, 0,
            "ClipMap/brush edges");
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->nodes)
        LoadNodes(varclipMap_t->nodes, varclipMap_t->numNodes);
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->leafs)
        LoadPlainArray(varclipMap_t->leafs, varclipMap_t->numLeafs, 3,
            "ClipMap/leafs");
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->leafbrushes)
        LoadPlainArray(varclipMap_t->leafbrushes,
            varclipMap_t->numLeafBrushes, 1, "ClipMap/leaf brushes");
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->leafbrushNodes)
        LoadLeafBrushNodes(varclipMap_t->leafbrushNodes,
            varclipMap_t->leafbrushNodesCount);
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->leafsurfaces)
        LoadPlainArray(varclipMap_t->leafsurfaces,
            varclipMap_t->numLeafSurfaces, 3, "ClipMap/leaf surfaces");
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->verts)
        LoadPlainArray(varclipMap_t->verts, varclipMap_t->vertCount, 3,
            "ClipMap/vertices");
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->triIndices)
        LoadPlainArray(varclipMap_t->triIndices,
            static_cast<std::int64_t>(varclipMap_t->triCount) * 3, 1,
            "ClipMap/triangle indices");
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->triEdgeIsWalkable)
    {
        const std::int64_t edgeCount =
            static_cast<std::int64_t>(varclipMap_t->triCount) * 3;
        const std::int64_t bytes = edgeCount < 0 ? -1
            : 4 * ((edgeCount + 31) >> 5);
        LoadPlainArray(varclipMap_t->triEdgeIsWalkable, bytes, 0,
            "ClipMap/walkable triangle bits");
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->borders)
        LoadPlainArray(varclipMap_t->borders, varclipMap_t->borderCount, 3,
            "ClipMap/borders");
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->partitions)
        LoadPartitions(varclipMap_t->partitions, varclipMap_t->partitionCount);
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->aabbTrees)
        LoadPlainArray(varclipMap_t->aabbTrees, varclipMap_t->aabbTreeCount, 3,
            "ClipMap/aabb trees");
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->cmodels)
        LoadPlainArray(varclipMap_t->cmodels, varclipMap_t->numSubModels, 3,
            "ClipMap/submodels");
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->brushes)
        LoadBrushes(varclipMap_t->brushes, varclipMap_t->numBrushes);
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->visibility)
        LoadPlainArray(varclipMap_t->visibility,
            static_cast<std::int64_t>(varclipMap_t->numClusters) *
                varclipMap_t->clusterBytes, 0, "ClipMap/visibility");
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varMapEntsPtr = &varclipMap_t->mapEnts;
        Load_MapEntsPtr(false);
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->box_brush)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            varclipMap_t->box_brush);
        if (token == UINT32_MAX)
        {
            varclipMap_t->box_brush = Alloc<cbrush_t>(15);
            LoadBrush(*varclipMap_t->box_brush, true);
        }
        else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
            &varclipMap_t->box_brush));
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->dynEntDefList[0])
        LoadDynEntityDefs(varclipMap_t->dynEntDefList[0],
            varclipMap_t->dynEntCount[0]);
    if (!DB_RuntimeGeneratedLoadFailed() && varclipMap_t->dynEntDefList[1])
        LoadDynEntityDefs(varclipMap_t->dynEntDefList[1],
            varclipMap_t->dynEntCount[1]);

    if (!DB_RuntimeGeneratedLoadFailed()) LoadRuntimeArray(
        varclipMap_t->dynEntPoseList[0], varclipMap_t->dynEntCount[0],
        "ClipMap/dynent model poses");
    if (!DB_RuntimeGeneratedLoadFailed()) LoadRuntimeArray(
        varclipMap_t->dynEntPoseList[1], varclipMap_t->dynEntCount[1],
        "ClipMap/dynent brush poses");
    if (!DB_RuntimeGeneratedLoadFailed()) LoadRuntimeArray(
        varclipMap_t->dynEntClientList[0], varclipMap_t->dynEntCount[0],
        "ClipMap/dynent model clients");
    if (!DB_RuntimeGeneratedLoadFailed()) LoadRuntimeArray(
        varclipMap_t->dynEntClientList[1], varclipMap_t->dynEntCount[1],
        "ClipMap/dynent brush clients");
    if (!DB_RuntimeGeneratedLoadFailed()) LoadRuntimeArray(
        varclipMap_t->dynEntCollList[0], varclipMap_t->dynEntCount[0],
        "ClipMap/dynent model collision");
    if (!DB_RuntimeGeneratedLoadFailed()) LoadRuntimeArray(
        varclipMap_t->dynEntCollList[1], varclipMap_t->dynEntCount[1],
        "ClipMap/dynent brush collision");
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_clipMap_ptr(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varclipMap_ptr),
        sizeof(clipMap_t *));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varclipMap_ptr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varclipMap_ptr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varclipMap_ptr = Alloc<clipMap_t>();
            varclipMap_t = *varclipMap_ptr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            LoadClipMap(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_ClipMapAsset(reinterpret_cast<XAssetHeader *>(
                    varclipMap_ptr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varclipMap_ptr)->name);
                if (inserted) *inserted = *varclipMap_ptr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varclipMap_ptr));
            // A top-level XAsset can legally alias a ClipMap loaded by an
            // earlier stream. Native DB state already has an entry for that
            // alias, but a web zone replacement may have retired the entry
            // while the canonical pointer remains reachable. Rebind the
            // existing engine-owned singleton only when the name has no
            // registry entry; existing aliases keep native no-publication
            // semantics and are not duplicated.
            if (!DB_RuntimeGeneratedLoadFailed() && *varclipMap_ptr &&
                (*varclipMap_ptr)->name)
            {
#if defined(KISAK_MP)
                constexpr XAssetType clipMapType = ASSET_TYPE_CLIPMAP_PVS;
#else
                constexpr XAssetType clipMapType = ASSET_TYPE_CLIPMAP;
#endif
                if (!DB_FindXAssetEntryCanonical(clipMapType,
                    (*varclipMap_ptr)->name))
                    Load_ClipMapAsset(reinterpret_cast<XAssetHeader *>(
                        varclipMap_ptr));
            }
        }
    }
    DB_PopStreamPos();
}
