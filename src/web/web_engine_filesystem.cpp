#include <web/web_engine_filesystem.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr const char *ARCHIVE_PATH = "main/iw_00.iwd";
constexpr uint32_t LOCAL_HEADER_PREFIX_BYTES = 30u;
constexpr uint32_t MAX_LOCAL_HEADER_BYTES =
    LOCAL_HEADER_PREFIX_BYTES + kisak::iwd::Limits{}.maxPathBytes +
    std::numeric_limits<uint16_t>::max();
constexpr uint32_t ARCHIVE_READ_CHUNK_BYTES = WEB_FS_MAX_READ_SIZE;
constexpr uint32_t VERIFY_OUTPUT_CHUNK_BYTES = WEB_FS_MAX_READ_SIZE;
constexpr uint32_t DECODE_OUTPUT_BUDGET_BYTES = 64u * 1024u;
static_assert(DECODE_OUTPUT_BUDGET_BYTES <= ARCHIVE_READ_CHUNK_BYTES);

enum class Phase : uint8_t
{
    NeedLocalPrefix,
    WaitingLocalPrefix,
    NeedLocalHeader,
    WaitingLocalHeader,
    NeedMemberData,
    WaitingMemberData,
    DecodingMemberData,
};

struct Request
{
    WebEngineFsRequestId requestId = 0;
    WebEngineFsOperation operation = WebEngineFsOperation::Read;
    WebEngineFsCompletionCallback callback = nullptr;
    void *userData = nullptr;
    Phase phase = Phase::NeedLocalPrefix;

    // Copy the central record into the request. This makes its canonical path
    // available to a stale-source callback after the mounted index is cleared.
    kisak::iwd::Entry entry;
    uint32_t outputLimit = 0;

    WebFsRequestId filesystemRequestId = 0;
    bool completionReady = false;
    bool completionCopyFailed = false;
    WebFsStatus completionStatus = WebFsStatus::Pending;
    std::vector<uint8_t> completionBytes;
    std::size_t completionInputCursor = 0;

    uint32_t requiredLocalHeaderBytes = 0;
    uint32_t localHeaderCursor = 0;
    std::vector<uint8_t> localHeaderBytes;

    kisak::iwd::MemberLocation memberLocation;
    kisak::iwd::MemberDecoder decoder;
    uint32_t memberInputOffset = 0;

    // Read keeps the complete decoded payload plus one terminator. Verify uses
    // only a reusable output chunk and discards its contents after Consume.
    std::vector<uint8_t> outputBytes;
    std::vector<uint8_t> verifyOutput;
};

struct Service
{
    bool mounted = false;
    kisak::iwd::CentralDirectoryLocator locator;
    kisak::iwd::ArchiveIndex index;
    uint32_t lastRequestId = 0;
    std::unique_ptr<Request> request;
};

Service g_service;

WebEngineFsRequestId AllocateRequestId()
{
    g_service.lastRequestId = g_service.lastRequestId == UINT32_MAX
        ? 1u
        : g_service.lastRequestId + 1u;
    return g_service.lastRequestId;
}

void ClearMount()
{
    g_service.mounted = false;
    g_service.locator = {};
    g_service.index = {};
}

void ResetRequest()
{
    // Destroying the request also tears down an active zlib stream and releases
    // every byte-vector allocation. Do this before any user callback.
    g_service.request.reset();
}

void Deliver(
    WebEngineFsStatus status,
    WebFsStatus filesystemStatus = WebFsStatus::Success,
    kisak::iwd::Error archiveError = kisak::iwd::Error::None,
    bool invalidateMount = false)
{
    Request *request = g_service.request.get();
    if (!request)
    {
        return;
    }

    const WebEngineFsRequestId requestId = request->requestId;
    const WebEngineFsOperation operation = request->operation;
    WebEngineFsCompletionCallback callback = request->callback;
    void *userData = request->userData;
    std::string path = std::move(request->entry.path);
    const uint32_t memberSize = request->entry.uncompressedSize;
    const uint16_t compressionMethod = request->entry.compressionMethod;
    const uint32_t crc32 = request->entry.crc32;
    std::vector<uint8_t> output;
    if (status == WebEngineFsStatus::Success && operation == WebEngineFsOperation::Read)
    {
        output = std::move(request->outputBytes);
    }

    ResetRequest();
    if (invalidateMount)
    {
        ClearMount();
    }

    const WebEngineFsCompletion completion{
        requestId,
        operation,
        status,
        filesystemStatus,
        archiveError,
        path.c_str(),
        !output.empty() ? output.data() : nullptr,
        status == WebEngineFsStatus::Success && operation == WebEngineFsOperation::Read
            ? memberSize
            : 0u,
        memberSize,
        compressionMethod,
        crc32,
    };
    callback(completion, userData);
}

