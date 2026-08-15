#include <web/web_retail_census_job.h>

#include <web/web_filesystem.h>
#include <web/web_retail_fastfile_census.h>
#include <web/web_renderer.h>
#include <web/web_shader_compatibility.h>
#include <web/web_system.h>

#include <emscripten.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace
{
constexpr const char *FASTFILE_PATH = "zone/english/code_post_gfx.ff";

enum class Phase : std::uint8_t
{
    Idle,
    NeedStat,
    WaitingStat,
    NeedRead,
    WaitingRead,
    Parse,
    Finished,
    Failed,
};

struct RetailCensusRuntime
{
    Phase phase = Phase::Idle;
    std::uint32_t generation = 0u;
    WebFsRequestId requestId = 0u;
    std::uint32_t fileSize = 0u;
    std::uint32_t readOffset = 0u;
    bool completionReady = false;
    WebFsStatus completionStatus = WebFsStatus::Pending;
    std::vector<std::uint8_t> completionBytes;
    kisak::fastfile::RetailFastfileCensusJob parser;
    kisak::fastfile::RetailFastfileCensus result;
};

RetailCensusRuntime g_runtime;

EM_JS(void, DispatchRetailCensusLoading, (uint32_t generation, const char *stage), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:retail-census", {
        detail: {
            state: "loading",
            stage: UTF8ToString(stage),
            generation: generation >>> 0,
            path: "zone/english/code_post_gfx.ff",
            message: "Reading a bounded retail fastfile prefix through the browser VFS",
            maxSourceChunkBytes: 64 * 1024,
            maxInflatedPrefixBytes: 256 * 1024,
            maxStepBytes: 64 * 1024,
            maxStepRecords: 64,
            assetBodyTraversal: "leading-technique-set-only"
        }
    }));
});

EM_JS(
    void,
    BeginRetailCensusReady,
    (uint32_t generation,
     uint32_t fileSize,
     uint32_t sourceBytesRead,
     double sourceBytesConsumed,
     uint32_t sourceFeedCount,
     uint32_t version,
     uint32_t xfileSize,
     uint32_t externalSize,
     double declaredBlockBytes,
     uint32_t scriptStringCount,
     uint32_t scriptStringBytes,
     uint32_t assetCount,
     uint32_t inflatedPrefixBytes,
     uint32_t inlineReferences,
     uint32_t sharedReferences,
     uint32_t aliasReferences,
     uint32_t nullReferences,
     uint32_t firstBodyIndex,
     uint32_t firstBodyType,
     const char *firstBodyTypeName,
     uint32_t firstBodyReference),
    {
        globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__ = {
            state: "ready",
            stage: "asset-boundary",
            generation: generation >>> 0,
            path: "zone/english/code_post_gfx.ff",
            message: "Leading technique set published with an explicit WebGL2 shader substitution",
            fileSize: fileSize >>> 0,
            sourceBytesRead: sourceBytesRead >>> 0,
            sourceBytesConsumed,
            sourceFeedCount: sourceFeedCount >>> 0,
            version: version >>> 0,
            xfileSize: xfileSize >>> 0,
            externalSize: externalSize >>> 0,
            declaredBlockBytes,
            scriptStringCount: scriptStringCount >>> 0,
            scriptStringBytes: scriptStringBytes >>> 0,
            assetCount: assetCount >>> 0,
            inflatedPrefixBytes: inflatedPrefixBytes >>> 0,
            inlineReferences: inlineReferences >>> 0,
            sharedReferences: sharedReferences >>> 0,
            aliasReferences: aliasReferences >>> 0,
            nullReferences: nullReferences >>> 0,
            firstTraversedAssetIndex: firstBodyIndex >>> 0,
            firstTraversedAssetType: firstBodyType >>> 0,
            firstTraversedAssetTypeName: UTF8ToString(firstBodyTypeName),
            firstTraversedAssetReference: firstBodyReference >>> 0,
            stoppedBeforeAssetBody: false,
            assetBodiesEntered: 1,
            maxSourceChunkBytes: 64 * 1024,
            maxInflatedPrefixBytes: 256 * 1024,
            maxStepBytes: 64 * 1024,
            maxStepRecords: 64,
            blockSizes: [],
            typeCounts: []
        };
    });

