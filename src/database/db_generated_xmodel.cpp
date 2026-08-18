#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_generated_xmodel_internal.h>
#include <database/db_runtime_prefix.h>
#include <gfx_d3d/material_types.h>
#include <physics/phys_geom_types.h>
#include <xanim/xsurface_types.h>

#include <cstdint>
#include <limits>

XModel *varXModel = nullptr;
XModel **varXModelPtr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical XModel loader requires the IW3 32-bit ABI");
static_assert(sizeof(XModel) == 220u);

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

template <typename T>
bool LoadInlineOrPointer(T *&pointer, std::int32_t count, int alignment,
    const char *stage)
{
    if (!pointer) return true;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(pointer);
    if (token != UINT32_MAX)
    {
        DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(&pointer));
        return !DB_RuntimeGeneratedLoadFailed();
    }
    std::size_t bytes = 0;
    if (!CheckedArray(count, sizeof(T), stage, bytes)) return false;
    pointer = Alloc<T>(alignment);
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(pointer),
        static_cast<std::int32_t>(bytes));
    return !DB_RuntimeGeneratedLoadFailed();
}

void LoadXModel(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varXModel),
        sizeof(XModel));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);

    varXString = &varXModel->name;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed()) { DB_PopStreamPos(); return; }
    if (varXModel->numRootBones > varXModel->numBones)
    {
        DB_RuntimeGeneratedFailure("XModel/invalid root bone count");
        DB_PopStreamPos();
        return;
    }
    const std::int32_t childBones =
        varXModel->numBones - varXModel->numRootBones;

    if (varXModel->boneNames)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            varXModel->boneNames);
        if (token == UINT32_MAX)
        {
            varXModel->boneNames = Alloc<std::uint16_t>(1);
            varScriptString = varXModel->boneNames;
            Load_ScriptStringArray(true, varXModel->numBones);
        }
        else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
            &varXModel->boneNames));
    }
    if (!DB_RuntimeGeneratedLoadFailed() && !LoadInlineOrPointer(
        varXModel->parentList, childBones, 0, "XModel/parent list")) {}
    if (!DB_RuntimeGeneratedLoadFailed() && !LoadInlineOrPointer(
        varXModel->quats, 4 * childBones, 1, "XModel/bone quaternions")) {}
    if (!DB_RuntimeGeneratedLoadFailed() && !LoadInlineOrPointer(
        varXModel->trans, 4 * childBones, 3, "XModel/bone translations")) {}
    if (!DB_RuntimeGeneratedLoadFailed() && !LoadInlineOrPointer(
        varXModel->partClassification, varXModel->numBones, 0,
        "XModel/part classification")) {}
    if (!DB_RuntimeGeneratedLoadFailed() && !LoadInlineOrPointer(
        varXModel->baseMat, varXModel->numBones, 3, "XModel/base matrices")) {}

    if (!DB_RuntimeGeneratedLoadFailed() && varXModel->surfs)
    {
        varXModel->surfs = Alloc<XSurface>(3);
        DB_LoadGeneratedXSurfaceArray(varXModel->surfs, varXModel->numsurfs);
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varXModel->materialHandles)
    {
        varXModel->materialHandles = Alloc<Material *>(3);
        varMaterialHandle = varXModel->materialHandles;
        Load_MaterialHandleArrayGenerated(true, varXModel->numsurfs);
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varXModel->collSurfs)
    {
        varXModel->collSurfs = Alloc<XModelCollSurf_s>(3);
        DB_LoadGeneratedXModelCollSurfArray(varXModel->collSurfs,
            varXModel->numCollSurfs);
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varXModel->boneInfo)
    {
        std::size_t bytes = 0;
        if (CheckedArray(varXModel->numBones, sizeof(XBoneInfo),
            "XModel/bone info", bytes))
        {
            varXModel->boneInfo = Alloc<XBoneInfo>(3);
            Load_Stream(true, reinterpret_cast<std::uint8_t *>(
                varXModel->boneInfo), static_cast<std::int32_t>(bytes));
        }
    }
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varPhysPresetPtr = &varXModel->physPreset;
        Load_PhysPresetPtrGenerated(false);
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varXModel->physGeoms)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            varXModel->physGeoms);
        if (token == UINT32_MAX)
        {
            varXModel->physGeoms = Alloc<PhysGeomList>(3);
            DB_LoadGeneratedPhysGeomList(varXModel->physGeoms, true);
        }
        else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
            &varXModel->physGeoms));
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_XModelPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varXModelPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varXModelPtr)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            *varXModelPtr);
        if (token == UINT32_MAX || token == UINT32_MAX - 1u)
        {
            *varXModelPtr = Alloc<XModel>(3);
            varXModel = *varXModelPtr;
            const void **inserted = token == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            LoadXModel(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_XModelAsset(reinterpret_cast<XAssetHeader *>(varXModelPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varXModelPtr)->name);
                if (inserted) *inserted = *varXModelPtr;
            }
        }
        else DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
            varXModelPtr));
    }
    DB_PopStreamPos();
}

void __cdecl Load_XModelPtrArray(bool atStreamStart, std::int32_t count)
{
    if (count < 0 || static_cast<std::uint64_t>(count) * sizeof(XModel *) >
        (std::numeric_limits<std::uint32_t>::max)())
    {
        DB_RuntimeGeneratedFailure("XModel/pointer array");
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(count) *
        sizeof(XModel *);
    if (atStreamStart && !DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure("XModel/pointer array");
        return;
    }
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varXModelPtr),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    XModel **entry = varXModelPtr;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varXModelPtr = entry;
        Load_XModelPtr(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}