void DeliverArchiveError(kisak::iwd::Error error)
{
    Deliver(
        error == kisak::iwd::Error::DecoderOutputLimit
            ? WebEngineFsStatus::OutputLimit
            : WebEngineFsStatus::ArchiveError,
        WebFsStatus::Success,
        error);
}

void CompleteFilesystemRequest(const WebFsCompletion &completion, void *userData)
{
    Request *request = g_service.request.get();
    const auto requestId = static_cast<WebEngineFsRequestId>(
        reinterpret_cast<uintptr_t>(userData));
    if (!request || request->requestId != requestId ||
        request->filesystemRequestId != completion.requestId)
    {
        return;
    }

    request->filesystemRequestId = 0;
    request->completionReady = true;
    request->completionCopyFailed = false;
    request->completionStatus = completion.operation == WebFsOperation::Read
        ? completion.status
        : WebFsStatus::ProtocolError;
    request->completionBytes.clear();
    request->completionInputCursor = 0;
    if (request->completionStatus != WebFsStatus::Success)
    {
        return;
    }

    try
    {
        request->completionBytes.assign(
            completion.data,
            completion.data + completion.dataLength);
    }
    catch (...)
    {
        request->completionBytes.clear();
        request->completionCopyFailed = true;
    }
}

bool BeginFilesystemRead(uint32_t offset, uint32_t length, Phase waitingPhase)
{
    Request *request = g_service.request.get();
    if (!request)
    {
        return false;
    }
    if (length == 0 || length > ARCHIVE_READ_CHUNK_BYTES)
    {
        Deliver(WebEngineFsStatus::InternalError);
        return false;
    }

    request->completionReady = false;
    request->completionCopyFailed = false;
    request->completionStatus = WebFsStatus::Pending;
    request->completionBytes.clear();
    request->completionInputCursor = 0;
    const WebFsStatus status = WebFs_BeginRead(
        ARCHIVE_PATH,
        offset,
        length,
        CompleteFilesystemRequest,
        reinterpret_cast<void *>(static_cast<uintptr_t>(request->requestId)),
        &request->filesystemRequestId);
    if (status != WebFsStatus::Pending)
    {
        Deliver(
            status == WebFsStatus::StaleSource
                ? WebEngineFsStatus::StaleSource
                : WebEngineFsStatus::FilesystemError,
            status,
            kisak::iwd::Error::None,
            status == WebFsStatus::StaleSource);
        return false;
    }

    request->phase = waitingPhase;
    return true;
}

bool TakeFilesystemCompletion()
{
    Request *request = g_service.request.get();
    if (!request || !request->completionReady)
    {
        return false;
    }

    request->completionReady = false;
    if (request->completionCopyFailed)
    {
        Deliver(WebEngineFsStatus::InternalError);
        return false;
    }
    if (request->completionStatus == WebFsStatus::Success)
    {
        return true;
    }

    const WebFsStatus status = request->completionStatus;
    if (status == WebFsStatus::StaleSource)
    {
        Deliver(
            WebEngineFsStatus::StaleSource,
            status,
            kisak::iwd::Error::None,
            true);
    }
    else if (status == WebFsStatus::Cancelled)
    {
        Deliver(WebEngineFsStatus::Cancelled, status);
    }
    else
    {
        Deliver(WebEngineFsStatus::FilesystemError, status);
    }
    return false;
}

