#pragma once

#include <cstdint>

enum class EngineLifecycleStage : std::uint8_t
{
    ClientInitBegin = 0,
    ClientInputBegin,
    ClientInputEnd,
    ClientRendererConfigure,
    ClientInitComplete,
    MapCommandAccepted,
    MapNameSelected,
    MapSpawnBegin,
    MapLoadingBegin,
    MapZoneRequestConstructed,
    DatabaseLoadAssets,
    DatabaseLoadZone,
    LogicalFastfileRequest,
    CollisionLoadBegin,
    CollisionLoadComplete,
    CommonWorldLoadBegin,
    CommonWorldLoadComplete,
    SaveSystemInitBegin,
    SaveSystemInitComplete,
    ScriptVariablesInitBegin,
    ScriptVariablesInitComplete,
    ScriptVmInitBegin,
    ScriptVmInitComplete,
    XAnimInitBegin,
    XAnimInitComplete,
    DObjInitBegin,
    DObjInitComplete,
    ServerGameProgsInitBegin,
    ServerGameVmInitBegin,
    GameInitBegin,
    GameConstantsInitComplete,
    GameWeaponsInitComplete,
    GameScriptsInitBegin,
    GameScriptsInitComplete,
    GameInitComplete,
    ServerSettleBegin,
    ServerSettlePreFrameComplete,
    ServerSettleRunFrameComplete,
    ServerDirectConnectComplete,
    ClientConnectResponseComplete,
    ServerClientEnterWorldComplete,
    GameLoadLevelBegin,
    GameLoadLevelPrecacheComplete,
    GameLoadLevelScriptSystemComplete,
    GameLoadLevelGameVariableComplete,
    GameLoadLevelClientBeginComplete,
    GameLoadLevelStructsComplete,
    GameLoadLevelActorsComplete,
    GameLoadLevelPathsComplete,
    GameLoadLevelScriptComplete,
    GameLoadLevelFirstFrameComplete,
    GameLoadLevelMessagesComplete,
    GameLoadLevelComplete,
    ServerGameVmInitComplete,
    ServerGameProgsInitComplete,
    ClientCGameInitBegin,
    CGameInitBegin,
    CGameInitComplete,
    ClientCGameInitComplete,
    GameDrivenFrame,
};

struct EngineLifecycleTraceEvent
{
    EngineLifecycleStage stage;
    const char *name;
    std::uint32_t zoneCount;
    std::int32_t allocFlags;
    std::int32_t freeFlags;
    std::int32_t sync;
};

using EngineLifecycleTraceObserver = void (*)(
    const EngineLifecycleTraceEvent &event, void *userData);

const char *EngineLifecycleStageName(EngineLifecycleStage stage) noexcept;
void SetEngineLifecycleTraceObserver(
    EngineLifecycleTraceObserver observer, void *userData = nullptr) noexcept;
void ClearEngineLifecycleTraceObserver() noexcept;
void EmitEngineLifecycleTrace(
    EngineLifecycleStage stage,
    const char *name = nullptr,
    std::uint32_t zoneCount = 0,
    std::int32_t allocFlags = 0,
    std::int32_t freeFlags = 0,
    std::int32_t sync = 0) noexcept;
