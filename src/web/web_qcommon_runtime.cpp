#include <web/web_qcommon_runtime.h>

#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <universal/dvar.h>
#include <web/web_filesystem.h>
#include <web/web_qcommon_preinit.h>
#include <web/web_system.h>

#include <emscripten.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace
{
using kisak::web::QcommonActionKind;
using kisak::web::QcommonPreinitAction;
using kisak::web::QcommonPreinitMachine;
using kisak::web::QcommonPreinitSnapshot;
using kisak::web::QcommonPreinitStage;

enum class RuntimeEventKind : std::uint8_t
{
    Start,
    FilesystemCompletion,
    Ready,
};

struct RuntimeEvent
{
    RuntimeEventKind kind = RuntimeEventKind::Start;
    std::uint32_t framePumpTick = 0;
};

struct PendingCompletion
{
    bool ready = false;
    WebFsOperation operation = WebFsOperation::Stat;
    WebFsStatus status = WebFsStatus::Pending;
    std::uint32_t actionToken = 0;
    std::uint32_t fileSize = 0;
    std::uint32_t byteCount = 0;
    std::array<std::uint8_t, 14> bytes{};
};

QcommonPreinitMachine g_machine;
std::vector<std::uint8_t> g_startupArena;
std::array<RuntimeEvent, kisak::web::QCOMMON_EVENT_CAPACITY> g_events{};
std::size_t g_eventHead = 0u;
std::size_t g_eventCount = 0u;
std::uint32_t g_eventsQueued = 0u;
std::uint32_t g_eventsProcessed = 0u;
std::uint32_t g_generation = 0u;
std::uint32_t g_pendingActionToken = 0u;
WebFsRequestId g_requestId = 0u;
PendingCompletion g_completion;
std::array<char, 256> g_failure{};
QcommonPreinitStage g_lastPublishedStage = QcommonPreinitStage::Idle;

EM_JS(
    void,
    DispatchQcommonState,
    (const char *state,
     const char *stage,
     const char *message,
     const char *error,
     const char *currentPath,
     std::uint32_t generation,
     std::uint32_t framePumpTick,
     std::uint32_t actionsIssued,
     std::uint32_t filesChecked,
     std::uint32_t totalFiles,
     std::uint32_t probeBytesRead,
     std::uint32_t arenaBytes,
     std::uint32_t eventCapacity,
     std::uint32_t eventsQueued,
     std::uint32_t eventsProcessed,
     std::uint32_t commandDvarCount,
     int actionPending),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:qcommon", {
            detail: {
                state: UTF8ToString(state),
                stage: UTF8ToString(stage),
                message: UTF8ToString(message),
                error: UTF8ToString(error),
                currentPath: currentPath ? UTF8ToString(currentPath) : null,
                generation: generation >>> 0,
                framePumpTick: framePumpTick >>> 0,
                actionsIssued: actionsIssued >>> 0,
                filesChecked: filesChecked >>> 0,
                totalFiles: totalFiles >>> 0,
                probeBytesRead: probeBytesRead >>> 0,
                arenaBytes: arenaBytes >>> 0,
                eventCapacity: eventCapacity >>> 0,
                eventsQueued: eventsQueued >>> 0,
                eventsProcessed: eventsProcessed >>> 0,
                commandDvarCount: commandDvarCount >>> 0,
                actionPending: Boolean(actionPending),
                cooperative: true,
                asyncify: false,
                pthreads: false,
                retailTraversal: false
            }
        }));
    });

const char *StateString(const QcommonPreinitSnapshot &snapshot)
{
    if (snapshot.stage == QcommonPreinitStage::PreDatabase)
    {
        return "ready";
    }
    if (snapshot.stage == QcommonPreinitStage::Failed)
    {
        return "failed";
    }
    if (snapshot.stage == QcommonPreinitStage::Idle ||
        snapshot.stage == QcommonPreinitStage::Cancelled)
    {
        return "idle";
    }
    return "loading";
}

