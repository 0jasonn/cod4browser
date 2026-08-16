#include <web/web_archive_job.h>

#include <qcommon/iwd_archive.h>
#include <web/web_engine_asset.h>
#include <web/web_engine_filesystem.h>
#include <web/web_filesystem.h>

#include <emscripten.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr uint32_t BASE_ARCHIVE_COUNT = 14u;
constexpr uint32_t ZIP_TAIL_BYTES = 22u + 0xffffu;
constexpr std::size_t READY_ENTRIES_PER_FRAME = 64u;

enum class Phase
{
    Idle,
    NeedStat,
    WaitingStat,
    NeedTail,
    WaitingTail,
    NeedCentral,
    WaitingCentral,
    SelectMember,
    WaitingMember,
    BeginReadyPublication,
    PublishingReadyEntries,
    Finished,
    Failed,
};

struct VerifiedMember
{
    std::string path;
    uint16_t method = 0;
    uint32_t size = 0;
    uint32_t crc32 = 0;
};

struct ArchiveJob
{
    Phase phase = Phase::Idle;
    uint32_t generation = 0;
    uint32_t archiveIndex = 0;
    std::string archivePath;
    std::string targetMemberPath;
    bool targetMemberUnavailable = false;
    WebFsRequestId requestId = 0;
    WebEngineFsRequestId memberRequestId = 0;
    uint32_t archiveSize = 0;
    uint32_t windowOffset = 0;
    uint32_t windowLength = 0;
    uint32_t windowCursor = 0;
    bool completionReady = false;
    WebFsStatus completionStatus = WebFsStatus::Pending;
    std::vector<uint8_t> completionBytes;
    std::vector<uint8_t> windowBytes;
    kisak::iwd::CentralDirectoryLocator locator;
    uint32_t recordCount = 0;
    uint32_t uniqueEntries = 0;
    std::vector<std::size_t> membersToVerify;
    std::size_t memberCursor = 0;
    bool memberCompletionReady = false;
    WebEngineFsStatus memberStatus = WebEngineFsStatus::Pending;
    WebFsStatus memberFilesystemStatus = WebFsStatus::Success;
    kisak::iwd::Error memberArchiveError = kisak::iwd::Error::None;
    VerifiedMember completedMember;
    std::vector<VerifiedMember> verifiedMembers;
    std::size_t readyEntryCursor = 0;
    std::string failure;
};

ArchiveJob g_job;

EM_JS(void, DispatchArchiveLoading,
    (uint32_t generation, const char *path, const char *targetMember), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:archive", {
        detail: {
            state: "loading",
            generation: generation >>> 0,
            path: UTF8ToString(path),
            targetMember: targetMember ? UTF8ToString(targetMember) : null,
            message: "Opening the browser-local IWD archive"
        }
    }));
});

EM_JS(
    void,
    DispatchArchiveReadyBegin,
    (uint32_t generation, const char *path, const char *targetMember,
     int targetMemberAvailable, uint32_t recordCount, uint32_t uniqueEntries),
    {
        const detail = {
            state: "ready",
            generation: generation >>> 0,
            path: UTF8ToString(path),
            targetMember: targetMember ? UTF8ToString(targetMember) : null,
            targetMemberAvailable: Boolean(targetMemberAvailable),
            message: "The portable IWD reader mounted and verified archive members",
            recordCount,
            uniqueEntries,
            entries: [],
            verifiedMembers: []
        };
        globalThis.__KISAKCOD_ARCHIVE_DETAIL__ = detail;
    });

EM_JS(void, DispatchArchiveReadyEntry, (const char *path), {
    globalThis.__KISAKCOD_ARCHIVE_DETAIL__?.entries.push({ path: UTF8ToString(path) });
});

EM_JS(
    void,
    DispatchArchiveReadyMember,
    (const char *path, uint32_t method, uint32_t size, uint32_t crc),
    {
        globalThis.__KISAKCOD_ARCHIVE_DETAIL__?.verifiedMembers.push({
            path: UTF8ToString(path),
            method,
            size,
            crc32: crc >>> 0
        });
    });

