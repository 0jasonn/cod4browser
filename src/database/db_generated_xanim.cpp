#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>

#include <cstdint>
#include <limits>

XAnimParts *varXAnimParts = nullptr;
XAnimParts **varXAnimPartsPtr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical XAnimParts loader requires the IW3 32-bit ABI");
static_assert(sizeof(XAnimParts) == 88u);

template <typename T>
T *Alloc(int alignment)
{
    return reinterpret_cast<T *>(DB_AllocStreamPos(alignment));
}

bool LoadArray(void *&field, std::int64_t count, std::size_t stride,
    int alignment, const char *stage)
{
    if (!field) return true;
    if (count < 0 || count > (std::numeric_limits<std::int32_t>::max)() ||
        static_cast<std::uint64_t>(count) * stride >
            (std::numeric_limits<std::uint32_t>::max)())
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    const std::size_t bytes = static_cast<std::size_t>(count) * stride;
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    field = DB_AllocStreamPos(alignment);
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(field),
        static_cast<std::int32_t>(bytes));
    return !DB_RuntimeGeneratedLoadFailed();
}

void LoadDynamicIndices(XAnimDynamicIndices *indices, std::uint16_t size)
{
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(indices), 0);
    void *data = indices;
    LoadArray(data, static_cast<std::int64_t>(size) + 1,
        varXAnimParts->numframes >= 0x100u ? 2u : 1u,
        varXAnimParts->numframes >= 0x100u ? 1 : 0,
        "XAnim/dynamic indices");
}

void LoadDeltaQuat(XAnimDeltaPartQuat *quat)
{
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(quat), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (!quat->size)
    {
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(&quat->u), 4);
        return;
    }
    XAnimDeltaPartQuatDataFrames *frames = &quat->u.frames;
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(frames), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    LoadDynamicIndices(&frames->indices, quat->size);
    if (DB_RuntimeGeneratedLoadFailed() || !frames->frames) return;
    void *frameData = frames->frames;
    if (LoadArray(frameData, static_cast<std::int64_t>(quat->size) + 1, 4u,
        3, "XAnim/delta quaternion frames"))
        frames->frames = static_cast<std::int16_t (*)[2]>(frameData);
}

void LoadDeltaTrans(XAnimPartTrans *trans)
{
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(trans), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (!trans->size)
    {
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(&trans->u), 12);
        return;
    }
    XAnimPartTransFrames *frames = &trans->u.frames;
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(frames), 28);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    LoadDynamicIndices(&frames->indices, trans->size);
    if (DB_RuntimeGeneratedLoadFailed() || !frames->frames._1) return;
    void *frameData = frames->frames._1;
    if (LoadArray(frameData, static_cast<std::int64_t>(trans->size) + 1,
        trans->smallTrans ? 3u : 6u, trans->smallTrans ? 0 : 3,
        "XAnim/delta translation frames"))
        frames->frames._1 = static_cast<std::uint8_t (*)[3]>(frameData);
}

void LoadDeltaPart(XAnimDeltaPart *delta)
{
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(delta), sizeof(*delta));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (delta->trans)
    {
        delta->trans = Alloc<XAnimPartTrans>(3);
        LoadDeltaTrans(delta->trans);
    }
    if (DB_RuntimeGeneratedLoadFailed() || !delta->quat) return;
    delta->quat = Alloc<XAnimDeltaPartQuat>(3);
    LoadDeltaQuat(delta->quat);
}

void LoadXAnimParts(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varXAnimParts),
        sizeof(XAnimParts));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varXAnimParts->name;
    Load_XString(false);

    if (!DB_RuntimeGeneratedLoadFailed() && varXAnimParts->names)
    {
        varXAnimParts->names = Alloc<std::uint16_t>(1);
        varScriptString = varXAnimParts->names;
        Load_ScriptStringArray(true, varXAnimParts->boneCount[9]);
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varXAnimParts->notify)
    {
        void *notify = varXAnimParts->notify;
        if (LoadArray(notify, varXAnimParts->notifyCount,
            sizeof(XAnimNotifyInfo), 3, "XAnim/notifies"))
        {
            varXAnimParts->notify = static_cast<XAnimNotifyInfo *>(notify);
            for (std::uint8_t index = 0; index < varXAnimParts->notifyCount;
                ++index)
                Load_ScriptStringCustom(&varXAnimParts->notify[index].name);
        }
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varXAnimParts->deltaPart)
    {
        varXAnimParts->deltaPart = Alloc<XAnimDeltaPart>(3);
        LoadDeltaPart(varXAnimParts->deltaPart);
    }

#define LOAD_XANIM_ARRAY(field, count, stride, alignment, stage) \
    do { if (!DB_RuntimeGeneratedLoadFailed()) { \
        void *value = varXAnimParts->field; \
        if (LoadArray(value, varXAnimParts->count, stride, alignment, stage)) \
            varXAnimParts->field = reinterpret_cast<decltype(varXAnimParts->field)>(value); \
    } } while (false)
    LOAD_XANIM_ARRAY(dataByte, dataByteCount, 1u, 0, "XAnim/data bytes");
    LOAD_XANIM_ARRAY(dataShort, dataShortCount, 2u, 1, "XAnim/data shorts");
    LOAD_XANIM_ARRAY(dataInt, dataIntCount, 4u, 3, "XAnim/data ints");
    LOAD_XANIM_ARRAY(randomDataShort, randomDataShortCount, 2u, 1,
        "XAnim/random shorts");
    LOAD_XANIM_ARRAY(randomDataByte, randomDataByteCount, 1u, 0,
        "XAnim/random bytes");
    LOAD_XANIM_ARRAY(randomDataInt, randomDataIntCount, 4u, 3,
        "XAnim/random ints");
#undef LOAD_XANIM_ARRAY

    if (!DB_RuntimeGeneratedLoadFailed() && varXAnimParts->indices.data)
    {
        void *indices = varXAnimParts->indices.data;
        if (LoadArray(indices, varXAnimParts->indexCount,
            varXAnimParts->numframes >= 0x100u ? 2u : 1u,
            varXAnimParts->numframes >= 0x100u ? 1 : 0,
            "XAnim/indices"))
            varXAnimParts->indices.data = indices;
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_XAnimPartsPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varXAnimPartsPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varXAnimPartsPtr)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            *varXAnimPartsPtr);
        if (token == UINT32_MAX || token == UINT32_MAX - 1u)
        {
            *varXAnimPartsPtr = Alloc<XAnimParts>(3);
            varXAnimParts = *varXAnimPartsPtr;
            const void **inserted = token == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            LoadXAnimParts(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_XAnimPartsAsset(reinterpret_cast<XAssetHeader *>(
                    varXAnimPartsPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varXAnimPartsPtr)->name);
                if (inserted) *inserted = *varXAnimPartsPtr;
            }
        }
        else DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
            varXAnimPartsPtr));
    }
    DB_PopStreamPos();
}