EM_JS(
    void,
    AppendRetailTechniqueTraversal,
    (const char *techniqueSetName,
     uint32_t firstTechniqueSlot,
     uint32_t techniquePassCount,
     uint32_t vertexStreamCount,
     uint32_t vertexStreamRoutingHash,
     const char *vertexShaderName,
     uint32_t vertexShaderProgramDwords,
     uint32_t vertexShaderProgramHash,
     uint32_t assetTableBlock4Offset,
     uint32_t techniqueSetBlock0Offset,
     uint32_t techniqueBlock4Offset,
     uint32_t vertexDeclarationBlock4Offset,
     uint32_t vertexShaderBlock4Offset,
     uint32_t vertexShaderProgramBlock4Offset,
     uint32_t block0HighWater,
     uint32_t block4Cursor,
     uint32_t completedAssetCount,
     int techniqueSetPublished,
     int vertexDeclarationPrepared,
     int stoppedBeforeShaderCreation,
     const char *unsupportedOperation),
    {
        const detail = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
        if (detail) {
            detail.techniqueSetName = UTF8ToString(techniqueSetName);
            detail.firstTechniqueSlot = firstTechniqueSlot >>> 0;
            detail.techniquePassCount = techniquePassCount >>> 0;
            detail.vertexStreamCount = vertexStreamCount >>> 0;
            detail.vertexStreamRoutingHash = vertexStreamRoutingHash >>> 0;
            detail.vertexShaderName = UTF8ToString(vertexShaderName);
            detail.vertexShaderProgramDwords = vertexShaderProgramDwords >>> 0;
            detail.vertexShaderProgramHash = vertexShaderProgramHash >>> 0;
            detail.assetTableBlock4Offset = assetTableBlock4Offset >>> 0;
            detail.techniqueSetBlock0Offset = techniqueSetBlock0Offset >>> 0;
            detail.techniqueBlock4Offset = techniqueBlock4Offset >>> 0;
            detail.vertexDeclarationBlock4Offset = vertexDeclarationBlock4Offset >>> 0;
            detail.vertexShaderBlock4Offset = vertexShaderBlock4Offset >>> 0;
            detail.vertexShaderProgramBlock4Offset = vertexShaderProgramBlock4Offset >>> 0;
            detail.block0HighWaterAtBoundary = block0HighWater >>> 0;
            detail.block4CursorAtBoundary = block4Cursor >>> 0;
            detail.completedAssetCount = completedAssetCount >>> 0;
            detail.techniqueSetPublished = Boolean(techniqueSetPublished);
            detail.vertexDeclarationPrepared = Boolean(vertexDeclarationPrepared);
            detail.stoppedBeforeShaderCreation = Boolean(stoppedBeforeShaderCreation);
            detail.unsupportedOperation = stoppedBeforeShaderCreation
                ? UTF8ToString(unsupportedOperation)
                : null;
            detail.traversesAssetBodies = true;
        }
    });

EM_JS(
    void,
    AppendRetailShaderCompatibility,
    (const char *pixelShaderName,
     uint32_t vertexInstructionCount,
     uint32_t vertexConstantCount,
     uint32_t pixelProgramDwords,
     uint32_t pixelProgramHash,
     uint32_t pixelInstructionCount,
     uint32_t pixelConstantCount,
     uint32_t shaderArgumentCount,
     uint32_t shaderArgumentHash,
     const char *techniqueName,
     const char *substitutionId,
     uint32_t vertexGlslHash,
     uint32_t fragmentGlslHash,
     uint32_t pixelShaderBlock4Offset,
     uint32_t pixelProgramBlock4Offset,
     uint32_t argumentBlock4Offset,
     int compatibilitySelected),
    {
        const detail = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
        if (detail) {
            detail.pixelShaderName = UTF8ToString(pixelShaderName);
            detail.vertexShaderInstructionCount = vertexInstructionCount >>> 0;
            detail.vertexShaderConstantCount = vertexConstantCount >>> 0;
            detail.pixelShaderProgramDwords = pixelProgramDwords >>> 0;
            detail.pixelShaderProgramHash = pixelProgramHash >>> 0;
            detail.pixelShaderInstructionCount = pixelInstructionCount >>> 0;
            detail.pixelShaderConstantCount = pixelConstantCount >>> 0;
            detail.shaderArgumentCount = shaderArgumentCount >>> 0;
            detail.shaderArgumentHash = shaderArgumentHash >>> 0;
            detail.techniqueName = UTF8ToString(techniqueName);
            detail.shaderSubstitutionId = UTF8ToString(substitutionId);
            detail.vertexGlslHash = vertexGlslHash >>> 0;
            detail.fragmentGlslHash = fragmentGlslHash >>> 0;
            detail.pixelShaderBlock4Offset = pixelShaderBlock4Offset >>> 0;
            detail.pixelShaderProgramBlock4Offset = pixelProgramBlock4Offset >>> 0;
            detail.shaderArgumentsBlock4Offset = argumentBlock4Offset >>> 0;
            detail.shaderCompatibilitySelected = Boolean(compatibilitySelected);
        }
    });