void FinishMember()
{
    Request *request = g_service.request.get();
    if (!request)
    {
        return;
    }
    const kisak::iwd::Error error = request->decoder.Finish();
    if (error != kisak::iwd::Error::None)
    {
        DeliverArchiveError(error);
        return;
    }
    Deliver(WebEngineFsStatus::Success);
}

void ValidateLocalHeaderAndBeginDecoder()
{
    Request *request = g_service.request.get();
    if (!request)
    {
        return;
    }

    const kisak::iwd::Error headerError = kisak::iwd::ValidateLocalHeader(
        request->entry,
        g_service.locator,
        request->localHeaderBytes,
        request->memberLocation);
    if (headerError != kisak::iwd::Error::None)
    {
        DeliverArchiveError(headerError);
        return;
    }

    const kisak::iwd::Error decoderError =
        request->decoder.Begin(request->entry, request->outputLimit);
    if (decoderError != kisak::iwd::Error::None)
    {
        DeliverArchiveError(decoderError);
        return;
    }

    // The local header is no longer needed. Release it before potentially
    // decoding a multi-megabyte cached member.
    std::vector<uint8_t>().swap(request->localHeaderBytes);
    std::vector<uint8_t>().swap(request->completionBytes);
    if (request->memberLocation.compressedSize == 0)
    {
        FinishMember();
        return;
    }
    request->phase = Phase::NeedMemberData;
}

void ContinueLocalHeaderRead()
{
    Request *request = g_service.request.get();
    if (!request)
    {
        return;
    }
    if (request->localHeaderCursor >= request->requiredLocalHeaderBytes)
    {
        ValidateLocalHeaderAndBeginDecoder();
        return;
    }

    const uint32_t remaining =
        request->requiredLocalHeaderBytes - request->localHeaderCursor;
    const uint32_t length = std::min(remaining, ARCHIVE_READ_CHUNK_BYTES);
    (void)BeginFilesystemRead(
        request->entry.localHeaderOffset + request->localHeaderCursor,
        length,
        Phase::WaitingLocalHeader);
}

void DecodeMemberStep()
{
    Request *request = g_service.request.get();
    if (!request)
    {
        return;
    }

    if (request->completionInputCursor > request->completionBytes.size())
    {
        DeliverArchiveError(kisak::iwd::Error::DecoderInputSizeMismatch);
        return;
    }

    // One service-frame invocation performs at most one decoder call. Both the
    // supplied compressed suffix and output window are bounded to 64 KiB, so a
    // highly compressible deflate member cannot monopolize the browser frame
    // by expanding a complete source chunk in one C++ loop.
    const std::span<const uint8_t> input =
        std::span<const uint8_t>(request->completionBytes)
            .subspan(request->completionInputCursor);
    if (input.size() > ARCHIVE_READ_CHUNK_BYTES)
    {
        DeliverArchiveError(kisak::iwd::Error::DecoderInputSizeMismatch);
        return;
    }

    std::span<uint8_t> output;
    if (request->operation == WebEngineFsOperation::Read)
    {
        const uint32_t produced = request->decoder.UncompressedBytesProduced();
        if (produced > request->entry.uncompressedSize)
        {
            DeliverArchiveError(kisak::iwd::Error::DecoderOutputSizeMismatch);
            return;
        }
        const uint32_t remaining = request->entry.uncompressedSize - produced;
        const uint32_t budget = std::min(remaining, DECODE_OUTPUT_BUDGET_BYTES);
        output = std::span<uint8_t>(request->outputBytes).subspan(produced, budget);
    }
    else
    {
        output = request->verifyOutput;
        if (output.size() > DECODE_OUTPUT_BUDGET_BYTES)
        {
            output = output.first(DECODE_OUTPUT_BUDGET_BYTES);
        }
    }

    std::size_t inputConsumed = 0;
    std::size_t outputProduced = 0;
    const kisak::iwd::Error error = request->decoder.Consume(
        input,
        output,
        inputConsumed,
        outputProduced);
    if (error != kisak::iwd::Error::None)
    {
        DeliverArchiveError(error);
        return;
    }
    if (inputConsumed == 0 && outputProduced == 0)
    {
        Deliver(WebEngineFsStatus::InternalError);
        return;
    }
    if (inputConsumed > input.size())
    {
        DeliverArchiveError(kisak::iwd::Error::DecoderInputSizeMismatch);
        return;
    }

    request->completionInputCursor += inputConsumed;
    request->memberInputOffset = request->decoder.CompressedBytesConsumed();
    if (request->memberInputOffset > request->memberLocation.compressedSize)
    {
        DeliverArchiveError(kisak::iwd::Error::DecoderInputSizeMismatch);
        return;
    }

    const bool compressedChunkConsumed =
        request->completionInputCursor == request->completionBytes.size();
    const bool decoderStillNeedsOutput =
        request->decoder.Progress() == kisak::iwd::DecoderProgress::NeedsOutput;
    if (!compressedChunkConsumed || decoderStillNeedsOutput)
    {
        // Preserve both the input buffer and its exact suffix cursor. The next
        // WebEngineFs_Frame resumes without rereading or replaying bytes.
        request->phase = Phase::DecodingMemberData;
        return;
    }

    request->completionBytes.clear();
    request->completionInputCursor = 0;
    if (request->memberInputOffset == request->memberLocation.compressedSize)
    {
        FinishMember();
        return;
    }
    request->phase = Phase::NeedMemberData;
}

