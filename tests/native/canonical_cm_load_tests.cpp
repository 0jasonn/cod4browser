#include <database/database.h>
#include <database/db_registry_pools.h>
#include <qcommon/cm_types.h>
#include <qcommon/com_world_runtime.h>
#include <qcommon/com_world_types.h>
#include <qcommon/engine_lifecycle_trace.h>
#include <qcommon/qcommon.h>
#include <qcommon/thread_context.h>
#include <universal/dvar.h>
#include <universal/com_memory.h>
#include <universal/q_shared.h>
#include <gfx_d3d/gfx_world_types.h>
#include <game/savememory.h>
#include <script/scr_main.h>
#include <script/scr_variable.h>
#include <script/scr_vm_runtime.h>
#include <xanim/dobj_runtime_init.h>
#include <xanim/xanim_runtime_init.h>

#include <array>
#include <cassert>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

TraceThreadInfo g_traceThreadInfo[THREAD_CONTEXT_COUNT]{};
GfxWorld s_world{};

namespace
{
alignas(4096) std::array<std::byte, 0x00a00000u> g_hunk{};
std::vector<std::string> g_lifecycle;
std::string g_lookupName;
std::uint32_t g_assertCount = 0;
std::uint32_t g_loadObjCount = 0;
dvar_t g_logScriptTimes{};

void ObserveLifecycle(const EngineLifecycleTraceEvent &event, void *)
{
    g_lifecycle.emplace_back(EngineLifecycleStageName(event.stage));
    assert(event.name);
    if (event.stage == EngineLifecycleStage::CollisionLoadBegin ||
        event.stage == EngineLifecycleStage::CollisionLoadComplete)
    {
        assert(std::strcmp(event.name, "maps/killhouse.d3dbsp") == 0);
    }
}
} // namespace

const dvar_t *useFastFile = nullptr;

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    ++g_assertCount;
}

void QDECL Com_Error(errorParm_t, const char *format, ...)
{
    char message[256]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    throw std::runtime_error(message);
}

void Sys_Error(const char *format, ...)
{
    char message[256]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    throw std::runtime_error(message);
}

void Com_Memset(void *destination, const int value, const std::size_t count)
{
    std::memset(destination, value, count);
}

bool Sys_IsMainThread()
{
    return true;
}

void *Sys_AllocatePhysicalMemory(std::size_t size, std::size_t alignment)
{
    assert(size == g_hunk.size());
    assert(alignment == 4096u);
    return g_hunk.data();
}

void Sys_FreePhysicalMemory(void *) {}

void track_static_alloc_internal(void *, int, const char *, int) {}

const dvar_s *__cdecl Dvar_RegisterBool(
    const char *name,
    bool value,
    std::uint16_t flags,
    const char *description)
{
    assert(std::strcmp(name, "logScriptTimes") == 0);
    assert(!value);
    assert(flags == DVAR_NOFLAG);
    assert(std::strcmp(
        description, "Log times for every print called from script") == 0);
    g_logScriptTimes.name = name;
    g_logScriptTimes.description = description;
    g_logScriptTimes.flags = flags;
    g_logScriptTimes.current.enabled = value;
    return &g_logScriptTimes;
}

std::uint32_t SL_GetString_(
    const char *value, std::uint32_t user, mtType_t type)
{
    assert(std::strcmp(value, "end") == 0);
    assert(user == 0);
    assert(type == MT_TYPE_NOTETRACK);
    return 0x1234u;
}

std::uint32_t SL_GetStringOfSize(
    const char *value,
    std::uint32_t user,
    std::uint32_t length,
    mtType_t type)
{
    assert(user == 0);
    assert(length == 0x11u);
    assert(type == MT_TYPE_DUPLICATE_PARTS);
    for (std::uint32_t i = 0; i < length; ++i)
        assert(value[i] == 0);
    return 0x5678u;
}

void Sys_OutOfMemErrorInternal(const char *, int)
{
    throw std::runtime_error("out of memory");
}