EM_JS(void, AppendRetailCensusBlock, (uint32_t block, uint32_t size), {
    globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.blockSizes.push({
        block: block >>> 0,
        size: size >>> 0
    });
});

EM_JS(
    void,
    AppendRetailCensusType,
    (uint32_t type, const char *name, uint32_t count),
    {
        globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.typeCounts.push({
            type: type >>> 0,
            name: UTF8ToString(name),
            count: count >>> 0
        });
    });

EM_JS(void, EndRetailCensusReady, (), {
    const detail = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
    delete globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
    if (detail) {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:retail-census", { detail }));
    }
});

EM_JS(void, DiscardRetailCensusReady, (), {
    delete globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
});

EM_JS(
    void,
    DispatchRetailCensusFailure,
    (uint32_t generation, const char *stage, const char *error, const char *message),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:retail-census", {
            detail: {
                state: "failed",
                stage: UTF8ToString(stage),
                generation: generation >>> 0,
                path: "zone/english/code_post_gfx.ff",
                error: UTF8ToString(error),
                message: UTF8ToString(message),
                completedAssetCount: 0,
                techniqueSetPublished: false,
                failClosed: true
            }
        }));
    });

EM_JS(void, DispatchRetailCensusIdle, (uint32_t generation), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:retail-census", {
        detail: {
            state: "idle",
            stage: "idle",
            generation: generation >>> 0,
            path: "zone/english/code_post_gfx.ff",
            message: "Waiting for qcommon pre-database startup",
            assetBodyTraversal: "leading-technique-set-only"
        }
    }));
});

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
    case WebFsStatus::NotFound: return "code_post_gfx.ff was not found";
    case WebFsStatus::StaleSource: return "browser asset import changed during census";
    case WebFsStatus::IoError: return "browser filesystem I/O failed";
    case WebFsStatus::ProtocolError: return "browser filesystem protocol failed";
    case WebFsStatus::Cancelled: return "filesystem request was cancelled";
    }
    return "unknown filesystem error";
}

void Reset(bool keepGeneration)
{
    const std::uint32_t generation = keepGeneration ? g_runtime.generation : 0u;
    g_runtime = {};
    g_runtime.generation = generation;
}

void CompleteRequest(const WebFsCompletion &completion, void *userData)
{
    const auto generation = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(userData));
    if (generation != g_runtime.generation || completion.requestId != g_runtime.requestId)
        return;
    g_runtime.requestId = 0u;
    g_runtime.completionReady = true;
    g_runtime.completionStatus = completion.status;
    if (completion.status != WebFsStatus::Success) return;
    if (completion.operation == WebFsOperation::Stat)
    {
        g_runtime.fileSize = completion.fileSize;
        return;
    }
    try
    {
        g_runtime.completionBytes.assign(
            completion.data, completion.data + completion.dataLength);
    }
    catch (...)
    {
        g_runtime.completionStatus = WebFsStatus::IoError;
    }
}

void Fail(const char *context, const char *reason)
{
    char message[384]{};
    std::snprintf(message, sizeof(message), "%s: %s", context, reason);
    message[sizeof(message) - 1u] = '\0';
    if (g_runtime.requestId != 0u)
    {
        (void)WebFs_Cancel(g_runtime.requestId);
        g_runtime.requestId = 0u;
    }
    g_runtime.phase = Phase::Failed;
    const auto stage = g_runtime.parser.Stage();
    DispatchRetailCensusFailure(
        g_runtime.generation,
        kisak::fastfile::RetailCensusStageString(stage),
        reason,
        message);
    Web_Log(WebLogLevel::Error, "[kisakcod-web] Retail fastfile census failed: %s\n", message);
}