WebEngineFsStatus BeginRequest(
    const char *path,
    WebEngineFsOperation operation,
    uint32_t maxOutputBytes,
    WebEngineFsCompletionCallback callback,
    void *userData,
    WebEngineFsRequestId *requestId)
{
    if (requestId)
    {
        *requestId = 0;
    }
    if (!path || path[0] == '\0' || !callback || !requestId)
    {
        return WebEngineFsStatus::InvalidArgument;
    }
    if (!g_service.mounted)
    {
        return WebEngineFsStatus::NotMounted;
    }
    if (g_service.request)
    {
        return WebEngineFsStatus::Busy;
    }

    const kisak::iwd::Entry *entry = nullptr;
    try
    {
        entry = g_service.index.Find(path);
    }
    catch (...)
    {
        return WebEngineFsStatus::InternalError;
    }
    if (!entry)
    {
        return WebEngineFsStatus::NotFound;
    }
    if (entry->directory)
    {
        return WebEngineFsStatus::ArchiveError;
    }

    const kisak::iwd::Limits archiveLimits;
    if (entry->compressedSize > archiveLimits.maxMemberCompressedBytes ||
        entry->uncompressedSize > archiveLimits.maxMemberUncompressedBytes)
    {
        return WebEngineFsStatus::OutputLimit;
    }

    const uint32_t effectiveOutputLimit = operation == WebEngineFsOperation::Read
        ? std::min(maxOutputBytes, WEB_ENGINE_FS_MAX_CACHED_MEMBER_BYTES)
        : archiveLimits.maxMemberUncompressedBytes;
    if (entry->uncompressedSize > effectiveOutputLimit)
    {
        return WebEngineFsStatus::OutputLimit;
    }

    std::unique_ptr<Request> request;
    try
    {
        request = std::make_unique<Request>();
        request->entry = *entry;
        request->requestId = AllocateRequestId();
        request->operation = operation;
        request->callback = callback;
        request->userData = userData;
        request->outputLimit = effectiveOutputLimit;
        if (operation == WebEngineFsOperation::Read)
        {
            request->outputBytes.resize(
                static_cast<std::size_t>(entry->uncompressedSize) + 1u,
                0u);
        }
        else if (entry->uncompressedSize != 0)
        {
            request->verifyOutput.resize(std::min(
                entry->uncompressedSize,
                VERIFY_OUTPUT_CHUNK_BYTES));
        }
    }
    catch (...)
    {
        return WebEngineFsStatus::InternalError;
    }

    *requestId = request->requestId;
    g_service.request = std::move(request);
    return WebEngineFsStatus::Pending;
}
} // namespace

