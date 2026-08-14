#include <web/web_filesystem.h>

#include <emscripten.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
constexpr uint32_t SLOT_INDEX_BITS = 4;
constexpr uint32_t SLOT_INDEX_MASK = (1u << SLOT_INDEX_BITS) - 1u;
constexpr uint32_t MAX_SLOT_GENERATION = UINT32_MAX >> SLOT_INDEX_BITS;

enum class RequestState : uint8_t
{
    Free,
    InFlight,
    Completed,
    Delivering,
};

struct RequestSlot
{
    uint32_t generation = 0;
    WebFsRequestId requestId = 0;
    RequestState state = RequestState::Free;
    WebFsOperation operation = WebFsOperation::Stat;
    WebFsStatus completionStatus = WebFsStatus::Pending;
    WebFsCompletionCallback callback = nullptr;
    void *userData = nullptr;
    uint32_t requestedLength = 0;
    uint32_t completedLength = 0;
    uint32_t fileSize = 0;
    uint64_t deliverOnPump = 0;
    std::vector<uint8_t> buffer;
};

std::array<RequestSlot, WEB_FS_MAX_REQUESTS> g_requestSlots{};
uint64_t g_completionPump = 0;

EM_JS(int, DispatchStatRequest, (uint32_t requestId, const char *path), {
    const bridge = globalThis.__KISAKCOD_WEB_FS_BRIDGE__;
    if (!bridge || typeof bridge.stat !== "function") {
        return 0;
    }
    try {
        return bridge.stat(requestId >>> 0, UTF8ToString(path)) ? 1 : 0;
    } catch (error) {
        console.error("[kisakcod-web] Filesystem stat dispatch failed.", error);
        return 0;
    }
});

EM_JS(
    int,
    DispatchReadRequest,
    (uint32_t requestId,
     const char *path,
     uint32_t offset,
     uint32_t length,
     uint8_t *destination,
     uint32_t capacity),
    {
        const bridge = globalThis.__KISAKCOD_WEB_FS_BRIDGE__;
        if (!bridge || typeof bridge.read !== "function") {
            return 0;
        }
        try {
            return bridge.read(
                requestId >>> 0,
                UTF8ToString(path),
                offset >>> 0,
                length >>> 0,
                destination >>> 0,
                capacity >>> 0
            ) ? 1 : 0;
        } catch (error) {
            console.error("[kisakcod-web] Filesystem read dispatch failed.", error);
            return 0;
        }
    });

EM_JS(int, DispatchCancelRequest, (uint32_t requestId), {
    const bridge = globalThis.__KISAKCOD_WEB_FS_BRIDGE__;
    if (!bridge) {
        // A bridge may disappear only after dispose(), which synchronously
        // retires every live token first.
        return 1;
    }
    if (typeof bridge.cancel !== "function") {
        return 0;
    }
    try {
        return bridge.cancel(requestId >>> 0) ? 1 : 0;
    } catch (error) {
        console.error("[kisakcod-web] Filesystem cancellation failed.", error);
        return 0;
    }
});

void ResetSlot(RequestSlot &slot)
{
    slot.requestId = 0;
    slot.state = RequestState::Free;
    slot.operation = WebFsOperation::Stat;
    slot.completionStatus = WebFsStatus::Pending;
    slot.callback = nullptr;
    slot.userData = nullptr;
    slot.requestedLength = 0;
    slot.completedLength = 0;
    slot.fileSize = 0;
    slot.deliverOnPump = 0;
    slot.buffer.clear();
}

RequestSlot *AllocateSlot(std::size_t &slotIndex)
{
    for (std::size_t index = 0; index < g_requestSlots.size(); ++index)
    {
        RequestSlot &slot = g_requestSlots[index];
        if (slot.state != RequestState::Free)
        {
            continue;
        }

        slot.generation = slot.generation == MAX_SLOT_GENERATION ? 1u : slot.generation + 1u;
        slotIndex = index;
        slot.requestId = (slot.generation << SLOT_INDEX_BITS) |
            static_cast<uint32_t>(index + 1u);
        slot.state = RequestState::InFlight;
        return &slot;
    }
    return nullptr;
}

RequestSlot *FindSlot(WebFsRequestId requestId)
{
    const uint32_t encodedIndex = requestId & SLOT_INDEX_MASK;
    if (encodedIndex == 0 || encodedIndex > g_requestSlots.size())
    {
        return nullptr;
    }

    RequestSlot &slot = g_requestSlots[encodedIndex - 1u];
    return slot.state != RequestState::Free && slot.requestId == requestId ? &slot : nullptr;
}

bool IsBridgeCompletionStatus(WebFsStatus status)
{
    switch (status)
    {
    case WebFsStatus::Success:
    case WebFsStatus::NotReady:
    case WebFsStatus::InvalidArgument:
    case WebFsStatus::InvalidRange:
    case WebFsStatus::NotFound:
    case WebFsStatus::StaleSource:
    case WebFsStatus::IoError:
    case WebFsStatus::ProtocolError:
    case WebFsStatus::Cancelled:
        return true;
    case WebFsStatus::Pending:
    case WebFsStatus::NoRequestSlots:
        return false;
    }
    return false;
}

void QueueCompletion(RequestSlot &slot, WebFsStatus status)
{
    slot.completionStatus = IsBridgeCompletionStatus(status)
        ? status
        : WebFsStatus::ProtocolError;
    slot.state = RequestState::Completed;
    slot.deliverOnPump = g_completionPump + 1u;
}
} // namespace

