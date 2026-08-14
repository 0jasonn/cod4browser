#include <web/web_engine_asset.h>

#include <qcommon/iwi_image.h>
#include <web/web_engine_filesystem.h>
#include <web/web_renderer.h>

#include <emscripten.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
static_assert(
    kisak::iwi::MAX_TEXTURE_MEMBER_BYTES ==
    WEB_RENDERER_MAX_RETAINED_TEXTURE_BYTES);
static_assert(
    kisak::iwi::MAX_TEXTURE_MEMBER_BYTES ==
    WEB_ENGINE_FS_MAX_CACHED_MEMBER_BYTES);

enum class Phase
{
    Idle,
    NeedSelection,
    NeedRead,
    WaitingRead,
    Finished,
    Failed,
    Unavailable,
};

struct EngineAssetJob
{
    Phase phase = Phase::Idle;
    uint32_t generation = 0;
    WebEngineFsRequestId requestId = 0;
    std::string selectedPath;

    bool completionReady = false;
    bool completionSucceeded = false;
    std::string completionPath;
    uint32_t completionSize = 0;
    uint16_t completionMethod = 0;
    uint32_t completionCrc32 = 0;
    kisak::iwi::Metadata completionMetadata;
    kisak::iwi::Rgba8Image completionImage;
    kisak::iwi::Error completionTextureError = kisak::iwi::Error::None;
    std::string completionFailure;
};

EngineAssetJob g_job;

struct RendererTexturePublication
{
    std::string state;
    std::string message;
    uint32_t generation = 0;
    std::string path;
    uint32_t sourceFormat = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t payloadBytes = 0;
    WebRendererTextureState renderer{};
};

EM_JS(void, DispatchEngineAssetIdle, (uint32_t generation), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:engine-asset", {
        detail: {
            state: "idle",
            generation: generation >>> 0,
            message: "Waiting for a mounted engine filesystem"
        }
    }));
});

EM_JS(void, DispatchEngineAssetLoading, (uint32_t generation), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:engine-asset", {
        detail: {
            state: "loading",
            generation: generation >>> 0,
            message: "Loading one bounded engine image asset"
        }
    }));
});

EM_JS(
    void,
    DispatchEngineAssetReady,
    (uint32_t generation,
     const char *path,
     uint32_t size,
     uint32_t compressionMethod,
     uint32_t crc32,
     uint32_t format,
     uint32_t flags,
     uint32_t width,
     uint32_t height,
     uint32_t depth,
     uint32_t mipCount,
     uint32_t cacheRetainedBytes),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:engine-asset", {
            detail: {
                state: "ready",
                generation: generation >>> 0,
                message: "Loaded and parsed one IWI through the engine filesystem",
                path: UTF8ToString(path),
                kind: "iwi",
                size: size >>> 0,
                compressionMethod,
                crc32: crc32 >>> 0,
                format,
                flags,
                width,
                height,
                depth,
                mipCount,
                cacheRetainedBytes: cacheRetainedBytes >>> 0
            }
        }));
    });

EM_JS(
    void,
    DispatchRendererTextureState,
    (const char *state,
     uint32_t generation,
     const char *path,
     uint32_t sourceFormat,
     uint32_t width,
     uint32_t height,
     uint32_t payloadBytes,
     uint32_t recoveryBytes,
     uint32_t uploadGeneration,
     uint32_t resourceGeneration,
     uint32_t recoveryCount,
     bool resident,
     const char *message),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:renderer-texture", {
            detail: {
                state: UTF8ToString(state),
                generation: generation >>> 0,
                path: path ? UTF8ToString(path) : "",
                sourceFormat,
                width,
                height,
                mipLevel: 0,
                payloadBytes: payloadBytes >>> 0,
                gpuFormat: "rgba8",
                recoveryBytes: recoveryBytes >>> 0,
                uploadGeneration: uploadGeneration >>> 0,
                resourceGeneration: resourceGeneration >>> 0,
                recoveryCount: recoveryCount >>> 0,
                resident: Boolean(resident),
                message: UTF8ToString(message)
            }
        }));
    });