const char *WebEngineFs_StatusString(WebEngineFsStatus status) noexcept
{
    switch (status)
    {
    case WebEngineFsStatus::Success: return "success";
    case WebEngineFsStatus::Pending: return "pending";
    case WebEngineFsStatus::NotMounted: return "engine archive is not mounted";
    case WebEngineFsStatus::InvalidArgument: return "invalid engine filesystem request";
    case WebEngineFsStatus::Busy: return "an engine filesystem request is already active";
    case WebEngineFsStatus::NotFound: return "archive member was not found";
    case WebEngineFsStatus::OutputLimit: return "archive member exceeds the request output limit";
    case WebEngineFsStatus::Cancelled: return "engine filesystem request was cancelled";
    case WebEngineFsStatus::StaleSource: return "browser asset import changed during the read";
    case WebEngineFsStatus::FilesystemError: return "browser filesystem read failed";
    case WebEngineFsStatus::ArchiveError: return "archive member validation or decoding failed";
    case WebEngineFsStatus::InternalError: return "engine filesystem state or allocation failed";
    }
    return "unknown engine filesystem status";
}

WebEngineFsStatus WebEngineFs_Mount(
    const kisak::iwd::CentralDirectoryLocator &locator,
    kisak::iwd::ArchiveIndex &&index)
{
    if (g_service.request)
    {
        return WebEngineFsStatus::Busy;
    }
    if (locator.archiveSize == 0 || locator.entryCount == 0 ||
        locator.entryCount != index.RecordCount() || index.Entries().empty() ||
        locator.eocdOffset > locator.archiveSize ||
        static_cast<uint64_t>(locator.centralOffset) + locator.centralSize !=
            locator.eocdOffset)
    {
        return WebEngineFsStatus::InvalidArgument;
    }

    ClearMount();
    g_service.locator = locator;
    g_service.index = std::move(index);
    g_service.mounted = true;
    return WebEngineFsStatus::Success;
}

bool WebEngineFs_Unmount()
{
    if (g_service.request && g_service.request->filesystemRequestId != 0 &&
        !WebFs_Cancel(g_service.request->filesystemRequestId))
    {
        return false;
    }
    ResetRequest();
    ClearMount();
    return true;
}

bool WebEngineFs_IsMounted()
{
    return g_service.mounted;
}

std::span<const kisak::iwd::Entry> WebEngineFs_Entries()
{
    return g_service.mounted
        ? g_service.index.Entries()
        : std::span<const kisak::iwd::Entry>{};
}

const kisak::iwd::Entry *WebEngineFs_Find(const char *path)
{
    if (!g_service.mounted || !path || path[0] == '\0')
    {
        return nullptr;
    }
    try
    {
        return g_service.index.Find(path);
    }
    catch (...)
    {
        return nullptr;
    }
}

WebEngineFsStatus WebEngineFs_BeginRead(
    const char *path,
    uint32_t maxOutputBytes,
    WebEngineFsCompletionCallback callback,
    void *userData,
    WebEngineFsRequestId *requestId)
{
    return BeginRequest(
        path,
        WebEngineFsOperation::Read,
        maxOutputBytes,
        callback,
        userData,
        requestId);
}

WebEngineFsStatus WebEngineFs_BeginVerify(
    const char *path,
    WebEngineFsCompletionCallback callback,
    void *userData,
    WebEngineFsRequestId *requestId)
{
    return BeginRequest(
        path,
        WebEngineFsOperation::Verify,
        0,
        callback,
        userData,
        requestId);
}

bool WebEngineFs_Cancel(WebEngineFsRequestId requestId)
{
    Request *request = g_service.request.get();
    if (!request || requestId == 0 || request->requestId != requestId)
    {
        return false;
    }
    if (request->filesystemRequestId != 0 &&
        !WebFs_Cancel(request->filesystemRequestId))
    {
        return false;
    }

    ResetRequest();
    return true;
}