EM_JS(void, DispatchArchiveReadyEnd, (), {
    const detail = globalThis.__KISAKCOD_ARCHIVE_DETAIL__;
    delete globalThis.__KISAKCOD_ARCHIVE_DETAIL__;
    if (detail) {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:archive", { detail }));
    }
});

EM_JS(void, DiscardArchiveReady, (), {
    delete globalThis.__KISAKCOD_ARCHIVE_DETAIL__;
});

EM_JS(void, DispatchArchiveFailure, (uint32_t generation, const char *message), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:archive", {
        detail: {
            state: "failed",
            generation: generation >>> 0,
            message: UTF8ToString(message)
        }
    }));
});

EM_JS(void, DispatchArchiveIdle, (uint32_t generation), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:archive", {
        detail: {
            state: "idle",
            generation: generation >>> 0,
            message: "Waiting for a validated local archive"
        }
    }));
});

void ResetJob(bool keepGeneration)
{
    const uint32_t generation = keepGeneration ? g_job.generation : 0;
    g_job = {};
    g_job.generation = generation;
}

bool SelectArchiveCandidate(uint32_t archiveIndex)
{
    if (archiveIndex >= BASE_ARCHIVE_COUNT) return false;
    try
    {
        g_job.archiveIndex = archiveIndex;
        g_job.archivePath = "main/iw_";
        g_job.archivePath.push_back(static_cast<char>('0' + archiveIndex / 10u));
        g_job.archivePath.push_back(static_cast<char>('0' + archiveIndex % 10u));
        g_job.archivePath += ".iwd";
    }
    catch (...)
    {
        return false;
    }
    g_job.archiveSize = 0u;
    g_job.windowOffset = 0u;
    g_job.windowLength = 0u;
    g_job.windowCursor = 0u;
    g_job.completionReady = false;
    g_job.completionStatus = WebFsStatus::Pending;
    g_job.completionBytes.clear();
    g_job.windowBytes.clear();
    g_job.locator = {};
    g_job.recordCount = 0u;
    g_job.uniqueEntries = 0u;
    return true;
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
    case WebFsStatus::NotFound: return "archive was not found";
    case WebFsStatus::StaleSource: return "browser asset import changed during the read";
    case WebFsStatus::IoError: return "browser filesystem I/O failed";
    case WebFsStatus::ProtocolError: return "browser filesystem protocol failed";
    case WebFsStatus::Cancelled: return "filesystem request was cancelled";
    }
    return "unknown filesystem error";
}

bool RetireArchiveState()
{
    if (g_job.requestId != 0)
    {
        if (!WebFs_Cancel(g_job.requestId))
        {
            return false;
        }
        g_job.requestId = 0;
    }
    if (g_job.memberRequestId != 0)
    {
        if (!WebEngineFs_Cancel(g_job.memberRequestId))
        {
            return false;
        }
        g_job.memberRequestId = 0;
    }
    if (!WebEngineAsset_Cancel())
    {
        return false;
    }
    return WebEngineFs_Unmount();
}

void Fail(const char *context, const char *reason)
{
    DiscardArchiveReady();
    g_job.failure = context;
    g_job.failure += ": ";
    g_job.failure += reason;
    if (!RetireArchiveState())
    {
        g_job.failure += "; the active filesystem request could not be retired safely";
    }
    g_job.phase = Phase::Failed;
    DispatchArchiveFailure(g_job.generation, g_job.failure.c_str());
}

void Fail(const char *context, kisak::iwd::Error error)
{
    Fail(context, kisak::iwd::ErrorString(error));
}