EM_JS(
    void,
    DispatchEngineAssetProblem,
    (const char *state, uint32_t generation, const char *message),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:engine-asset", {
            detail: {
                state: UTF8ToString(state),
                generation: generation >>> 0,
                message: UTF8ToString(message)
            }
        }));
    });

uint32_t NextGeneration(uint32_t generation) noexcept
{
    return generation == UINT32_MAX ? 1u : generation + 1u;
}

void ResetJob(uint32_t generation, Phase phase)
{
    g_job = {};
    g_job.generation = generation;
    g_job.phase = phase;
}

unsigned char AsciiLower(unsigned char value) noexcept
{
    if (value >= 'A' && value <= 'Z')
    {
        return static_cast<unsigned char>(value + ('a' - 'A'));
    }
    return value;
}

bool AsciiEqual(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index)
    {
        if (AsciiLower(static_cast<unsigned char>(lhs[index])) !=
            AsciiLower(static_cast<unsigned char>(rhs[index])))
        {
            return false;
        }
    }
    return true;
}

bool IsIwiPath(std::string_view path) noexcept
{
    constexpr std::string_view prefix = "images/";
    constexpr std::string_view suffix = ".iwi";
    return path.size() > prefix.size() + suffix.size() &&
        AsciiEqual(path.substr(0, prefix.size()), prefix) &&
        AsciiEqual(path.substr(path.size() - suffix.size()), suffix);
}

bool AsciiLess(std::string_view lhs, std::string_view rhs) noexcept
{
    const std::size_t commonLength = lhs.size() < rhs.size() ? lhs.size() : rhs.size();
    for (std::size_t index = 0; index < commonLength; ++index)
    {
        const unsigned char left = AsciiLower(static_cast<unsigned char>(lhs[index]));
        const unsigned char right = AsciiLower(static_cast<unsigned char>(rhs[index]));
        if (left != right)
        {
            return left < right;
        }
    }
    if (lhs.size() != rhs.size())
    {
        return lhs.size() < rhs.size();
    }
    return lhs < rhs;
}

const kisak::iwd::Entry *SelectIwiEntry()
{
    const kisak::iwd::Entry *selected = nullptr;
    for (const kisak::iwd::Entry &entry : WebEngineFs_Entries())
    {
        if (entry.directory || entry.uncompressedSize <= kisak::iwi::HEADER_SIZE ||
            entry.uncompressedSize > WEB_ENGINE_FS_MAX_CACHED_MEMBER_BYTES ||
            !IsIwiPath(entry.path))
        {
            continue;
        }
        if (!selected || AsciiLess(entry.path, selected->path))
        {
            selected = &entry;
        }
    }
    return selected;
}

void SetFailure(std::string message)
{
    g_job.completionSucceeded = false;
    g_job.completionFailure = std::move(message);
    g_job.completionReady = true;
}

RendererTexturePublication CaptureRendererTexturePublication(
    const char *state,
    const char *message,
    uint32_t payloadBytes = 0u)
{
    RendererTexturePublication publication;
    publication.state = state;
    publication.message = message;
    publication.generation = g_job.generation;
    publication.path = g_job.completionPath.empty()
        ? g_job.selectedPath
        : g_job.completionPath;
    publication.sourceFormat = g_job.completionMetadata.format;
    publication.width = g_job.completionMetadata.width;
    publication.height = g_job.completionMetadata.height;
    publication.payloadBytes = payloadBytes;
    publication.renderer = WebRenderer_GetBootstrapTextureState();
    return publication;
}