void WebEngineFs_Frame()
{
    Request *request = g_service.request.get();
    if (!request)
    {
        return;
    }

    switch (request->phase)
    {
    case Phase::NeedLocalPrefix:
        (void)BeginFilesystemRead(
            request->entry.localHeaderOffset,
            LOCAL_HEADER_PREFIX_BYTES,
            Phase::WaitingLocalPrefix);
        return;

    case Phase::WaitingLocalPrefix:
        if (!request->completionReady || !TakeFilesystemCompletion())
        {
            return;
        }
        request = g_service.request.get();
        if (!request)
        {
            return;
        }
        if (const kisak::iwd::Error error = kisak::iwd::RequiredLocalHeaderBytes(
                request->completionBytes,
                request->requiredLocalHeaderBytes);
            error != kisak::iwd::Error::None)
        {
            DeliverArchiveError(error);
            return;
        }
        if (request->requiredLocalHeaderBytes < LOCAL_HEADER_PREFIX_BYTES ||
            request->requiredLocalHeaderBytes > MAX_LOCAL_HEADER_BYTES)
        {
            DeliverArchiveError(kisak::iwd::Error::LocalHeaderMismatch);
            return;
        }
        if (static_cast<uint64_t>(request->entry.localHeaderOffset) +
                request->requiredLocalHeaderBytes >
            g_service.locator.centralOffset)
        {
            DeliverArchiveError(kisak::iwd::Error::EntryRange);
            return;
        }
        try
        {
            request->localHeaderBytes = std::move(request->completionBytes);
            request->localHeaderBytes.reserve(request->requiredLocalHeaderBytes);
        }
        catch (...)
        {
            Deliver(WebEngineFsStatus::InternalError);
            return;
        }
        request->localHeaderCursor = LOCAL_HEADER_PREFIX_BYTES;
        if (request->localHeaderCursor == request->requiredLocalHeaderBytes)
        {
            ValidateLocalHeaderAndBeginDecoder();
        }
        else
        {
            request->phase = Phase::NeedLocalHeader;
        }
        return;

    case Phase::NeedLocalHeader:
        ContinueLocalHeaderRead();
        return;

    case Phase::WaitingLocalHeader:
        if (!request->completionReady || !TakeFilesystemCompletion())
        {
            return;
        }
        request = g_service.request.get();
        if (!request)
        {
            return;
        }
        try
        {
            request->localHeaderBytes.insert(
                request->localHeaderBytes.end(),
                request->completionBytes.begin(),
                request->completionBytes.end());
        }
        catch (...)
        {
            Deliver(WebEngineFsStatus::InternalError);
            return;
        }
        request->localHeaderCursor +=
            static_cast<uint32_t>(request->completionBytes.size());
        request->completionBytes.clear();
        if (request->localHeaderCursor > request->requiredLocalHeaderBytes)
        {
            DeliverArchiveError(kisak::iwd::Error::LocalHeaderMismatch);
            return;
        }
        if (request->localHeaderCursor == request->requiredLocalHeaderBytes)
        {
            ValidateLocalHeaderAndBeginDecoder();
        }
        else
        {
            request->phase = Phase::NeedLocalHeader;
        }
        return;

    case Phase::NeedMemberData:
    {
        if (request->memberInputOffset >= request->memberLocation.compressedSize)
        {
            if (request->memberInputOffset == request->memberLocation.compressedSize)
            {
                FinishMember();
            }
            else
            {
                DeliverArchiveError(kisak::iwd::Error::DecoderInputSizeMismatch);
            }
            return;
        }
        const uint32_t remaining =
            request->memberLocation.compressedSize - request->memberInputOffset;
        const uint32_t length = std::min(remaining, ARCHIVE_READ_CHUNK_BYTES);
        (void)BeginFilesystemRead(
            request->memberLocation.dataOffset + request->memberInputOffset,
            length,
            Phase::WaitingMemberData);
        return;
    }

    case Phase::WaitingMemberData:
        if (!request->completionReady || !TakeFilesystemCompletion())
        {
            return;
        }
        DecodeMemberStep();
        return;

    case Phase::DecodingMemberData:
        DecodeMemberStep();
        return;
    }
}

std::size_t WebEngineFs_RetainedByteCount()
{
    const Request *request = g_service.request.get();
    if (!request)
    {
        return 0;
    }
    return request->completionBytes.capacity() +
        request->localHeaderBytes.capacity() +
        request->outputBytes.capacity() +
        request->verifyOutput.capacity();
}