XAssetHeader DB_FindXAssetHeader(XAssetType type, const char *name)
{
    g_lookupName = name ? name : "";
    XAssetHeader header{};
    if (type == ASSET_TYPE_CLIPMAP &&
        g_lookupName == "maps/killhouse.d3dbsp")
        header.clipMap = &cm;
    else if (type == ASSET_TYPE_COMWORLD &&
        g_lookupName == "maps/killhouse.d3dbsp")
        header.comWorld = &comWorld;
    return header;
}

void CM_LoadMapData_LoadObj(const char *)
{
    ++g_loadObjCount;
}

cmodel_t *CM_ClipHandleToModel(std::uint32_t handle)
{
    assert(handle < cm.numSubModels);
    return &cm.cmodels[handle];
}

int main()
{
    dvar_t fastFile{};
    fastFile.current.enabled = true;
    useFastFile = &fastFile;

    cbrush_t sourceBrush{};
    sourceBrush.contents = 0x1234;
    sourceBrush.mins[0] = -16.0f;
    sourceBrush.maxs[2] = 32.0f;
    cmodel_t models[2]{};
    models[1].mins[0] = -64.0f;
    models[1].maxs[2] = 96.0f;
    cLeaf_t leaves[1]{};
    leaves[0].cluster = 17;

    std::memset(&cm, 0, sizeof(cm));
    cm.name = "maps/killhouse.d3dbsp";
    cm.partitionCount = 3;
    cm.box_brush = &sourceBrush;
    cm.box_model.radius = 41.0f;
    cm.checksum = 0x5a17c0deu;
    cm.cmodels = models;
    cm.numSubModels = 2;
    cm.leafs = leaves;
    cm.numLeafs = 1;

    DB_InitAssetPools();
    assert(DB_XAssetPool[ASSET_TYPE_CLIPMAP] == &cm);
    assert(DB_XAssetPool[ASSET_TYPE_COMWORLD] == &comWorld);
    assert(DB_XAssetPool[ASSET_TYPE_GFXWORLD] == &s_world);
    assert(DB_IsSingletonAssetPool(ASSET_TYPE_CLIPMAP));
    Com_InitHunkMemory();
    SetEngineLifecycleTraceObserver(ObserveLifecycle);
    Scr_InitVariables();
    Scr_Init();
    Scr_Settings(1, 0, 1);
    XAnimInit();
    DObjInit();
    int checksum = 0;
    CM_LoadMap("maps/killhouse.d3dbsp", &checksum);
    comWorld.name = "maps/killhouse.d3dbsp";
    comWorld.isInUse = 1;
    char worldName[] = "maps/killhouse.d3dbsp";
    Com_LoadWorld(worldName);
    SaveMemory_InitializeSaveSystem();
    SaveMemory_ClearDemoSave();
    ClearEngineLifecycleTraceObserver();

    assert(g_assertCount == 0);
    assert(g_loadObjCount == 0);
    assert(g_lookupName == "maps/killhouse.d3dbsp");
    assert(checksum == static_cast<int>(0x5a17c0deu));
    assert(cm.isInUse == 1);
    assert((g_lifecycle == std::vector<std::string>{
        "Scr_InitVariables begin", "Scr_InitVariables complete",
        "Scr_Init begin", "Scr_Init complete",
        "XAnimInit begin", "XAnimInit complete",
        "DObjInit begin", "DObjInit complete",
        "CM_LoadMap begin", "CM_LoadMap complete",
        "Com_LoadWorld begin", "Com_LoadWorld complete",
        "SaveMemory initialization begin",
        "SaveMemory initialization complete"}));
    SaveGame *currentSave = SaveMemory_GetSaveHandle(0);
    SaveGame *demoSave = SaveMemory_GetSaveHandle(1);
    SaveGame *committedSave = SaveMemory_GetSaveHandle(2);
    assert(currentSave);
    assert(committedSave);
    assert(currentSave != committedSave);
    assert(currentSave->isUsingGlobalBuffer);
    assert(committedSave->isUsingGlobalBuffer);
    assert(currentSave->memFile.bufferSize == 1572864);
    assert(committedSave->memFile.bufferSize == 1572864);
    assert(demoSave);
    assert(!demoSave->isUsingGlobalBuffer);
    assert(!demoSave->saveState);
    assert(scrVarPub.bInited);
    assert(scrVarPub.developer);
    assert(!scrVarPub.developer_script);
    assert(scrVmPub.abort_on_error);
    assert(logScriptTimes == &g_logScriptTimes);
    assert(scrVmPub.top == scrVmPub.stack);
    assert(scrVmPub.maxstack == &scrVmPub.stack[2047]);
    assert(scrVmPub.stack[0].type == VAR_CODEPOS);
    assert(scrVarPub.tempVariable == 1);
    assert(scrVarPub.numScriptValues == 1);
    assert(scrVarPub.numScriptObjects == 0);
    assert(scrVarPub.totalObjectRefCount == 1);
    assert(scrVarGlob.variableList[VARIABLELIST_PARENT_BEGIN].u.next == 1);
    assert(scrVarGlob.variableList[VARIABLELIST_CHILD_BEGIN].u.next == 2);
    assert(g_endNotetrackName == 0x1234u);
    assert(g_anim_developer);
    assert(g_info_usage == 1);
    assert(g_info_high_usage == 1);
    assert(g_xAnimInfo[0].prev == 4095);
    assert(g_xAnimInfo[0].next == 1);
    assert(g_xAnimInfo[1].prev == 0);
    assert(g_xAnimInfo[1].next == 2);
    assert(g_xAnimInfo[4095].next == 0);
    assert(g_xAnimInfo[0].state.currentAnimTime == 0.0f);
    assert(g_xAnimInfo[0].state.goalWeight == 0.0f);
    assert(g_empty == 0x5678u);
    assert(Hunk_Used() == 5 * 32);

    constexpr std::array<ThreadContext_t, 5> initializedContexts{
        THREAD_CONTEXT_MAIN,
        THREAD_CONTEXT_BACKEND,
        THREAD_CONTEXT_WORKER0,
        THREAD_CONTEXT_WORKER1,
        THREAD_CONTEXT_SERVER,
    };
    for (const ThreadContext_t context : initializedContexts)
    {
        const TraceThreadInfo &thread = g_traceThreadInfo[context];
        assert(thread.checkcount.global == 0);
        assert(thread.checkcount.partitions);
        assert(thread.box_brush);
        assert(thread.box_model);
        assert(thread.box_brush != &sourceBrush);
        assert(thread.box_brush->contents == sourceBrush.contents);
        assert(thread.box_brush->mins[0] == sourceBrush.mins[0]);
        assert(thread.box_brush->maxs[2] == sourceBrush.maxs[2]);
        assert(thread.box_model->radius == cm.box_model.radius);
    }
    assert(!g_traceThreadInfo[THREAD_CONTEXT_WORKER2].checkcount.partitions);
    assert(CM_LeafCluster(0) == 17);

    float mins[3]{};
    float maxs[3]{};
    CM_ModelBounds(1, mins, maxs);
    assert(mins[0] == -64.0f);
    assert(maxs[2] == 96.0f);

    const char *savedName = cm.name;
    cm.isInUse = 0;
    CM_Shutdown();
    assert(cm.name == savedName);
    assert(cm.checksum == 0);
    assert(cm.partitionCount == 0);
    CM_Unload();

    std::printf(
        "canonical-cm-load name=%s lookup=clipMap==&cm checksum=%08x "
        "thread-contexts=0,1,2,3,5 partition-bytes=%zu "
        "save-buffers=1572864,1572864 script-ranges=1,2 temp=1 "
        "xanim-ring=4095,1,0 dobj-empty=5678 "
        "lifecycle=%s>%s>%s>%s>%s>%s>%s>%s>%s>%s>%s>%s>%s>%s\n",
        savedName,
        0x5a17c0deu,
        5u * 3u * sizeof(int),
        g_lifecycle[0].c_str(),
        g_lifecycle[1].c_str(),
        g_lifecycle[2].c_str(),
        g_lifecycle[3].c_str(),
        g_lifecycle[4].c_str(),
        g_lifecycle[5].c_str(),
        g_lifecycle[6].c_str(),
        g_lifecycle[7].c_str(),
        g_lifecycle[8].c_str(),
        g_lifecycle[9].c_str(),
        g_lifecycle[10].c_str(),
        g_lifecycle[11].c_str(),
        g_lifecycle[12].c_str(),
        g_lifecycle[13].c_str());
    return 0;
}