void DispatchRendererTexturePublication(
    const RendererTexturePublication &publication)
{
    DispatchRendererTextureState(
        publication.state.c_str(),
        publication.generation,
        publication.path.c_str(),
        publication.sourceFormat,
        publication.width,
        publication.height,
        publication.payloadBytes,
        publication.renderer.retainedByteCount <= UINT32_MAX
            ? static_cast<uint32_t>(publication.renderer.retainedByteCount)
            : 0u,
        publication.renderer.uploadGeneration,
        publication.renderer.rebuildGeneration,
        publication.renderer.recoveryCount,
        publication.renderer.resident,
        publication.message.c_str());
}

bool JobStillMatches(uint32_t generation, Phase phase) noexcept
{
    return g_job.generation == generation && g_job.phase == phase;
}

bool IsUnsupportedTextureSlice(kisak::iwi::Error error) noexcept
{
    return error == kisak::iwi::Error::DecodeUnsupportedFormat ||
        error == kisak::iwi::Error::DecodeUnsupportedFlags ||
        error == kisak::iwi::Error::DecodeUnsupportedDimensions ||
        error == kisak::iwi::Error::DecodeOutputTooLarge;
}

bool IsUnsupportedRendererResult(WebRendererTextureResult result) noexcept
{
    return result == WebRendererTextureResult::UnsupportedDimensions ||
        result == WebRendererTextureResult::OutputTooLarge;
}

std::string CompletionFailureMessage(const WebEngineFsCompletion &completion)
{
    std::string message = "Could not read engine IWI: ";
    message += WebEngineFs_StatusString(completion.status);
    if (completion.status == WebEngineFsStatus::ArchiveError &&
        completion.archiveError != kisak::iwd::Error::None)
    {
        message += " (";
        message += kisak::iwd::ErrorString(completion.archiveError);
        message += ')';
    }
    return message;
}

void CompleteRead(const WebEngineFsCompletion &completion, void *userData)
{
    const uint32_t requestGeneration = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(userData));
    if (requestGeneration != g_job.generation ||
        completion.requestId != g_job.requestId)
    {
        return;
    }

    g_job.requestId = 0;
    if (completion.status != WebEngineFsStatus::Success)
    {
        SetFailure(CompletionFailureMessage(completion));
        return;
    }
    if (!completion.data || completion.dataLength < kisak::iwi::HEADER_SIZE)
    {
        SetFailure("Could not parse engine IWI: successful read returned no complete header");
        return;
    }

    kisak::iwi::Metadata metadata;
    const kisak::iwi::Error parseError = kisak::iwi::Parse(
        std::span<const uint8_t>(completion.data, completion.dataLength), metadata);
    if (parseError != kisak::iwi::Error::None)
    {
        std::string message = "Could not parse engine IWI: ";
        message += kisak::iwi::ErrorString(parseError);
        SetFailure(std::move(message));
        return;
    }

    kisak::iwi::Rgba8Image decodedImage;
    const kisak::iwi::Error textureError = kisak::iwi::DecodeRgba8(
        std::span<const uint8_t>(completion.data, completion.dataLength),
        decodedImage);

    g_job.completionSucceeded = true;
    g_job.completionPath = completion.path ? completion.path : g_job.selectedPath;
    g_job.completionSize = completion.dataLength;
    g_job.completionMethod = completion.compressionMethod;
    g_job.completionCrc32 = completion.crc32;
    g_job.completionMetadata = metadata;
    g_job.completionImage = std::move(decodedImage);
    g_job.completionTextureError = textureError;
    g_job.completionReady = true;
}

void PublishUnavailable(const char *message)
{
    g_job.phase = Phase::Unavailable;
    const uint32_t generation = g_job.generation;
    const RendererTexturePublication texture =
        CaptureRendererTexturePublication("unavailable", message);
    DispatchEngineAssetProblem("unavailable", generation, message);
    if (JobStillMatches(generation, Phase::Unavailable))
    {
        DispatchRendererTexturePublication(texture);
    }
}

