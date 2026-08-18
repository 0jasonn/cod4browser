#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_xmodel_internal.h>
#include <database/db_runtime_prefix.h>
#include <xanim/xsurface_types.h>

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

void LoadCollisionTree(XSurfaceCollisionTree *tree)
{
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(tree), sizeof(*tree));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (tree->nodes)
    {
        std::size_t bytes = 0;
        if (!CheckedArray(tree->nodeCount, sizeof(XSurfaceCollisionNode),
            "XModel/surface collision nodes", bytes)) return;
        tree->nodes = Alloc<XSurfaceCollisionNode>(15);
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(tree->nodes),
            static_cast<std::int32_t>(bytes));
    }
    if (DB_RuntimeGeneratedLoadFailed() || !tree->leafs) return;
    std::size_t bytes = 0;
    if (!CheckedArray(tree->leafCount, sizeof(XSurfaceCollisionLeaf),
        "XModel/surface collision leafs", bytes)) return;
    tree->leafs = Alloc<XSurfaceCollisionLeaf>(1);
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(tree->leafs),
        static_cast<std::int32_t>(bytes));
}

void LoadRigidVertList(XRigidVertList &list)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(&list), sizeof(list));
    if (!list.collisionTree || DB_RuntimeGeneratedLoadFailed()) return;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
        list.collisionTree);
    if (token == UINT32_MAX)
    {
        list.collisionTree = Alloc<XSurfaceCollisionTree>(3);
        LoadCollisionTree(list.collisionTree);
    }
    else
    {
        DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
            &list.collisionTree));
    }
}

void LoadSurface(XSurface &surface)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(&surface),
        sizeof(surface));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    Load_GetCurrentZoneHandle(&surface.zoneHandle);

    if (surface.vertInfo.vertsBlend)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            surface.vertInfo.vertsBlend);
        if (token == UINT32_MAX)
        {
            const std::int64_t count =
                7ll * surface.vertInfo.vertCount[3] +
                5ll * surface.vertInfo.vertCount[2] +
                3ll * surface.vertInfo.vertCount[1] +
                surface.vertInfo.vertCount[0];
            std::size_t bytes = 0;
            if (!CheckedArray(count, sizeof(std::uint16_t),
                "XModel/surface blend vertices", bytes)) return;
            surface.vertInfo.vertsBlend = Alloc<std::uint16_t>(1);
            Load_Stream(true, reinterpret_cast<std::uint8_t *>(
                surface.vertInfo.vertsBlend), static_cast<std::int32_t>(bytes));
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &surface.vertInfo.vertsBlend));
        }
    }

    DB_PushStreamPos(7);
    if (!DB_RuntimeGeneratedLoadFailed() && surface.verts0)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            surface.verts0);
        if (token == UINT32_MAX)
        {
            std::size_t bytes = 0;
            if (CheckedArray(surface.vertCount, sizeof(GfxPackedVertex),
                "XModel/surface vertices", bytes))
            {
                surface.verts0 = Alloc<GfxPackedVertex>(15);
                Load_Stream(true, reinterpret_cast<std::uint8_t *>(surface.verts0),
                    static_cast<std::int32_t>(bytes));
            }
        }
        else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
            &surface.verts0));
    }
    DB_PopStreamPos();
    if (DB_RuntimeGeneratedLoadFailed()) return;

    if (surface.vertList)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            surface.vertList);
        if (token == UINT32_MAX)
        {
            std::size_t bytes = 0;
            if (!CheckedArray(surface.vertListCount, sizeof(XRigidVertList),
                "XModel/rigid vertex lists", bytes)) return;
            surface.vertList = Alloc<XRigidVertList>(3);
            Load_Stream(true, reinterpret_cast<std::uint8_t *>(surface.vertList),
                static_cast<std::int32_t>(bytes));
            if (DB_RuntimeGeneratedLoadFailed()) return;
            for (std::uint32_t index = 0; index < surface.vertListCount; ++index)
            {
                LoadRigidVertList(surface.vertList[index]);
                if (DB_RuntimeGeneratedLoadFailed()) return;
            }
        }
        else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
            &surface.vertList));
    }

    DB_PushStreamPos(8);
    if (!DB_RuntimeGeneratedLoadFailed() && surface.triIndices)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            surface.triIndices);
        if (token == UINT32_MAX)
        {
            std::size_t bytes = 0;
            if (CheckedArray(3ll * surface.triCount, sizeof(std::uint16_t),
                "XModel/surface indices", bytes))
            {
                surface.triIndices = Alloc<std::uint16_t>(15);
                Load_Stream(true, reinterpret_cast<std::uint8_t *>(
                    surface.triIndices), static_cast<std::int32_t>(bytes));
            }
        }
        else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
            &surface.triIndices));
    }
    DB_PopStreamPos();
}
} // namespace

void DB_LoadGeneratedXSurfaceArray(XSurface *surfaces, std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedArray(count, sizeof(XSurface), "XModel/surfaces", bytes)) return;
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(surfaces),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    for (std::int32_t index = 0; index < count; ++index)
    {
        LoadSurface(surfaces[index]);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}
