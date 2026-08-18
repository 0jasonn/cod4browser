#include <qcommon/engine_lifecycle_trace.h>

namespace
{
EngineLifecycleTraceObserver g_observer = nullptr;
void *g_userData = nullptr;
} // namespace

const char *EngineLifecycleStageName(EngineLifecycleStage stage) noexcept
{
    switch (stage)
    {
    case EngineLifecycleStage::ClientInitBegin: return "CL_Init begin";
    case EngineLifecycleStage::ClientInputBegin: return "CL_InitInput begin";
    case EngineLifecycleStage::ClientInputEnd: return "CL_InitInput end";
    case EngineLifecycleStage::ClientRendererConfigure: return "CL_InitRef";
    case EngineLifecycleStage::ClientInitComplete: return "CL_Init complete";
    case EngineLifecycleStage::MapCommandAccepted: return "map command accepted";
    case EngineLifecycleStage::MapNameSelected: return "canonical map name selected";
    case EngineLifecycleStage::MapSpawnBegin: return "SV_SpawnServer";
    case EngineLifecycleStage::MapLoadingBegin: return "map loading begins";
    case EngineLifecycleStage::MapZoneRequestConstructed: return "map zone request constructed";
    case EngineLifecycleStage::DatabaseLoadAssets: return "DB_LoadXAssets";
    case EngineLifecycleStage::DatabaseLoadZone: return "DB_LoadXZone";
    case EngineLifecycleStage::LogicalFastfileRequest: return "logical fastfile requested";
    case EngineLifecycleStage::CollisionLoadBegin: return "CM_LoadMap begin";
    case EngineLifecycleStage::CollisionLoadComplete: return "CM_LoadMap complete";
    case EngineLifecycleStage::CommonWorldLoadBegin: return "Com_LoadWorld begin";
    case EngineLifecycleStage::CommonWorldLoadComplete: return "Com_LoadWorld complete";
    case EngineLifecycleStage::SaveSystemInitBegin: return "SaveMemory initialization begin";
    case EngineLifecycleStage::SaveSystemInitComplete: return "SaveMemory initialization complete";
    case EngineLifecycleStage::ScriptVariablesInitBegin: return "Scr_InitVariables begin";
    case EngineLifecycleStage::ScriptVariablesInitComplete: return "Scr_InitVariables complete";
    case EngineLifecycleStage::ScriptVmInitBegin: return "Scr_Init begin";
    case EngineLifecycleStage::ScriptVmInitComplete: return "Scr_Init complete";
    case EngineLifecycleStage::XAnimInitBegin: return "XAnimInit begin";
    case EngineLifecycleStage::XAnimInitComplete: return "XAnimInit complete";
    case EngineLifecycleStage::DObjInitBegin: return "DObjInit begin";
    case EngineLifecycleStage::DObjInitComplete: return "DObjInit complete";
    }
    return "unknown";
}

void SetEngineLifecycleTraceObserver(
    EngineLifecycleTraceObserver observer, void *userData) noexcept
{
    g_observer = observer;
    g_userData = userData;
}

void ClearEngineLifecycleTraceObserver() noexcept
{
    g_observer = nullptr;
    g_userData = nullptr;
}

void EmitEngineLifecycleTrace(
    EngineLifecycleStage stage,
    const char *name,
    std::uint32_t zoneCount,
    std::int32_t allocFlags,
    std::int32_t freeFlags,
    std::int32_t sync) noexcept
{
    if (!g_observer) return;
    const EngineLifecycleTraceEvent event{
        stage, name ? name : "", zoneCount, allocFlags, freeFlags, sync};
    g_observer(event, g_userData);
}