const char *MessageFor(const QcommonPreinitSnapshot &snapshot)
{
    switch (snapshot.stage)
    {
    case QcommonPreinitStage::Idle: return "Waiting for a validated local installation";
    case QcommonPreinitStage::Memory: return "Initializing bounded startup memory";
    case QcommonPreinitStage::Events: return "Initializing the cooperative event queue";
    case QcommonPreinitStage::Commands: return "Registering pre-database commands and dvars";
    case QcommonPreinitStage::FilesystemStat: return "Opening a startup file through the browser VFS";
    case QcommonPreinitStage::FilesystemRead: return "Reading a bounded startup-file header";
    case QcommonPreinitStage::PreDatabase: return "Portable qcommon reached the pre-database boundary";
    case QcommonPreinitStage::Failed: return g_failure.data();
    case QcommonPreinitStage::Cancelled: return "Startup was cancelled after the local installation changed";
    }
    return "Unknown qcommon startup state";
}

void Publish(std::uint32_t framePumpTick, bool force = false)
{
    const QcommonPreinitSnapshot snapshot = g_machine.Snapshot();
    if (!force && snapshot.stage == g_lastPublishedStage)
    {
        return;
    }
    g_lastPublishedStage = snapshot.stage;
    DispatchQcommonState(
        StateString(snapshot),
        kisak::web::QcommonPreinitStageString(snapshot.stage),
        MessageFor(snapshot),
        kisak::web::QcommonPreinitErrorString(snapshot.error),
        snapshot.currentPath,
        snapshot.generation,
        framePumpTick,
        snapshot.actionsIssued,
        snapshot.filesChecked,
        snapshot.totalFiles,
        snapshot.probeBytesRead,
        snapshot.arenaBytes,
        snapshot.eventCapacity,
        g_eventsQueued,
        g_eventsProcessed,
        snapshot.commandDvarCount,
        snapshot.actionPending ? 1 : 0);
}

void PushEvent(RuntimeEventKind kind, std::uint32_t framePumpTick)
{
    if (g_eventCount == g_events.size())
    {
        g_eventHead = (g_eventHead + 1u) % g_events.size();
        --g_eventCount;
    }
    const std::size_t tail = (g_eventHead + g_eventCount) % g_events.size();
    g_events[tail] = {kind, framePumpTick};
    ++g_eventCount;
    ++g_eventsQueued;
}

void PumpOneEvent()
{
    if (g_eventCount == 0u)
    {
        return;
    }
    g_eventHead = (g_eventHead + 1u) % g_events.size();
    --g_eventCount;
    ++g_eventsProcessed;
}

const char *WebFsStatusString(WebFsStatus status)
{
    switch (status)
    {
    case WebFsStatus::Success: return "success";
    case WebFsStatus::Pending: return "pending";
    case WebFsStatus::NotReady: return "filesystem bridge is not ready";
    case WebFsStatus::InvalidArgument: return "invalid filesystem request";
    case WebFsStatus::NoRequestSlots: return "filesystem request table is full";
    case WebFsStatus::InvalidRange: return "filesystem range is invalid";
    case WebFsStatus::NotFound: return "startup file was not found";
    case WebFsStatus::StaleSource: return "browser asset import changed during startup";
    case WebFsStatus::IoError: return "browser filesystem I/O failed";
    case WebFsStatus::ProtocolError: return "browser filesystem protocol failed";
    case WebFsStatus::Cancelled: return "filesystem request was cancelled";
    }
    return "unknown filesystem error";
}

