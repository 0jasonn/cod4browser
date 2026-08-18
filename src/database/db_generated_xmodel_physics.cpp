#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_xmodel_internal.h>
#include <database/db_runtime_prefix.h>
#include <physics/phys_geom_types.h>
#include <xanim/xmodel_types.h>

#include <cstdint>
#include <limits>

namespace
{
bool CheckedArray(std::int64_t count, std::size_t stride, const char *stage,
    std::size_t &bytes)
{
    if (count < 0 || count > (std::numeric_limits<std::int32_t>::max)() ||
        static_cast<std::uint64_t>(count) * stride >
            (std::numeric_limits<std::uint32_t>::max)())
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
T *Alloc(int alignment)
{
    return reinterpret_cast<T *>(DB_AllocStreamPos(alignment));
}

void LoadBrushSide(cbrushside_t &side)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(&side), sizeof(side));
    if (!side.plane || DB_RuntimeGeneratedLoadFailed()) return;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(side.plane);
    if (token == UINT32_MAX)
    {
        side.plane = Alloc<cplane_s>(3);
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(side.plane),
            sizeof(cplane_s));
    }
    else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
        &side.plane));
}

void LoadBrushWrapper(BrushWrapper &brush)
{
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(&brush), sizeof(brush));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (brush.sides)
    {
        std::size_t bytes = 0;
        if (!CheckedArray(brush.numsides, sizeof(cbrushside_t),
            "XModel/phys brush sides", bytes)) return;
        brush.sides = Alloc<cbrushside_t>(3);
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(brush.sides),
            static_cast<std::int32_t>(bytes));
        if (DB_RuntimeGeneratedLoadFailed()) return;
        for (std::uint32_t index = 0; index < brush.numsides; ++index)
        {
            LoadBrushSide(brush.sides[index]);
            if (DB_RuntimeGeneratedLoadFailed()) return;
        }
    }
    if (brush.baseAdjacentSide)
    {
        std::size_t bytes = 0;
        if (!CheckedArray(brush.totalEdgeCount, 1u,
            "XModel/phys brush adjacent sides", bytes)) return;
        brush.baseAdjacentSide = Alloc<std::uint8_t>(0);
        Load_Stream(true, brush.baseAdjacentSide,
            static_cast<std::int32_t>(bytes));
    }
    if (DB_RuntimeGeneratedLoadFailed() || !brush.planes) return;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(brush.planes);
    if (token == UINT32_MAX)
    {
        std::size_t bytes = 0;
        if (!CheckedArray(brush.numsides, sizeof(cplane_s),
            "XModel/phys brush planes", bytes)) return;
        brush.planes = Alloc<cplane_s>(3);
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(brush.planes),
            static_cast<std::int32_t>(bytes));
    }
    else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
        &brush.planes));
}

void LoadPhysGeomInfo(PhysGeomInfo &geom)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(&geom), sizeof(geom));
    if (!geom.brush || DB_RuntimeGeneratedLoadFailed()) return;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(geom.brush);
    if (token == UINT32_MAX)
    {
        geom.brush = Alloc<BrushWrapper>(3);
        LoadBrushWrapper(*geom.brush);
    }
    else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
        &geom.brush));
}
} // namespace

void DB_LoadGeneratedXModelCollSurfArray(XModelCollSurf_s *surfaces,
    std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedArray(count, sizeof(XModelCollSurf_s),
        "XModel/collision surfaces", bytes)) return;
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(surfaces),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    for (std::int32_t index = 0; index < count; ++index)
    {
        XModelCollSurf_s &surface = surfaces[index];
        Load_Stream(false, reinterpret_cast<std::uint8_t *>(&surface),
            sizeof(surface));
        if (!surface.collTris) continue;
        if (!CheckedArray(surface.numCollTris, sizeof(XModelCollTri_s),
            "XModel/collision triangles", bytes)) return;
        surface.collTris = Alloc<XModelCollTri_s>(3);
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(surface.collTris),
            static_cast<std::int32_t>(bytes));
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void DB_LoadGeneratedPhysGeomList(PhysGeomList *list, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(list),
        sizeof(*list));
    if (DB_RuntimeGeneratedLoadFailed() || !list->geoms) return;
    std::size_t bytes = 0;
    if (!CheckedArray(list->count, sizeof(PhysGeomInfo),
        "XModel/physical geometries", bytes)) return;
    list->geoms = Alloc<PhysGeomInfo>(3);
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(list->geoms),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    for (std::uint32_t index = 0; index < list->count; ++index)
    {
        LoadPhysGeomInfo(list->geoms[index]);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}