void Fail(const char *context, WebEngineFsStatus status)
{
    std::string reason = WebEngineFs_StatusString(status);
    if (status == WebEngineFsStatus::FilesystemError ||
        status == WebEngineFsStatus::StaleSource)
    {
        reason += " (";
        reason += WebFsStatusString(g_job.memberFilesystemStatus);
        reason += ")";
    }
    else if (status == WebEngineFsStatus::ArchiveError)
    {
        reason += " (";
        reason += kisak::iwd::ErrorString(g_job.memberArchiveError);
        reason += ")";
    }
    Fail(context, reason.c_str());
}

void CompleteRequest(const WebFsCompletion &completion, void *userData)
{
    const auto requestGeneration = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(userData));
    if (requestGeneration != g_job.generation || completion.requestId != g_job.requestId)
    {
        return;
    }

    g_job.requestId = 0;
    g_job.completionReady = true;
    g_job.completionStatus = completion.status;
    if (completion.operation == WebFsOperation::Stat && completion.status == WebFsStatus::Success)
    {
        g_job.archiveSize = completion.fileSize;
    }
    if (completion.operation == WebFsOperation::Read && completion.status == WebFsStatus::Success)
    {
        try
        {
            g_job.completionBytes.assign(
                completion.data, completion.data + completion.dataLength);
        }
        catch (...)
        {
            g_job.completionStatus = WebFsStatus::IoError;
            g_job.completionBytes.clear();
        }
    }
}

void CompleteMemberRequest(const WebEngineFsCompletion &completion, void *userData)
{
    const auto requestGeneration = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(userData));
    if (requestGeneration != g_job.generation ||
        completion.requestId != g_job.memberRequestId)
    {
        return;
    }

    g_job.memberRequestId = 0;
    g_job.memberCompletionReady = true;
    g_job.memberStatus = completion.status;
    g_job.memberFilesystemStatus = completion.filesystemStatus;
    g_job.memberArchiveError = completion.archiveError;
    if (completion.status == WebEngineFsStatus::Success)
    {
        try
        {
            g_job.completedMember = {
                completion.path ? completion.path : "",
                completion.compressionMethod,
                completion.memberSize,
                completion.crc32,
            };
        }
        catch (...)
        {
            g_job.memberStatus = WebEngineFsStatus::InternalError;
            g_job.completedMember = {};
        }
    }
}

bool BeginStat()
{
    const WebFsStatus status = WebFs_BeginStat(
        g_job.archivePath.c_str(),
        CompleteRequest,
        reinterpret_cast<void *>(static_cast<uintptr_t>(g_job.generation)),
        &g_job.requestId);
    if (status != WebFsStatus::Pending)
    {
        Fail("could not open IWD", WebFsStatusString(status));
        return false;
    }
    g_job.phase = Phase::WaitingStat;
    return true;
}

bool BeginRead(uint32_t offset, uint32_t length, Phase waitingPhase)
{
    g_job.completionBytes.clear();
    const WebFsStatus status = WebFs_BeginRead(
        g_job.archivePath.c_str(),
        offset,
        length,
        CompleteRequest,
        reinterpret_cast<void *>(static_cast<uintptr_t>(g_job.generation)),
        &g_job.requestId);
    if (status != WebFsStatus::Pending)
    {
        Fail("could not read IWD", WebFsStatusString(status));
        return false;
    }
    g_job.phase = waitingPhase;
    return true;
}

bool ContinueWindow(Phase waitingPhase)
{
    const uint32_t remaining = g_job.windowLength - g_job.windowCursor;
    const uint32_t length = std::min(remaining, WEB_FS_MAX_READ_SIZE);
    return BeginRead(g_job.windowOffset + g_job.windowCursor, length, waitingPhase);
}

bool BeginWindow(uint32_t offset, uint32_t length, Phase waitingPhase)
{
    g_job.windowOffset = offset;
    g_job.windowLength = length;
    g_job.windowCursor = 0;
    g_job.windowBytes.clear();
    try
    {
        g_job.windowBytes.reserve(length);
    }
    catch (...)
    {
        Fail("could not allocate IWD read window", "out of memory");
        return false;
    }
    return ContinueWindow(waitingPhase);
}