void CompleteFilesystem(const WebFsCompletion &completion, void *)
{
    if (completion.requestId != g_requestId || g_pendingActionToken == 0u)
    {
        return;
    }
    g_requestId = 0u;
    g_completion = {};
    g_completion.ready = true;
    g_completion.operation = completion.operation;
    g_completion.status = completion.status;
    g_completion.actionToken = g_pendingActionToken;
    g_completion.fileSize = completion.fileSize;
    if (completion.operation == WebFsOperation::Read &&
        completion.status == WebFsStatus::Success &&
        completion.data && completion.dataLength <= g_completion.bytes.size())
    {
        g_completion.byteCount = completion.dataLength;
        for (std::uint32_t index = 0u; index < completion.dataLength; ++index)
        {
            g_completion.bytes[index] = completion.data[index];
        }
    }
}

bool InitializeMemory()
{
    try
    {
        g_startupArena.assign(kisak::web::QCOMMON_STARTUP_ARENA_BYTES, 0u);
    }
    catch (...)
    {
        std::vector<std::uint8_t>().swap(g_startupArena);
        return false;
    }
    return g_startupArena.size() == kisak::web::QCOMMON_STARTUP_ARENA_BYTES;
}

bool InitializeEvents()
{
    g_events.fill({});
    g_eventHead = 0u;
    g_eventCount = 0u;
    g_eventsQueued = 0u;
    g_eventsProcessed = 0u;
    return true;
}

bool RegisterCommandsAndDvars()
{
    if (!Dvar_IsSystemActive())
    {
        return false;
    }
    const std::array<const dvar_s *, kisak::web::QCOMMON_COMMAND_DVAR_COUNT> dvars = {{
        // These two now belong to canonical Com_InitDvars. The temporary
        // bootstrap observes them without attempting to reinterpret their
        // canonical integer/bool types as strings.
        Dvar_FindVar("developer"),
        Dvar_FindVar("useFastFile"),
        Dvar_RegisterString("fs_game", "", 0u, "Selected game directory"),
        Dvar_RegisterInt(
            "loc_language", 0, 0, 15, DVAR_ARCHIVE | DVAR_LATCH,
            "The current language locale"),
        Dvar_RegisterString(
            "com_webPreDatabase",
            "initializing",
            0u,
            "Browser qcommon pre-database startup state"),
    }};
    for (const dvar_s *dvar : dvars)
    {
        if (!dvar)
        {
            return false;
        }
    }
    Cbuf_ExecuteBuffer(
        0,
        0,
        "set com_webPreDatabase initializing;com_webPreDatabase");
    return std::strcmp(Dvar_GetString("com_webPreDatabase"), "initializing") == 0;
}

void SetFailure(const QcommonPreinitSnapshot &snapshot, WebFsStatus filesystemStatus)
{
    const char *path = snapshot.currentPath ? snapshot.currentPath : "startup core";
    const char *reason = snapshot.error == kisak::web::QcommonPreinitError::None
        ? WebFsStatusString(filesystemStatus)
        : kisak::web::QcommonPreinitErrorString(snapshot.error);
    std::snprintf(
        g_failure.data(),
        g_failure.size(),
        "%s: %s",
        path,
        reason);
    g_failure.back() = '\0';
}

void ApplyCompletion(std::uint32_t framePumpTick)
{
    PendingCompletion completion = g_completion;
    g_completion = {};
    g_pendingActionToken = 0u;
    const bool success = completion.status == WebFsStatus::Success;
    bool accepted = false;
    if (completion.operation == WebFsOperation::Stat)
    {
        accepted = g_machine.CompleteStat(
            completion.actionToken,
            success,
            completion.fileSize);
    }
    else
    {
        accepted = g_machine.CompleteRead(
            completion.actionToken,
            success,
            std::span<const std::uint8_t>(
                completion.bytes.data(),
                completion.byteCount));
    }
    if (!accepted)
    {
        std::snprintf(g_failure.data(), g_failure.size(), "filesystem completion did not match startup state");
    }
    PushEvent(RuntimeEventKind::FilesystemCompletion, framePumpTick);
    const QcommonPreinitSnapshot snapshot = g_machine.Snapshot();
    if (snapshot.stage == QcommonPreinitStage::Failed)
    {
        SetFailure(snapshot, completion.status);
        Web_Log(WebLogLevel::Error, "[kisakcod-web] qcommon startup failed: %s\n", g_failure.data());
    }
    else if (snapshot.stage == QcommonPreinitStage::PreDatabase)
    {
        Dvar_SetCommand("com_webPreDatabase", "ready");
        PushEvent(RuntimeEventKind::Ready, framePumpTick);
        Web_Log(
            WebLogLevel::Info,
            "[kisakcod-web] Portable qcommon reached the pre-database boundary.\n");
    }
    Publish(framePumpTick, true);
}