void PublishReady()
{
    const auto &result = g_runtime.result;
    kisak::web::WebGL2ShaderSubstitution substitution;
    if (!kisak::web::LookupWebGL2ShaderSubstitution(
            result.shaderSubstitutionId, substitution) ||
        substitution.vertexSourceHash != result.vertexGlslHash ||
        substitution.fragmentSourceHash != result.fragmentGlslHash)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Published shader compatibility metadata did not resolve to its registry source.\n");
    }
    else
    {
        const WebRendererShaderResult shaderResult =
            WebRenderer_SetShaderCompatibility(substitution);
        if (shaderResult != WebRendererShaderResult::Success)
        {
            Web_Log(
                WebLogLevel::Error,
                "[kisakcod-web] Renderer rejected %s: %s.\n",
                substitution.id,
                WebRenderer_ShaderResultString(shaderResult));
        }
    }
    BeginRetailCensusReady(
        g_runtime.generation,
        g_runtime.fileSize,
        g_runtime.readOffset,
        static_cast<double>(result.sourceBytesConsumed),
        result.sourceFeedCount,
        result.version,
        result.xfileSize,
        result.externalSize,
        static_cast<double>(result.declaredBlockBytes),
        result.scriptStringCount,
        result.scriptStringBytes,
        result.assetCount,
        result.inflatedPrefixBytes,
        result.inlineAssetReferences,
        result.sharedAssetReferences,
        result.aliasAssetReferences,
        result.nullAssetReferences,
        result.firstBodyIndex,
        result.firstBodyType,
        kisak::fastfile::RetailAssetTypeName(result.firstBodyType),
        result.firstBodyReference);
    AppendRetailTechniqueTraversal(
        result.techniqueSetName.c_str(),
        result.firstTechniqueSlot,
        result.techniquePassCount,
        result.vertexStreamCount,
        result.vertexStreamRoutingHash,
        result.vertexShaderName.c_str(),
        result.vertexShaderProgramDwords,
        result.vertexShaderProgramHash,
        result.assetTableBlock4Offset,
        result.techniqueSetBlock0Offset,
        result.techniqueBlock4Offset,
        result.vertexDeclarationBlock4Offset,
        result.vertexShaderBlock4Offset,
        result.vertexShaderProgramBlock4Offset,
        result.block0HighWaterAtBoundary,
        result.block4CursorAtBoundary,
        result.completedAssetCount,
        result.techniqueSetPublished ? 1 : 0,
        result.vertexDeclarationPrepared ? 1 : 0,
        result.stoppedBeforeShaderCreation ? 1 : 0,
        result.unsupportedOperation ? result.unsupportedOperation : "unknown");
    AppendRetailShaderCompatibility(
        result.pixelShaderName.c_str(),
        result.vertexShaderInstructionCount,
        result.vertexShaderConstantCount,
        result.pixelShaderProgramDwords,
        result.pixelShaderProgramHash,
        result.pixelShaderInstructionCount,
        result.pixelShaderConstantCount,
        result.shaderArgumentCount,
        result.shaderArgumentHash,
        result.techniqueName.c_str(),
        result.shaderSubstitutionId.c_str(),
        result.vertexGlslHash,
        result.fragmentGlslHash,
        result.pixelShaderBlock4Offset,
        result.pixelShaderProgramBlock4Offset,
        result.shaderArgumentsBlock4Offset,
        result.shaderCompatibilitySelected ? 1 : 0);
    for (std::uint32_t block = 0u; block < result.blockSizes.size(); ++block)
        AppendRetailCensusBlock(block, result.blockSizes[block]);
    for (std::uint32_t type = 0u; type < result.typeCounts.size(); ++type)
    {
        if (result.typeCounts[type] != 0u)
            AppendRetailCensusType(
                type, kisak::fastfile::RetailAssetTypeName(type), result.typeCounts[type]);
    }
    EndRetailCensusReady();
    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] Retail census found %u assets; published %s with a WebGL2 shader substitution.\n",
        result.assetCount,
        kisak::fastfile::RetailAssetTypeName(result.firstBodyType));
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_StartRetailCensus()
{
    WebRetailCensusJob_Start();
}

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_CancelRetailCensus()
{
    WebRetailCensusJob_Cancel();
}

void WebRetailCensusJob_Start()
{
    DiscardRetailCensusReady();
    (void)WebRenderer_ClearShaderCompatibility();
    if (g_runtime.requestId != 0u) (void)WebFs_Cancel(g_runtime.requestId);
    const std::uint32_t generation = g_runtime.generation == UINT32_MAX
        ? 1u : g_runtime.generation + 1u;
    Reset(false);
    g_runtime.generation = generation;
    if (const auto error = g_runtime.parser.BeginStreaming();
        error != kisak::fastfile::RetailCensusError::None)
    {
        Fail("could not start retail census", kisak::fastfile::RetailCensusErrorString(error));
        return;
    }
    g_runtime.phase = Phase::NeedStat;
    DispatchRetailCensusLoading(generation, "stat");
}