bool TakeCompletion(const char *context)
{
    if (!g_job.completionReady)
    {
        return false;
    }
    g_job.completionReady = false;
    if (g_job.completionStatus != WebFsStatus::Success)
    {
        Fail(context, WebFsStatusString(g_job.completionStatus));
        return false;
    }
    return true;
}

bool TakeWindowCompletion(const char *context, Phase waitingPhase)
{
    if (!TakeCompletion(context))
    {
        return false;
    }
    try
    {
        g_job.windowBytes.insert(
            g_job.windowBytes.end(),
            g_job.completionBytes.begin(),
            g_job.completionBytes.end());
    }
    catch (...)
    {
        Fail(context, "out of memory while assembling read window");
        return false;
    }
    g_job.windowCursor += static_cast<uint32_t>(g_job.completionBytes.size());
    g_job.completionBytes.clear();
    if (g_job.windowCursor < g_job.windowLength)
    {
        (void)ContinueWindow(waitingPhase);
        return false;
    }
    g_job.completionBytes = std::move(g_job.windowBytes);
    return true;
}

void SelectMembers()
{
    g_job.membersToVerify.clear();
    try
    {
        g_job.membersToVerify.reserve(2);
    }
    catch (...)
    {
        Fail("could not select IWD members", "out of memory");
        return;
    }
    bool haveStored = false;
    bool haveDeflated = false;
    const auto entries = WebEngineFs_Entries();
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        if (entries[index].directory)
        {
            continue;
        }
        if (!haveStored && entries[index].compressionMethod == 0)
        {
            try
            {
                g_job.membersToVerify.push_back(index);
            }
            catch (...)
            {
                Fail("could not select IWD members", "out of memory");
                return;
            }
            haveStored = true;
        }
        else if (!haveDeflated && entries[index].compressionMethod == 8)
        {
            try
            {
                g_job.membersToVerify.push_back(index);
            }
            catch (...)
            {
                Fail("could not select IWD members", "out of memory");
                return;
            }
            haveDeflated = true;
        }
        if (haveStored && haveDeflated)
        {
            break;
        }
    }
    if (g_job.membersToVerify.empty())
    {
        Fail("could not verify IWD member", "archive contains no readable file members");
        return;
    }
    g_job.memberCursor = 0;
    g_job.phase = Phase::SelectMember;
}

void BeginMemberVerification()
{
    const auto entries = WebEngineFs_Entries();
    if (g_job.memberCursor >= g_job.membersToVerify.size())
    {
        return;
    }
    const std::size_t entryIndex = g_job.membersToVerify[g_job.memberCursor];
    if (entryIndex >= entries.size())
    {
        Fail("could not verify IWD member", "mounted archive index changed unexpectedly");
        return;
    }

    g_job.memberCompletionReady = false;
    g_job.completedMember = {};
    const WebEngineFsStatus status = WebEngineFs_BeginVerify(
        entries[entryIndex].path.c_str(),
        CompleteMemberRequest,
        reinterpret_cast<void *>(static_cast<uintptr_t>(g_job.generation)),
        &g_job.memberRequestId);
    if (status != WebEngineFsStatus::Pending)
    {
        g_job.memberStatus = status;
        Fail("could not begin IWD member verification", status);
        return;
    }
    g_job.phase = Phase::WaitingMember;
}

void TakeMemberVerification()
{
    if (!g_job.memberCompletionReady)
    {
        return;
    }
    g_job.memberCompletionReady = false;
    if (g_job.memberStatus != WebEngineFsStatus::Success)
    {
        Fail("could not verify IWD member", g_job.memberStatus);
        return;
    }
    try
    {
        g_job.verifiedMembers.push_back(std::move(g_job.completedMember));
    }
    catch (...)
    {
        Fail("could not retain IWD verification metadata", "out of memory");
        return;
    }
    ++g_job.memberCursor;
    g_job.phase = Phase::SelectMember;
}

