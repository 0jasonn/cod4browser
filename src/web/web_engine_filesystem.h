#pragma once

#include <qcommon/iwd_archive.h>
#include <web/web_filesystem.h>

#include <cstddef>
#include <cstdint>
#include <span>

// The first engine-facing browser filesystem contract is intentionally
// narrow: one mounted IWD, one asynchronous request, and read-all results
// whose storage exists only while the completion callback is running.
using WebEngineFsRequestId = uint32_t;

inline constexpr uint32_t WEB_ENGINE_FS_MAX_CACHED_MEMBER_BYTES = 8u * 1024u * 1024u;

enum class WebEngineFsOperation : uint8_t
{
    Read = 1,
    Verify = 2,
};

enum class WebEngineFsStatus : uint8_t
{
    Success = 0,
    Pending,
    NotMounted,
    InvalidArgument,
    Busy,
    NotFound,
    OutputLimit,
    Cancelled,
    StaleSource,
    FilesystemError,
    ArchiveError,
    InternalError,
};

struct WebEngineFsCompletion
{
    WebEngineFsRequestId requestId = 0;
    WebEngineFsOperation operation = WebEngineFsOperation::Read;
    WebEngineFsStatus status = WebEngineFsStatus::InternalError;

    // These preserve the lower-layer reason without requiring callers to
    // parse a diagnostic string. They are Success/None when not applicable.
    WebFsStatus filesystemStatus = WebFsStatus::Success;
    kisak::iwd::Error archiveError = kisak::iwd::Error::None;

    // path is the canonical spelling stored in the archive. memberSize is
    // populated for both operations. For a successful Read, data points to
    // dataLength decoded bytes and data[dataLength] is a convenience NUL
    // terminator. Both pointers are callback-lifetime only and must be
    // copied by a caller that needs longer ownership.
    const char *path = nullptr;
    const uint8_t *data = nullptr;
    uint32_t dataLength = 0;
    uint32_t memberSize = 0;
    uint16_t compressionMethod = 0;
    uint32_t crc32 = 0;
};

using WebEngineFsCompletionCallback =
    void (*)(const WebEngineFsCompletion &completion, void *userData);

const char *WebEngineFs_StatusString(WebEngineFsStatus status) noexcept;

// Mount takes ownership of the parsed index. The locator and index must have
// been produced from the same immutable browser-VFS archivePath. Mounting
// replaces any idle mount; an active request must be cancelled or allowed to
// finish first.
WebEngineFsStatus WebEngineFs_Mount(
    const char *archivePath,
    const kisak::iwd::CentralDirectoryLocator &locator,
    kisak::iwd::ArchiveIndex &&index);

// Unmount suppresses any active high-level callback. It returns false only if
// an in-flight WebFs operation could not be retired safely.
bool WebEngineFs_Unmount();
bool WebEngineFs_IsMounted();
std::span<const kisak::iwd::Entry> WebEngineFs_Entries();
const kisak::iwd::Entry *WebEngineFs_Find(const char *path);

// At most one read or verification may be active. Accepted calls return
// Pending and complete from WebEngineFs_Frame on a later frame. BeginRead
// applies both maxOutputBytes and the 4 MiB cache ceiling. BeginVerify retains
// only a small decode scratch buffer and accepts the portable archive reader's
// member limit.
WebEngineFsStatus WebEngineFs_BeginRead(
    const char *path,
    uint32_t maxOutputBytes,
    WebEngineFsCompletionCallback callback,
    void *userData,
    WebEngineFsRequestId *requestId);

WebEngineFsStatus WebEngineFs_BeginVerify(
    const char *path,
    WebEngineFsCompletionCallback callback,
    void *userData,
    WebEngineFsRequestId *requestId);

// Successful cancellation guarantees that the callback will not run and its
// userData may be released immediately.
bool WebEngineFs_Cancel(WebEngineFsRequestId requestId);

// Advances at most one bounded decoder step. A step presents no more than one
// 64 KiB compressed source chunk and 64 KiB of output, so even high-ratio
// deflate members yield across browser frames.
void WebEngineFs_Frame();

// Sum of capacities held by active request byte vectors. It is zero while
// idle, including from inside a completion callback, because request state is
// detached and reset before callbacks are invoked.
std::size_t WebEngineFs_RetainedByteCount();
