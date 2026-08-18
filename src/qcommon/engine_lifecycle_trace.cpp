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
    case EngineLifecycleStage::ServerGameProgsInitBegin: return "SV_InitGameProgs begin";
    case EngineLifecycleStage::ServerGameVmInitBegin: return "SV_InitGameVM begin";
    case EngineLifecycleStage::GameInitBegin: return "G_InitGame begin";
    case EngineLifecycleStage::GameConstantsInitComplete: return "G_InitGame constants complete";
    case EngineLifecycleStage::GameWeaponsInitComplete: return "G_InitGame weapons complete";
    case EngineLifecycleStage::GameScriptsInitBegin: return "G_InitGame scripts begin";
    case EngineLifecycleStage::GameScriptsInitComplete: return "G_InitGame scripts complete";
    case EngineLifecycleStage::GameInitComplete: return "G_InitGame complete";
    case EngineLifecycleStage::ServerSettleBegin: return "SV_Settle begin";
    case EngineLifecycleStage::ServerSettlePreFrameComplete: return "SV_Settle pre-frame complete";
    case EngineLifecycleStage::ServerSettleRunFrameComplete: return "SV_Settle run-frame complete";
    case EngineLifecycleStage::ServerDirectConnectComplete: return "SV_DirectConnect complete";
    case EngineLifecycleStage::ClientConnectResponseComplete: return "CL_ConnectResponse complete";
    case EngineLifecycleStage::ServerClientEnterWorldComplete: return "SV_ClientEnterWorld complete";
    case EngineLifecycleStage::GameLoadLevelBegin: return "G_LoadLevel begin";
    case EngineLifecycleStage::GameLoadLevelPrecacheComplete: return "G_LoadLevel default models complete";
    case EngineLifecycleStage::GameLoadLevelScriptSystemComplete: return "G_LoadLevel script system complete";
    case EngineLifecycleStage::GameLoadLevelGameVariableComplete: return "G_LoadLevel game variable complete";
    case EngineLifecycleStage::GameLoadLevelClientBeginComplete: return "G_LoadLevel ClientBegin complete";
    case EngineLifecycleStage::GameLoadLevelStructsComplete: return "G_LoadLevel structs complete";
    case EngineLifecycleStage::GameLoadLevelActorsComplete: return "G_LoadLevel actors complete";
    case EngineLifecycleStage::GameLoadLevelPathsComplete: return "G_LoadLevel paths complete";
    case EngineLifecycleStage::GameLoadLevelScriptComplete: return "G_LoadLevel script complete";
    case EngineLifecycleStage::GameLoadLevelFirstFrameComplete: return "G_LoadLevel first frame complete";
    case EngineLifecycleStage::GameLoadLevelMessagesComplete: return "G_LoadLevel messages complete";
    case EngineLifecycleStage::GameLoadLevelComplete: return "G_LoadLevel complete";
    case EngineLifecycleStage::ServerGameVmInitComplete: return "SV_InitGameVM complete";
    case EngineLifecycleStage::ServerGameProgsInitComplete: return "SV_InitGameProgs complete";
    case EngineLifecycleStage::ClientCGameInitBegin: return "CL_InitCGame begin";
    case EngineLifecycleStage::CGameInitBegin: return "CG_Init begin";
    case EngineLifecycleStage::CGameInitComplete: return "CG_Init complete";
    case EngineLifecycleStage::ClientCGameInitComplete: return "CL_InitCGame complete";
    case EngineLifecycleStage::GameDrivenFrame: return "game-driven frame";
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