void BeginFilesystemAction(const QcommonPreinitAction &action)
{
    g_pendingActionToken = action.token;
    WebFsStatus status = WebFsStatus::InvalidArgument;
    if (action.kind == QcommonActionKind::StatFile)
    {
        status = WebFs_BeginStat(action.path, CompleteFilesystem, nullptr, &g_requestId);
    }
    else
    {
        status = WebFs_BeginRead(
            action.path,
            action.offset,
            action.length,
            CompleteFilesystem,
            nullptr,
            &g_requestId);
    }
    if (status != WebFsStatus::Pending)
    {
        g_requestId = 0u;
        g_completion = {};
        g_completion.ready = true;
        g_completion.operation = action.kind == QcommonActionKind::StatFile
            ? WebFsOperation::Stat
            : WebFsOperation::Read;
        g_completion.status = status;
        g_completion.actionToken = action.token;
    }
}

void CancelInternal(bool publish)
{
    if (g_requestId != 0u)
    {
        (void)WebFs_Cancel(g_requestId);
        g_requestId = 0u;
    }
    g_pendingActionToken = 0u;
    g_completion = {};
    g_machine.Cancel();
    std::vector<std::uint8_t>().swap(g_startupArena);
    g_eventHead = 0u;
    g_eventCount = 0u;
    if (publish)
    {
        Publish(0u, true);
    }
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_StartQcommonRuntime()
{
    if (g_machine.Running() || g_machine.Ready())
    {
        CancelInternal(false);
    }
    g_generation = g_generation == UINT32_MAX ? 1u : g_generation + 1u;
    g_failure.fill(0);
    g_lastPublishedStage = QcommonPreinitStage::Idle;
    if (!g_machine.Start(g_generation))
    {
        Web_Log(WebLogLevel::Error, "[kisakcod-web] qcommon startup could not begin.\n");
        return;
    }
    PushEvent(RuntimeEventKind::Start, 0u);
    Publish(0u, true);
}

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_CancelQcommonRuntime()
{
    CancelInternal(true);
}

void WebQcommonRuntime_Frame(const WebFrameInfo &frame)
{
    PumpOneEvent();
    if (g_completion.ready)
    {
        ApplyCompletion(frame.pumpTick);
        return;
    }
    if (!g_machine.Running() || g_requestId != 0u)
    {
        return;
    }

    QcommonPreinitAction action;
    if (!g_machine.NextAction(action))
    {
        return;
    }
    if (action.kind == QcommonActionKind::InitializeMemory ||
        action.kind == QcommonActionKind::InitializeEvents ||
        action.kind == QcommonActionKind::RegisterCommands)
    {
        const bool success = action.kind == QcommonActionKind::InitializeMemory
            ? InitializeMemory()
            : action.kind == QcommonActionKind::InitializeEvents
                ? InitializeEvents()
                : RegisterCommandsAndDvars();
        (void)g_machine.CompleteLocal(action.token, success);
        const QcommonPreinitSnapshot snapshot = g_machine.Snapshot();
        if (snapshot.stage == QcommonPreinitStage::Failed)
        {
            SetFailure(snapshot, WebFsStatus::Success);
        }
        Publish(frame.pumpTick, true);
        return;
    }

    BeginFilesystemAction(action);
    Publish(frame.pumpTick, true);
    if (g_completion.ready)
    {
        return;
    }

}
