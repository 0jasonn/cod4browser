#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <game/savememory_state.h>

#include <qcommon/engine_lifecycle_trace.h>
#include <qcommon/mem_track.h>
#include <universal/q_shared.h>

#include <cstring>

SaveMemoryGlob saveMemoryGlob{};

void __cdecl TRACK_save_memory()
{
    track_static_alloc_internal(
        &saveMemoryGlob, sizeof(saveMemoryGlob), "saveMemoryGlob", 10);
}

SaveGame *__cdecl SaveMemory_GetSaveHandle(unsigned int type)
{
    if (!type)
        return saveMemoryGlob.currentGameSave;
    if (type == 1)
        return &saveMemoryGlob.demo;
    if (type < 3)
        return saveMemoryGlob.committedGameSave;
    if (!alwaysfails)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\savememory.cpp",
            174, 0, "unreachable");
    return nullptr;
}

void __cdecl SaveMemory_ClearSaveGame(
    SaveGame *saveGame, bool isUsingGlobalBuffer)
{
    std::memset(saveGame, 0, sizeof(SaveGame));
    saveGame->isUsingGlobalBuffer = isUsingGlobalBuffer;
}

void *SaveMemory_ResetGameBuffers()
{
    std::memset(&saveMemoryGlob.game0, 0, sizeof(saveMemoryGlob.game0));
    saveMemoryGlob.game0.isUsingGlobalBuffer = true;
    void *result = std::memset(
        &saveMemoryGlob.game1, 0, sizeof(saveMemoryGlob.game1));
    saveMemoryGlob.game1.isUsingGlobalBuffer = true;
    saveMemoryGlob.game0.memFile.buffer = saveMemoryGlob.buffer0;
    saveMemoryGlob.game1.memFile.buffer = saveMemoryGlob.buffer1;
    saveMemoryGlob.game0.memFile.bufferSize = 1572864;
    saveMemoryGlob.game1.memFile.bufferSize = 1572864;
    saveMemoryGlob.committedGameSave = &saveMemoryGlob.game0;
    saveMemoryGlob.currentGameSave = &saveMemoryGlob.game1;
    return result;
}

void __cdecl SaveMemory_InitializeSaveSystem()
{
    EmitEngineLifecycleTrace(EngineLifecycleStage::SaveSystemInitBegin);
    if (saveMemoryGlob.committedGameSave)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\savememory.cpp",
            226, 0, "%s", "!saveMemoryGlob.committedGameSave");
    if (saveMemoryGlob.currentGameSave)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\savememory.cpp",
            227, 0, "%s", "!saveMemoryGlob.currentGameSave");
    SaveMemory_ResetGameBuffers();
    saveMemoryGlob.recentLoadTime = 0;
    EmitEngineLifecycleTrace(EngineLifecycleStage::SaveSystemInitComplete);
}

void __cdecl SaveMemory_ShutdownSaveSystem()
{
    saveMemoryGlob.committedGameSave = nullptr;
    saveMemoryGlob.currentGameSave = nullptr;
}

void __cdecl SaveMemory_ClearDemoSave()
{
    std::memset(&saveMemoryGlob.demo, 0, sizeof(saveMemoryGlob.demo));
    saveMemoryGlob.demo.isUsingGlobalBuffer = false;
}