void BeginReadyPublication()
{
    g_job.readyEntryCursor = 0;
    g_job.phase = Phase::PublishingReadyEntries;
    DispatchArchiveReadyBegin(
        g_job.generation,
        g_job.archivePath.c_str(),
        g_job.targetMemberPath.empty() ? nullptr : g_job.targetMemberPath.c_str(),
        g_job.targetMemberUnavailable ? 0 : 1,
        g_job.recordCount,
        g_job.uniqueEntries);
}

void PublishReadyEntries()
{
    const auto entries = WebEngineFs_Entries();
    if (!WebEngineFs_IsMounted() || entries.size() != g_job.uniqueEntries)
    {
        Fail("could not publish IWD index", "mounted archive index changed unexpectedly");
        return;
    }

    const std::size_t end = std::min(
        entries.size(),
        g_job.readyEntryCursor + READY_ENTRIES_PER_FRAME);
    while (g_job.readyEntryCursor < end)
    {
        DispatchArchiveReadyEntry(entries[g_job.readyEntryCursor].path.c_str());
        ++g_job.readyEntryCursor;
    }
    if (g_job.readyEntryCursor < entries.size())
    {
        return;
    }

    for (const VerifiedMember &member : g_job.verifiedMembers)
    {
        DispatchArchiveReadyMember(
            member.path.c_str(), member.method, member.size, member.crc32);
    }

    // Publish only after the state transition. Event listeners run
    // synchronously and may immediately start another generation.
    g_job.phase = Phase::Finished;
    const uint32_t publishedGeneration = g_job.generation;
    DispatchArchiveReadyEnd();

    if (g_job.phase == Phase::Finished &&
        g_job.generation == publishedGeneration &&
        WebEngineFs_IsMounted())
    {
        WebEngineAsset_Start();
    }
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_StartArchiveJob()
{
    WebArchiveJob_Start();
}

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_CancelArchiveJob()
{
    WebArchiveJob_Cancel();
}

void WebArchiveJob_Start()
{
    DiscardArchiveReady();
    if (!RetireArchiveState())
    {
        g_job.phase = Phase::Failed;
        DispatchArchiveFailure(
            g_job.generation,
            "could not retire the previous engine filesystem request safely");
        return;
    }

    const uint32_t generation = g_job.generation == UINT32_MAX ? 1u : g_job.generation + 1u;
    ResetJob(false);
    g_job.generation = generation;
    if (!SelectArchiveCandidate(0u))
    {
        g_job.phase = Phase::Failed;
        DispatchArchiveFailure(generation, "could not select a base IWD archive");
        return;
    }
    g_job.phase = Phase::NeedStat;
    DispatchArchiveLoading(
        generation,
        g_job.archivePath.c_str(),
        g_job.targetMemberPath.empty() ? nullptr : g_job.targetMemberPath.c_str());
}

void WebArchiveJob_Cancel()
{
    DiscardArchiveReady();
    if (!RetireArchiveState())
    {
        g_job.phase = Phase::Failed;
        DispatchArchiveFailure(
            g_job.generation,
            "could not cancel the active engine filesystem request safely");
        return;
    }
    const uint32_t generation = g_job.generation == UINT32_MAX ? 1u : g_job.generation + 1u;
    ResetJob(false);
    g_job.generation = generation;
    DispatchArchiveIdle(generation);
}

void WebArchiveJob_Frame()
{
    switch (g_job.phase)
    {
    case Phase::Idle:
    case Phase::Failed:
        return;
    case Phase::Finished:
        if (!WebEngineFs_IsMounted())
        {
            g_job.phase = Phase::Failed;
            DispatchArchiveFailure(
                g_job.generation,
                "mounted IWD became unavailable: engine filesystem mount was invalidated");
        }
        return;
    case Phase::NeedStat:
        (void)BeginStat();
        return;
    case Phase::WaitingStat:
        if (!TakeCompletion("could not stat IWD")) return;
        if (g_job.archiveSize < 22u)
        {
            Fail("could not locate IWD", "archive is smaller than a ZIP EOCD");
            return;
        }
        g_job.phase = Phase::NeedTail;
        return;
    case Phase::NeedTail:
    {
        const uint32_t length = std::min(g_job.archiveSize, ZIP_TAIL_BYTES);
        (void)BeginWindow(g_job.archiveSize - length, length, Phase::WaitingTail);
        return;
    }
    case Phase::WaitingTail:
        if (!TakeWindowCompletion("could not read IWD tail", Phase::WaitingTail)) return;
        if (const auto error = kisak::iwd::LocateCentralDirectory(
                g_job.completionBytes,
                g_job.windowOffset,
                g_job.archiveSize,
                {},
                g_job.locator);
            error != kisak::iwd::Error::None)
        {
            Fail("could not locate IWD central directory", error);
            return;
        }
        g_job.phase = Phase::NeedCentral;
        return;
    case Phase::NeedCentral:
        (void)BeginWindow(
            g_job.locator.centralOffset,
            g_job.locator.centralSize,
            Phase::WaitingCentral);
        return;
    case Phase::WaitingCentral:
    {
        if (!TakeWindowCompletion(
                "could not read IWD central directory", Phase::WaitingCentral)) return;
        kisak::iwd::ArchiveIndex index;
        kisak::iwd::Error parseError = kisak::iwd::Error::None;
        try
        {
            parseError = kisak::iwd::ParseCentralDirectory(
                g_job.completionBytes, g_job.locator, {}, index);
        }
        catch (...)
        {
            Fail("could not enumerate IWD", "out of memory");
            return;
        }
        if (parseError != kisak::iwd::Error::None)
        {
            Fail("could not enumerate IWD", parseError);
            return;
        }
        g_job.completionBytes.clear();
        if (!g_job.targetMemberPath.empty() && !g_job.targetMemberUnavailable)
        {
            const kisak::iwd::Entry *targetEntry = nullptr;
            try
            {
                targetEntry = index.Find(g_job.targetMemberPath);
            }
            catch (...)
            {
                Fail("could not search IWD", "selected member lookup failed");
                return;
            }
            if (!targetEntry)
            {
                const uint32_t nextArchive = g_job.archiveIndex + 1u;
                if (nextArchive >= BASE_ARCHIVE_COUNT)
                {
                    g_job.targetMemberUnavailable = true;
                    if (!SelectArchiveCandidate(0u))
                    {
                        Fail("could not restore primary IWD", "out of memory");
                        return;
                    }
                    g_job.phase = Phase::NeedStat;
                    return;
                }
                if (!SelectArchiveCandidate(nextArchive))
                {
                    Fail("could not continue IWD search", "out of memory");
                    return;
                }
                g_job.phase = Phase::NeedStat;
                return;
            }
        }
        g_job.recordCount = index.RecordCount();
        g_job.uniqueEntries = static_cast<uint32_t>(index.Entries().size());
        const WebEngineFsStatus mountStatus = WebEngineFs_Mount(
            g_job.archivePath.c_str(), g_job.locator, std::move(index));
        if (mountStatus != WebEngineFsStatus::Success)
        {
            g_job.memberStatus = mountStatus;
            Fail("could not mount IWD for engine reads", mountStatus);
            return;
        }
        SelectMembers();
        return;
    }
    case Phase::SelectMember:
        if (g_job.memberCursor >= g_job.membersToVerify.size())
        {
            g_job.phase = Phase::BeginReadyPublication;
            return;
        }
        BeginMemberVerification();
        return;
    case Phase::WaitingMember:
        TakeMemberVerification();
        return;
    case Phase::BeginReadyPublication:
        BeginReadyPublication();
        return;
    case Phase::PublishingReadyEntries:
        PublishReadyEntries();
        return;
    }
}