void PublishFailure(const char *message)
{
    g_job.phase = Phase::Failed;
    const uint32_t generation = g_job.generation;
    const RendererTexturePublication texture =
        CaptureRendererTexturePublication("failed", message);
    DispatchEngineAssetProblem("failed", generation, message);
    if (JobStillMatches(generation, Phase::Failed))
    {
        DispatchRendererTexturePublication(texture);
    }
}

void ConsumeCompletion()
{
    if (!g_job.completionReady)
    {
        return;
    }
    g_job.completionReady = false;
    if (!g_job.completionSucceeded)
    {
        PublishFailure(g_job.completionFailure.c_str());
        return;
    }

    // The engine filesystem tears down its request cache before invoking the
    // callback. Publishing here, after WebEngineFs_Frame has returned, keeps
    // synchronous JavaScript listeners outside the byte-cache lifetime.
    const std::size_t retainedBytes = WebEngineFs_RetainedByteCount();
    if (retainedBytes > UINT32_MAX)
    {
        PublishFailure("Engine filesystem retained-byte count overflowed");
        return;
    }

    RendererTexturePublication texture;
    if (g_job.completionTextureError != kisak::iwi::Error::None)
    {
        std::string message = "The IWI metadata is valid, but its texture was not uploaded: ";
        message += kisak::iwi::ErrorString(g_job.completionTextureError);
        texture = CaptureRendererTexturePublication(
            IsUnsupportedTextureSlice(g_job.completionTextureError)
                ? "unsupported"
                : "failed",
            message.c_str());
    }
    else if (g_job.completionImage.pixels.size() > UINT32_MAX)
    {
        g_job.completionImage = {};
        texture = CaptureRendererTexturePublication(
            "failed",
            "The decoded RGBA8 payload size could not be represented");
    }
    else
    {
        const uint32_t payloadBytes = static_cast<uint32_t>(
            g_job.completionImage.pixels.size());
        const WebRendererRgba8TextureDesc upload{
            g_job.completionImage.width,
            g_job.completionImage.height,
            g_job.completionImage.pixels.data(),
            g_job.completionImage.pixels.size(),
        };
        const WebRendererTextureResult uploadResult =
            WebRenderer_SetBootstrapTexture(upload);
        g_job.completionImage = {};
        if (uploadResult != WebRendererTextureResult::Success)
        {
            std::string message = "The bounded RGBA8 texture was not uploaded: ";
            message += WebRenderer_TextureResultString(uploadResult);
            texture = CaptureRendererTexturePublication(
                IsUnsupportedRendererResult(uploadResult)
                    ? "unsupported"
                    : "failed",
                message.c_str(),
                payloadBytes);
        }
        else
        {
            const WebRendererTextureState renderer =
                WebRenderer_GetBootstrapTextureState();
            texture = CaptureRendererTexturePublication(
                renderer.resident ? "ready" : "retained",
                renderer.resident
                    ? "The engine IWI was uploaded through the renderer-owned texture path"
                    : "The engine IWI is retained for upload when the WebGL2 context returns",
                payloadBytes);
        }
    }

    const uint32_t generation = g_job.generation;
    g_job.phase = Phase::Finished;
    DispatchEngineAssetReady(
        generation,
        g_job.completionPath.c_str(),
        g_job.completionSize,
        g_job.completionMethod,
        g_job.completionCrc32,
        g_job.completionMetadata.format,
        g_job.completionMetadata.flags,
        g_job.completionMetadata.width,
        g_job.completionMetadata.height,
        g_job.completionMetadata.depth,
        g_job.completionMetadata.mipCount,
        static_cast<uint32_t>(retainedBytes));
    if (JobStillMatches(generation, Phase::Finished))
    {
        DispatchRendererTexturePublication(texture);
    }
}
} // namespace