WebFsStatus WebFs_BeginStat(
    const char *path,
    WebFsCompletionCallback callback,
    void *userData,
    WebFsRequestId *requestId)
{
    if (requestId)
    {
        *requestId = 0;
    }
    if (!path || path[0] == '\0' || !callback || !requestId)
    {
        return WebFsStatus::InvalidArgument;
    }

    std::size_t slotIndex = 0;
    RequestSlot *slot = AllocateSlot(slotIndex);
    if (!slot)
    {
        return WebFsStatus::NoRequestSlots;
    }
    (void)slotIndex;

    slot->operation = WebFsOperation::Stat;
    slot->callback = callback;
    slot->userData = userData;
    const WebFsRequestId allocatedId = slot->requestId;
    if (!DispatchStatRequest(allocatedId, path))
    {
        ResetSlot(*slot);
        return WebFsStatus::NotReady;
    }

    *requestId = allocatedId;
    return WebFsStatus::Pending;
}

WebFsStatus WebFs_BeginRead(
    const char *path,
    uint32_t offset,
    uint32_t length,
    WebFsCompletionCallback callback,
    void *userData,
    WebFsRequestId *requestId)
{
    if (requestId)
    {
        *requestId = 0;
    }
    if (!path || path[0] == '\0' || !callback || !requestId)
    {
        return WebFsStatus::InvalidArgument;
    }
    if (length == 0 || length > WEB_FS_MAX_READ_SIZE || offset > UINT32_MAX - length)
    {
        return WebFsStatus::InvalidRange;
    }

    std::size_t slotIndex = 0;
    RequestSlot *slot = AllocateSlot(slotIndex);
    if (!slot)
    {
        return WebFsStatus::NoRequestSlots;
    }
    (void)slotIndex;

    slot->operation = WebFsOperation::Read;
    slot->callback = callback;
    slot->userData = userData;
    slot->requestedLength = length;
    try
    {
        slot->buffer.resize(length);
    }
    catch (...)
    {
        ResetSlot(*slot);
        return WebFsStatus::IoError;
    }
    const WebFsRequestId allocatedId = slot->requestId;
    if (!DispatchReadRequest(
            allocatedId,
            path,
            offset,
            length,
            slot->buffer.data(),
            static_cast<uint32_t>(slot->buffer.size())))
    {
        ResetSlot(*slot);
        return WebFsStatus::NotReady;
    }

    *requestId = allocatedId;
    return WebFsStatus::Pending;
}

bool WebFs_Cancel(WebFsRequestId requestId)
{
    RequestSlot *slot = FindSlot(requestId);
    if (!slot || slot->state == RequestState::Delivering)
    {
        return false;
    }
    if (!DispatchCancelRequest(requestId))
    {
        return false;
    }
    ResetSlot(*slot);
    return true;
}

void WebFs_CancelAll()
{
    for (RequestSlot &slot : g_requestSlots)
    {
        if (slot.state != RequestState::Free && slot.state != RequestState::Delivering)
        {
            (void)WebFs_Cancel(slot.requestId);
        }
    }
}

bool WebFs_IsPending(WebFsRequestId requestId)
{
    const RequestSlot *slot = FindSlot(requestId);
    return slot && (slot->state == RequestState::InFlight || slot->state == RequestState::Completed);
}

void WebFs_PumpCompletions()
{
    ++g_completionPump;
    for (RequestSlot &slot : g_requestSlots)
    {
        if (slot.state != RequestState::Completed || slot.deliverOnPump > g_completionPump)
        {
            continue;
        }

        slot.state = RequestState::Delivering;
        const WebFsCompletion completion{
            slot.requestId,
            slot.operation,
            slot.completionStatus,
            slot.fileSize,
            slot.operation == WebFsOperation::Read &&
                    slot.completionStatus == WebFsStatus::Success
                ? slot.buffer.data()
                : nullptr,
            slot.operation == WebFsOperation::Read &&
                    slot.completionStatus == WebFsStatus::Success
                ? slot.completedLength
                : 0u,
        };
        WebFsCompletionCallback callback = slot.callback;
        void *userData = slot.userData;
        callback(completion, userData);
        ResetSlot(slot);
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_CompleteFsStat(
    WebFsRequestId requestId,
    int32_t status,
    uint32_t fileSize)
{
    RequestSlot *slot = FindSlot(requestId);
    if (!slot || slot->state != RequestState::InFlight ||
        slot->operation != WebFsOperation::Stat)
    {
        return;
    }

    const WebFsStatus completionStatus = static_cast<WebFsStatus>(status);
    slot->fileSize = completionStatus == WebFsStatus::Success ? fileSize : 0u;
    QueueCompletion(*slot, completionStatus);
}

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_CompleteFsRead(
    WebFsRequestId requestId,
    int32_t status,
    uint32_t bytesRead)
{
    RequestSlot *slot = FindSlot(requestId);
    if (!slot || slot->state != RequestState::InFlight ||
        slot->operation != WebFsOperation::Read)
    {
        return;
    }

    WebFsStatus completionStatus = static_cast<WebFsStatus>(status);
    if (completionStatus == WebFsStatus::Success &&
        (bytesRead != slot->requestedLength || bytesRead > slot->buffer.size()))
    {
        completionStatus = WebFsStatus::ProtocolError;
    }
    slot->completedLength = completionStatus == WebFsStatus::Success ? bytesRead : 0u;
    QueueCompletion(*slot, completionStatus);
}
