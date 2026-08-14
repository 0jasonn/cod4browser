#pragma once

#include <cstddef>
#include <cstdint>

// OPFS is asynchronous on the browser main thread.  This API deliberately
// exposes that fact: accepted operations return Pending and their callbacks are
// delivered only by WebFs_PumpCompletions on a later browser frame.
enum class WebFsStatus : int32_t
{
    Success = 0,
    Pending = 1,
    NotReady = 2,
    InvalidArgument = 3,
    NoRequestSlots = 4,
    InvalidRange = 5,
    NotFound = 6,
    StaleSource = 7,
    IoError = 8,
    ProtocolError = 9,
    Cancelled = 10,
};

enum class WebFsOperation : uint8_t
{
    Stat = 1,
    Read = 2,
};

using WebFsRequestId = uint32_t;

inline constexpr uint32_t WEB_FS_MAX_READ_SIZE = 64u * 1024u;
inline constexpr std::size_t WEB_FS_MAX_REQUESTS = 8;

struct WebFsCompletion
{
    WebFsRequestId requestId;
    WebFsOperation operation;
    WebFsStatus status;

    // Set for a successful Stat completion.
    uint32_t fileSize;

    // Set for a successful Read completion.  The storage is owned by the
    // request slot and remains valid only for the duration of the callback.
    const uint8_t *data;
    uint32_t dataLength;
};

using WebFsCompletionCallback = void (*)(const WebFsCompletion &completion, void *userData);

WebFsStatus WebFs_BeginStat(
    const char *path,
    WebFsCompletionCallback callback,
    void *userData,
    WebFsRequestId *requestId);

WebFsStatus WebFs_BeginRead(
    const char *path,
    uint32_t offset,
    uint32_t length,
    WebFsCompletionCallback callback,
    void *userData,
    WebFsRequestId *requestId);

// Cancellation first retires the matching JavaScript operation token.  Only
// after that synchronous acknowledgement is the C++-owned buffer released for
// reuse, so a late Promise cannot write into a new request.
bool WebFs_Cancel(WebFsRequestId requestId);
void WebFs_CancelAll();

bool WebFs_IsPending(WebFsRequestId requestId);
void WebFs_PumpCompletions();

// JavaScript bridge completion entry points.  These only queue completion
// metadata; user callbacks are never invoked from a Promise microtask.
extern "C" void KisakWeb_CompleteFsStat(
    WebFsRequestId requestId,
    int32_t status,
    uint32_t fileSize);

extern "C" void KisakWeb_CompleteFsRead(
    WebFsRequestId requestId,
    int32_t status,
    uint32_t bytesRead);