void WebEngineAsset_Start()
{
    if (g_job.requestId != 0 && !WebEngineFs_Cancel(g_job.requestId))
    {
        PublishFailure("Could not retire the previous engine asset request safely");
        return;
    }
    const uint32_t generation = NextGeneration(g_job.generation);
    const bool rendererCleared = WebRenderer_ClearBootstrapTexture();
    ResetJob(generation, Phase::NeedSelection);
    const RendererTexturePublication texture = CaptureRendererTexturePublication(
        "loading",
        rendererCleared
            ? "Waiting for one supported bounded IWI texture"
            : "Previous recovery pixels were released; the renderer fallback is not resident");
    DispatchEngineAssetLoading(generation);
    if (JobStillMatches(generation, Phase::NeedSelection))
    {
        DispatchRendererTexturePublication(texture);
    }
}

bool WebEngineAsset_Cancel()
{
    if (g_job.requestId != 0 && !WebEngineFs_Cancel(g_job.requestId))
    {
        PublishFailure("Could not cancel the active engine asset request safely");
        return false;
    }
    const uint32_t generation = NextGeneration(g_job.generation);
    const bool rendererCleared = WebRenderer_ClearBootstrapTexture();
    ResetJob(generation, Phase::Idle);
    const RendererTexturePublication texture = CaptureRendererTexturePublication(
        rendererCleared ? "idle" : "failed",
        rendererCleared
            ? "Waiting for a mounted engine image"
            : "Imported recovery pixels were released, but the renderer fallback is not resident");
    DispatchEngineAssetIdle(generation);
    if (JobStillMatches(generation, Phase::Idle))
    {
        DispatchRendererTexturePublication(texture);
    }
    return true;
}

void WebEngineAsset_Frame()
{
    // Low-level WebFs completions are pumped by the owning engine frame before
    // this function. Advance the cache service first, then publish any copied
    // completion metadata only after the service has released decoded bytes.
    WebEngineFs_Frame();

    if (g_job.phase == Phase::WaitingRead && g_job.completionReady)
    {
        ConsumeCompletion();
        return;
    }

    switch (g_job.phase)
    {
    case Phase::Idle:
    case Phase::Finished:
    case Phase::Failed:
    case Phase::Unavailable:
        return;
    case Phase::NeedSelection:
    {
        if (!WebEngineFs_IsMounted())
        {
            PublishUnavailable("The engine filesystem is not mounted");
            return;
        }
        const kisak::iwd::Entry *entry = SelectIwiEntry();
        if (!entry)
        {
            PublishUnavailable("No bounded images/*.iwi member is available");
            return;
        }
        g_job.selectedPath = entry->path;
        g_job.phase = Phase::NeedRead;
        return;
    }
    case Phase::NeedRead:
    {
        const WebEngineFsStatus status = WebEngineFs_BeginRead(
            g_job.selectedPath.c_str(),
            WEB_ENGINE_FS_MAX_CACHED_MEMBER_BYTES,
            CompleteRead,
            reinterpret_cast<void *>(static_cast<uintptr_t>(g_job.generation)),
            &g_job.requestId);
        if (status == WebEngineFsStatus::Pending)
        {
            g_job.phase = Phase::WaitingRead;
            return;
        }
        if (status == WebEngineFsStatus::NotMounted ||
            status == WebEngineFsStatus::NotFound)
        {
            std::string message = "Engine IWI became unavailable: ";
            message += WebEngineFs_StatusString(status);
            PublishUnavailable(message.c_str());
            return;
        }
        std::string message = "Could not begin engine IWI read: ";
        message += WebEngineFs_StatusString(status);
        PublishFailure(message.c_str());
        return;
    }
    case Phase::WaitingRead:
        if (!WebEngineFs_IsMounted())
        {
            if (g_job.requestId != 0)
            {
                (void)WebEngineFs_Cancel(g_job.requestId);
                g_job.requestId = 0;
            }
            PublishUnavailable("The mounted engine filesystem was removed during the read");
        }
        return;
    }
}