void WebRetailCensusJob_Cancel()
{
    DiscardRetailCensusReady();
    (void)WebRenderer_ClearShaderCompatibility();
    if (g_runtime.requestId != 0u) (void)WebFs_Cancel(g_runtime.requestId);
    const std::uint32_t generation = g_runtime.generation == UINT32_MAX
        ? 1u : g_runtime.generation + 1u;
    Reset(false);
    g_runtime.generation = generation;
    DispatchRetailCensusIdle(generation);
}

WebRetailCensusFrameResult WebRetailCensusJob_Frame()
{
    using namespace kisak::fastfile;
    switch (g_runtime.phase)
    {
    case Phase::Idle:
    case Phase::Finished:
    case Phase::Failed:
        return {};
    case Phase::NeedStat:
    {
        const WebFsStatus status = WebFs_BeginStat(
            FASTFILE_PATH, CompleteRequest,
            reinterpret_cast<void *>(static_cast<std::uintptr_t>(g_runtime.generation)),
            &g_runtime.requestId);
        if (status != WebFsStatus::Pending) Fail("could not stat code_post_gfx.ff", WebFsStatusString(status));
        else g_runtime.phase = Phase::WaitingStat;
        return {};
    }
    case Phase::WaitingStat:
        if (!g_runtime.completionReady) return {};
        g_runtime.completionReady = false;
        if (g_runtime.completionStatus != WebFsStatus::Success)
        {
            Fail("could not stat code_post_gfx.ff", WebFsStatusString(g_runtime.completionStatus));
            return {};
        }
        if (g_runtime.fileSize < 14u || g_runtime.fileSize > 16u * 1024u * 1024u)
        {
            Fail("could not census code_post_gfx.ff", "file size is outside the bounded census envelope");
            return {};
        }
        g_runtime.phase = Phase::NeedRead;
        return {};
    case Phase::NeedRead:
    {
        if (g_runtime.readOffset >= g_runtime.fileSize)
        {
            Fail("could not census code_post_gfx.ff", "compressed prefix ended before the asset table");
            return {};
        }
        const std::uint32_t length = std::min<std::uint32_t>(
            WEB_FS_MAX_READ_SIZE, g_runtime.fileSize - g_runtime.readOffset);
        const WebFsStatus status = WebFs_BeginRead(
            FASTFILE_PATH, g_runtime.readOffset, length, CompleteRequest,
            reinterpret_cast<void *>(static_cast<std::uintptr_t>(g_runtime.generation)),
            &g_runtime.requestId);
        if (status != WebFsStatus::Pending) Fail("could not read code_post_gfx.ff", WebFsStatusString(status));
        else g_runtime.phase = Phase::WaitingRead;
        return {};
    }
    case Phase::WaitingRead:
        if (!g_runtime.completionReady) return {};
        g_runtime.completionReady = false;
        if (g_runtime.completionStatus != WebFsStatus::Success)
        {
            Fail("could not read code_post_gfx.ff", WebFsStatusString(g_runtime.completionStatus));
            return {};
        }
        if (g_runtime.completionBytes.empty())
        {
            Fail("could not read code_post_gfx.ff", "filesystem returned an empty bounded read");
            return {};
        }
        {
            const bool final = g_runtime.readOffset + g_runtime.completionBytes.size() == g_runtime.fileSize;
            const auto error = g_runtime.parser.FeedSource(g_runtime.completionBytes, final);
            if (error != RetailCensusError::None)
            {
                Fail("could not feed code_post_gfx.ff", RetailCensusErrorString(error));
                return {};
            }
            g_runtime.readOffset += static_cast<std::uint32_t>(g_runtime.completionBytes.size());
            g_runtime.completionBytes.clear();
            g_runtime.phase = Phase::Parse;
        }
        return {};
    case Phase::Parse:
    {
        const RetailCensusStepReport report = g_runtime.parser.Step();
        const std::uint32_t bytesUsed = std::max({
            report.sourceBytesConsumed,
            report.inflatedBytesProduced,
            report.traversedBytes});
        if (report.progress == RetailCensusProgress::Failed)
        {
            Fail("could not traverse code_post_gfx.ff prefix", RetailCensusErrorString(report.error));
        }
        else if (report.progress == RetailCensusProgress::Succeeded)
        {
            if (!g_runtime.parser.TakeResult(g_runtime.result))
            {
                Fail("could not publish code_post_gfx.ff census", "completed result was unavailable");
            }
            else
            {
                g_runtime.phase = Phase::Finished;
                PublishReady();
            }
        }
        else if (report.needsSource)
        {
            g_runtime.phase = Phase::NeedRead;
        }
        return {bytesUsed, report.recordsProcessed};
    }
    }
    return {};
}
