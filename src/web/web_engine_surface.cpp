#include <web/web_engine_surface.h>

#include <web/web_engine_world_surface.h>
#include <web/web_fastfile_world_surface.h>
#include <web/web_renderer.h>
#include <web/web_system.h>

#include <emscripten.h>
#include <zlib.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint32_t SYNTHETIC_BLOCK_0_BYTES = 732u;
constexpr std::uint32_t SYNTHETIC_BLOCK_4_BYTES = 380u;
constexpr std::uint32_t SYNTHETIC_DECLARED_ZONE_BYTES =
    SYNTHETIC_BLOCK_0_BYTES + SYNTHETIC_BLOCK_4_BYTES;
constexpr std::size_t SYNTHETIC_INFLATED_BYTES = 1246u;
constexpr std::uint32_t SYNTHETIC_MATERIAL_ALIAS = 0x40000011u;
constexpr char SYNTHETIC_MATERIAL_NAME[] = "web/synthetic";
constexpr std::array<std::uint8_t, 8> SYNTHETIC_FASTFILE_MAGIC = {
    'I', 'W', 'f', 'f', 'u', '1', '0', '0',
};

constexpr WebEngineWorldProjection2D SYNTHETIC_WORLD_PROJECTION{
    {1.0f / 64.0f, 0.0f, 0.0f, -1.0f},
    {0.0f, 1.0f / 64.0f, 0.0f, -0.75f},
};

std::uint32_t g_extractionGeneration = 0;
std::uint32_t g_conversionGeneration = 0;

constexpr kisak::fastfile::StepBudget EXTRACTION_STEP_BUDGET{
    kisak::fastfile::MAX_STEP_BYTES,
    kisak::fastfile::MAX_STEP_RECORDS,
};

enum class RuntimePhase : std::uint8_t
{
    Idle = 0,
    Running,
    Ready,
    Failed,
};

struct RuntimeExtraction
{
    RuntimePhase phase = RuntimePhase::Idle;
    kisak::fastfile::WorldSurfaceExtractionJob job;
    std::uint32_t fastfileBytes = 0u;
    std::uint32_t framePumpTick = 0u;
    std::uint32_t stepCount = 0u;
    std::uint32_t stepInputBytes = 0u;
    std::uint32_t stepOutputBytes = 0u;
    std::uint32_t stepParsedBytes = 0u;
    std::uint32_t stepRecords = 0u;
    std::uint32_t compressedBytesConsumed = 0u;
    std::uint32_t inflatedBytesProduced = 0u;
    std::uint32_t parsedBytes = 0u;
    std::uint32_t recordsProcessed = 0u;
};

RuntimeExtraction g_runtimeExtraction;

constexpr std::uint32_t PackNativeColor(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha)
{
    return static_cast<std::uint32_t>(blue) |
        (static_cast<std::uint32_t>(green) << 8u) |
        (static_cast<std::uint32_t>(red) << 16u) |
        (static_cast<std::uint32_t>(alpha) << 24u);
}

std::size_t AppendZeroedRecord(
    std::vector<std::uint8_t> &bytes,
    std::size_t size)
{
    const std::size_t offset = bytes.size();
    bytes.resize(offset + size, 0u);
    return offset;
}

void WriteU16(
    std::vector<std::uint8_t> &bytes,
    std::size_t offset,
    std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

void WriteU32(
    std::vector<std::uint8_t> &bytes,
    std::size_t offset,
    std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    bytes[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
}

void WriteF32(
    std::vector<std::uint8_t> &bytes,
    std::size_t offset,
    float value)
{
    WriteU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void AppendU16(std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    const std::size_t offset = AppendZeroedRecord(bytes, sizeof(value));
    WriteU16(bytes, offset, value);
}

void AppendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    const std::size_t offset = AppendZeroedRecord(bytes, sizeof(value));
    WriteU32(bytes, offset, value);
}

void AppendSyntheticWorldVertex(
    std::vector<std::uint8_t> &bytes,
    float x,
    float y,
    float z,
    float binormalSign,
    std::uint32_t color,
    float textureU,
    float textureV,
    float lightmapU,
    float lightmapV,
    std::uint32_t normal,
    std::uint32_t tangent)
{
    const std::size_t vertex = AppendZeroedRecord(
        bytes, kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE);
    WriteF32(bytes, vertex + 0u, x);
    WriteF32(bytes, vertex + 4u, y);
    WriteF32(bytes, vertex + 8u, z);
    WriteF32(bytes, vertex + 12u, binormalSign);
    WriteU32(bytes, vertex + 16u, color);
    WriteF32(bytes, vertex + 20u, textureU);
    WriteF32(bytes, vertex + 24u, textureV);
    WriteF32(bytes, vertex + 28u, lightmapU);
    WriteF32(bytes, vertex + 32u, lightmapV);
    WriteU32(bytes, vertex + 36u, normal);
    WriteU32(bytes, vertex + 40u, tangent);
}

// Build a complete, freely generated IWffu100 v5 input rather than handing a
// native struct graph directly to the converter. The decompressed record order
// follows the generated top-level asset and GfxWorld loaders. Logical alignment
// gaps and the block-4 inserted alias slot advance zone cursors but do not
// occupy compressed bytes.
const char *BuildSyntheticFastfile(std::vector<std::uint8_t> &destination)
{
    try
    {
        std::vector<std::uint8_t> inflated;
        inflated.reserve(SYNTHETIC_INFLATED_BYTES);

        // XFile: total allocation size, no external bytes, and nine blocks.
        const std::size_t xfile = AppendZeroedRecord(inflated, 44u);
        WriteU32(inflated, xfile + 0u, SYNTHETIC_DECLARED_ZONE_BYTES);
        WriteU32(inflated, xfile + 8u, SYNTHETIC_BLOCK_0_BYTES);
        WriteU32(inflated, xfile + 8u + 4u * 4u, SYNTHETIC_BLOCK_4_BYTES);

        // XAssetList: no script strings and a complete two-record table. The
        // generated loader reads both records before dispatching either body.
        const std::size_t assetList = AppendZeroedRecord(inflated, 16u);
        WriteU32(inflated, assetList + 8u, 2u);
        WriteU32(inflated, assetList + 12u, 0xffffffffu);
        const std::size_t assets = AppendZeroedRecord(inflated, 16u);
        WriteU32(inflated, assets + 0u, 0x04u);
        WriteU32(inflated, assets + 4u, 0xfffffffeu);
        WriteU32(inflated, assets + 8u, 0x10u);
        WriteU32(inflated, assets + 12u, 0xffffffffu);

        // The shared top-level material receives block-4 alias slot 16 before
        // its body is read in rewindable block 0. Preserve its bounded name in
        // stable job-owned metadata before the following world reuses block 0.
        const std::size_t material = AppendZeroedRecord(
            inflated, kisak::fastfile::MATERIAL_WIRE_SIZE);
        WriteU32(inflated, material + 0u, 0xffffffffu);
        inflated.insert(
            inflated.end(),
            SYNTHETIC_MATERIAL_NAME,
            SYNTHETIC_MATERIAL_NAME + sizeof(SYNTHETIC_MATERIAL_NAME));

        // The 732-byte GfxWorld body uses only fields consumed by the narrow
        // extractor. Array values are presence markers, never host pointers.
        const std::size_t world = AppendZeroedRecord(
            inflated, kisak::fastfile::GFX_WORLD_WIRE_SIZE);
        WriteU32(inflated, world + 0x10u, 12u);
        WriteU32(inflated, world + 0x14u, 0xffffffffu);
        WriteU32(inflated, world + 0x18u, 1u);
        WriteU32(inflated, world + 0x30u, 6u);
        WriteU32(inflated, world + 0x34u, 0xffffffffu);
        WriteU32(inflated, world + 0x174u, 1u);
        WriteU32(inflated, world + 0x178u, 0xffffffffu);
        WriteU32(inflated, world + 0x248u, 1u);
        WriteU32(inflated, world + 0x294u, 0xffffffffu);

        // Shared world indices include prefix and suffix sentinels. The one
        // selected surface begins at raw baseIndex 3 and keeps local indices.
        constexpr std::array<std::uint16_t, 12> indices = {
            0xffffu, 0xffffu, 0xffffu,
            0u, 1u, 2u, 2u, 3u, 0u,
            0xffffu, 0xffffu, 0xffffu,
        };
        for (std::uint16_t index : indices)
        {
            AppendU16(inflated, index);
        }

        // MaterialMemory resolves the already defined top-level material alias.
        // The surface below resolves the same stable job-local identity.
        const std::size_t materialMemory = AppendZeroedRecord(inflated, 8u);
        WriteU32(inflated, materialMemory + 0u, SYNTHETIC_MATERIAL_ALIAS);

        // Shared vertices likewise retain guard records around the selected
        // raw firstVertex 1 slice. Texture row zero is the top row.
        AppendSyntheticWorldVertex(inflated,
            -4096.0f, -4096.0f, -4096.0f, 1.0f,
            PackNativeColor(1, 2, 3, 4), -7.0f, -8.0f, -9.0f, -10.0f,
            0x01020304u, 0x05060708u);
        AppendSyntheticWorldVertex(inflated,
            32.0f, 80.0f, 24.0f, 1.0f,
            PackNativeColor(224, 96, 32, 255), 0.0f, 0.0f, 0.0f, 0.0f,
            0x7f7f7fffu, 0x7f7f7fffu);
        AppendSyntheticWorldVertex(inflated,
            32.0f, 16.0f, 24.0f, 1.0f,
            PackNativeColor(48, 176, 80, 255), 0.0f, 1.0f, 0.0f, 1.0f,
            0x7f7f7fffu, 0x7f7f7fffu);
        AppendSyntheticWorldVertex(inflated,
            96.0f, 16.0f, 24.0f, 1.0f,
            PackNativeColor(64, 112, 232, 255), 1.0f, 1.0f, 1.0f, 1.0f,
            0x7f7f7fffu, 0x7f7f7fffu);
        AppendSyntheticWorldVertex(inflated,
            96.0f, 80.0f, 24.0f, 1.0f,
            PackNativeColor(240, 208, 72, 255), 1.0f, 0.0f, 1.0f, 0.0f,
            0x7f7f7fffu, 0x7f7f7fffu);
        AppendSyntheticWorldVertex(inflated,
            4096.0f, 4096.0f, 4096.0f, -1.0f,
            PackNativeColor(5, 6, 7, 8), 7.0f, 8.0f, 9.0f, 10.0f,
            0x11121314u, 0x15161718u);

        const std::size_t surface = AppendZeroedRecord(
            inflated, kisak::fastfile::GFX_SURFACE_WIRE_SIZE);
        WriteU32(inflated, surface + 4u, 1u);
        WriteU16(inflated, surface + 8u, 4u);
        WriteU16(inflated, surface + 10u, 2u);
        WriteU32(inflated, surface + 12u, 3u);
        WriteU32(inflated, surface + 16u, SYNTHETIC_MATERIAL_ALIAS);
        WriteF32(inflated, surface + 24u, 32.0f);
        WriteF32(inflated, surface + 28u, 16.0f);
        WriteF32(inflated, surface + 32u, 24.0f);
        WriteF32(inflated, surface + 36u, 96.0f);
        WriteF32(inflated, surface + 40u, 80.0f);
        WriteF32(inflated, surface + 44u, 24.0f);

        if (inflated.size() != SYNTHETIC_INFLATED_BYTES)
        {
            return "synthetic fastfile record layout is inconsistent";
        }
        if (inflated.size() > std::numeric_limits<uLong>::max())
        {
            return "synthetic fastfile payload exceeds zlib's input range";
        }

        const uLong sourceBytes = static_cast<uLong>(inflated.size());
        uLongf compressedCapacity = compressBound(sourceBytes);
        std::vector<std::uint8_t> compressed(compressedCapacity);
        const int compressionResult = compress2(
            reinterpret_cast<Bytef *>(compressed.data()),
            &compressedCapacity,
            reinterpret_cast<const Bytef *>(inflated.data()),
            sourceBytes,
            Z_BEST_COMPRESSION);
        if (compressionResult != Z_OK)
        {
            return "synthetic fastfile zlib compression failed";
        }
        compressed.resize(static_cast<std::size_t>(compressedCapacity));

        std::vector<std::uint8_t> replacement;
        replacement.reserve(12u + compressed.size());
        replacement.insert(
            replacement.end(),
            SYNTHETIC_FASTFILE_MAGIC.begin(),
            SYNTHETIC_FASTFILE_MAGIC.end());
        AppendU32(replacement, kisak::fastfile::VERSION);
        replacement.insert(replacement.end(), compressed.begin(), compressed.end());
        destination.swap(replacement);
        return nullptr;
    }
    catch (const std::bad_alloc &)
    {
        return "synthetic fastfile allocation failed";
    }
}

EM_JS(
    void,
    DispatchEngineWorldSurfaceLifecycle,
    (const char *state,
     const char *pipelineStage,
     const char *message,
     std::uint32_t extractionGeneration,
     std::uint32_t conversionGeneration,
     std::uint32_t framePumpTick,
     std::uint32_t stepCount,
     std::uint32_t stepInputBytes,
     std::uint32_t stepOutputBytes,
     std::uint32_t stepParsedBytes,
     std::uint32_t stepRecords,
     std::uint32_t compressedBytesConsumed,
     std::uint32_t inflatedBytesProduced,
     std::uint32_t parsedBytes,
     std::uint32_t recordsProcessed,
     std::uint32_t maxStepBytes,
     std::uint32_t maxStepRecords,
     std::uint32_t fastfileVersion,
     std::uint32_t fastfileBytes,
     std::uint32_t compressedBytes,
     std::uint32_t inflatedBytes,
     std::uint32_t declaredZoneBytes,
     std::uint32_t zoneBlock0Bytes,
     std::uint32_t zoneBlock4Bytes,
     std::uint32_t sourceAssetCount,
     std::uint32_t materialAssetIndex,
     std::uint32_t worldAssetIndex,
     std::uint32_t materialIdentity,
     std::uint32_t sourceSurfaceIndex,
     std::uint32_t worldVertexCount,
     std::uint32_t worldIndexCount,
     std::uint32_t worldSurfaceCount,
     std::uint32_t firstVertex,
     std::uint32_t vertexCount,
     std::uint32_t baseIndex,
     std::uint32_t triangleCount,
     const char *materialReferenceKind,
     const char *materialName,
     std::uint32_t convertedVertexCount,
     std::uint32_t convertedIndexCount),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:engine-world-surface", {
            detail: {
                state: UTF8ToString(state),
                pipelineStage: UTF8ToString(pipelineStage),
                message: UTF8ToString(message),
                sourceRepresentation: "fastfile-gfxworld",
                sourceContainer: "IWffu100",
                vertexFormat: "base-world",
                projection: "affine-world-to-clip-2d",
                synthetic: true,
                extractionGeneration: extractionGeneration >>> 0,
                conversionGeneration: conversionGeneration >>> 0,
                framePumpTick: framePumpTick >>> 0,
                stepCount: stepCount >>> 0,
                stepInputBytes: stepInputBytes >>> 0,
                stepOutputBytes: stepOutputBytes >>> 0,
                stepParsedBytes: stepParsedBytes >>> 0,
                stepRecords: stepRecords >>> 0,
                compressedBytesConsumed: compressedBytesConsumed >>> 0,
                inflatedBytesProduced: inflatedBytesProduced >>> 0,
                parsedBytes: parsedBytes >>> 0,
                recordsProcessed: recordsProcessed >>> 0,
                maxStepBytes: maxStepBytes >>> 0,
                maxStepRecords: maxStepRecords >>> 0,
                fastfileVersion: fastfileVersion >>> 0,
                fastfileBytes: fastfileBytes >>> 0,
                compressedBytes: compressedBytes >>> 0,
                inflatedBytes: inflatedBytes >>> 0,
                declaredZoneBytes: declaredZoneBytes >>> 0,
                zoneBlock0Bytes: zoneBlock0Bytes >>> 0,
                zoneBlock4Bytes: zoneBlock4Bytes >>> 0,
                sourceAssetCount: sourceAssetCount >>> 0,
                materialAssetIndex: materialAssetIndex >>> 0,
                worldAssetIndex: worldAssetIndex >>> 0,
                materialIdentity: materialIdentity >>> 0,
                sourceSurfaceIndex: sourceSurfaceIndex >>> 0,
                worldVertexCount: worldVertexCount >>> 0,
                worldIndexCount: worldIndexCount >>> 0,
                worldSurfaceCount: worldSurfaceCount >>> 0,
                firstVertex: firstVertex >>> 0,
                vertexCount: vertexCount >>> 0,
                baseIndex: baseIndex >>> 0,
                triangleCount: triangleCount >>> 0,
                materialReferenceKind: UTF8ToString(materialReferenceKind),
                materialName: UTF8ToString(materialName),
                convertedVertexCount: convertedVertexCount >>> 0,
                convertedIndexCount: convertedIndexCount >>> 0
            }
        }));
    });

void EmitWorldSurfaceLifecycle(
    const char *state,
    const char *pipelineStage,
    const char *message,
    const kisak::fastfile::ExtractedWorldSurface *extracted,
    std::uint32_t fastfileBytes,
    std::uint32_t convertedVertexCount,
    std::uint32_t convertedIndexCount)
{
    const bool hasSource = extracted != nullptr;
    DispatchEngineWorldSurfaceLifecycle(
        state,
        pipelineStage,
        message,
        g_extractionGeneration,
        g_conversionGeneration,
        g_runtimeExtraction.framePumpTick,
        g_runtimeExtraction.stepCount,
        g_runtimeExtraction.stepInputBytes,
        g_runtimeExtraction.stepOutputBytes,
        g_runtimeExtraction.stepParsedBytes,
        g_runtimeExtraction.stepRecords,
        g_runtimeExtraction.compressedBytesConsumed,
        g_runtimeExtraction.inflatedBytesProduced,
        g_runtimeExtraction.parsedBytes,
        g_runtimeExtraction.recordsProcessed,
        EXTRACTION_STEP_BUDGET.maxBytes,
        EXTRACTION_STEP_BUDGET.maxRecords,
        hasSource ? extracted->fastfileVersion : 0u,
        fastfileBytes,
        hasSource ? extracted->compressedBytes : 0u,
        hasSource ? extracted->inflatedBytes : 0u,
        hasSource ? extracted->declaredZoneBytes : 0u,
        hasSource ? extracted->blockSizes[0] : 0u,
        hasSource ? extracted->blockSizes[4] : 0u,
        hasSource ? extracted->sourceAssetCount : 0u,
        hasSource ? extracted->materialAssetIndex : 0u,
        hasSource ? extracted->worldAssetIndex : 0u,
        hasSource ? extracted->materialIdentity : 0u,
        hasSource ? extracted->sourceSurfaceIndex : 0u,
        hasSource ? extracted->sourceWorldVertexCount : 0u,
        hasSource ? extracted->sourceWorldIndexCount : 0u,
        hasSource ? extracted->sourceWorldSurfaceCount : 0u,
        hasSource ? extracted->metadata.sourceFirstVertex : 0u,
        hasSource ? extracted->metadata.vertexCount : 0u,
        hasSource ? extracted->metadata.sourceBaseIndex : 0u,
        hasSource ? extracted->metadata.triangleCount : 0u,
        hasSource
            ? kisak::fastfile::MaterialReferenceKindString(
                extracted->metadata.materialReference)
            : "",
        hasSource ? extracted->materialName.c_str() : "",
        convertedVertexCount,
        convertedIndexCount);
}

std::uint32_t NextGeneration(std::uint32_t generation) noexcept
{
    return generation == UINT32_MAX ? 1u : generation + 1u;
}

bool AddCounter(std::uint32_t &total, std::uint32_t increment) noexcept
{
    if (increment > UINT32_MAX - total)
    {
        return false;
    }
    total += increment;
    return true;
}

bool RecordStep(
    const WebFrameInfo &frame,
    const kisak::fastfile::StepReport &report) noexcept
{
    g_runtimeExtraction.framePumpTick = frame.pumpTick;
    g_runtimeExtraction.stepInputBytes = report.compressedBytesConsumedThisStep;
    g_runtimeExtraction.stepOutputBytes = report.inflatedBytesProducedThisStep;
    g_runtimeExtraction.stepParsedBytes = report.traversedBytesThisStep;
    g_runtimeExtraction.stepRecords = report.recordsProcessedThisStep;
    return AddCounter(g_runtimeExtraction.stepCount, 1u) &&
        AddCounter(
            g_runtimeExtraction.compressedBytesConsumed,
            report.compressedBytesConsumedThisStep) &&
        AddCounter(
            g_runtimeExtraction.inflatedBytesProduced,
            report.inflatedBytesProducedThisStep) &&
        AddCounter(g_runtimeExtraction.parsedBytes, report.traversedBytesThisStep) &&
        AddCounter(g_runtimeExtraction.recordsProcessed, report.recordsProcessedThisStep);
}

const char *RuntimeStageString(kisak::fastfile::JobStage stage) noexcept
{
    switch (stage)
    {
    case kisak::fastfile::JobStage::Inflate: return "inflate";
    case kisak::fastfile::JobStage::Traverse: return "traverse";
    case kisak::fastfile::JobStage::NotStarted: return "begin";
    case kisak::fastfile::JobStage::Complete: return "complete";
    case kisak::fastfile::JobStage::Failed: return "extraction";
    }
    return "extraction";
}

WebEngineSurfaceFrameResult FailRuntimeExtraction(
    const char *stage,
    const char *message,
    const kisak::fastfile::ExtractedWorldSurface *extracted = nullptr,
    std::uint32_t convertedVertexCount = 0u,
    std::uint32_t convertedIndexCount = 0u)
{
    g_runtimeExtraction.phase = RuntimePhase::Failed;
    g_runtimeExtraction.job.Reset();
    EmitWorldSurfaceLifecycle(
        "failed",
        stage,
        message,
        extracted,
        g_runtimeExtraction.fastfileBytes,
        convertedVertexCount,
        convertedIndexCount);
    return WebEngineSurfaceFrameResult::Failed;
}

WebEngineSurfaceFrameResult CompleteRuntimeExtraction()
{
    kisak::fastfile::ExtractedWorldSurface extracted;
    if (!g_runtimeExtraction.job.TakeResult(extracted))
    {
        return FailRuntimeExtraction(
            "result", "The completed fastfile extraction did not expose one owned result");
    }

    // TakeResult transfers only the selected surface. Release the source and
    // inflated traversal staging before conversion or renderer submission.
    g_runtimeExtraction.job.Reset();
    const std::uint64_t expectedTraversalBytes =
        static_cast<std::uint64_t>(extracted.inflatedBytes) +
        static_cast<std::uint64_t>(extracted.metadata.vertexCount) *
            kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE +
        static_cast<std::uint64_t>(extracted.metadata.triangleCount) * 3u *
            sizeof(std::uint16_t);
    if (g_runtimeExtraction.compressedBytesConsumed != extracted.compressedBytes ||
        g_runtimeExtraction.inflatedBytesProduced != extracted.inflatedBytes ||
        expectedTraversalBytes > UINT32_MAX ||
        g_runtimeExtraction.parsedBytes != expectedTraversalBytes)
    {
        return FailRuntimeExtraction(
            "metrics",
            "Incremental fastfile work counters did not match the extracted source",
            &extracted);
    }

    WebEngineConvertedWorldSurface converted;
    const WebEngineWorldSurfaceResult conversionResult =
        WebEngine_ConvertWorldSurface(
            extracted.View(), SYNTHETIC_WORLD_PROJECTION, converted);
    if (conversionResult != WebEngineWorldSurfaceResult::Success)
    {
        const char *message = WebEngine_WorldSurfaceResultString(conversionResult);
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Extracted world-surface conversion failed: %s.\n",
            message);
        return FailRuntimeExtraction("conversion", message, &extracted);
    }

    const WebRendererSurfaceDesc surface{
        converted.vertices.data(),
        static_cast<std::uint32_t>(converted.vertices.size()),
        converted.indices.data(),
        static_cast<std::uint32_t>(converted.indices.size()),
    };
    const WebRendererSurfaceResult submissionResult =
        WebRenderer_SetSurface(surface, converted.draw);
    if (submissionResult != WebRendererSurfaceResult::Success)
    {
        const char *message = WebRenderer_SurfaceResultString(submissionResult);
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Extracted engine surface submission failed: %s.\n",
            message);
        return FailRuntimeExtraction(
            "submission",
            message,
            &extracted,
            static_cast<std::uint32_t>(converted.vertices.size()),
            static_cast<std::uint32_t>(converted.indices.size()));
    }

    g_conversionGeneration = NextGeneration(g_conversionGeneration);
    g_runtimeExtraction.phase = RuntimePhase::Ready;
    EmitWorldSurfaceLifecycle(
        "ready",
        "complete",
        "Incrementally extracted and converted one bounded synthetic IWffu100 v5 GfxWorld surface",
        &extracted,
        g_runtimeExtraction.fastfileBytes,
        static_cast<std::uint32_t>(converted.vertices.size()),
        static_cast<std::uint32_t>(converted.indices.size()));
    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] Incrementally extracted and converted synthetic IWffu100 v%u "
        "material '%s' (%u vertices, %u triangles) through the renderer seam.\n",
        extracted.fastfileVersion,
        extracted.materialName.c_str(),
        static_cast<std::uint32_t>(extracted.metadata.vertexCount),
        static_cast<std::uint32_t>(extracted.metadata.triangleCount));
    return WebEngineSurfaceFrameResult::Ready;
}
} // namespace

bool WebEngineSurface_Start()
{
    g_runtimeExtraction.job.Reset();
    g_runtimeExtraction = {};
    g_extractionGeneration = NextGeneration(g_extractionGeneration);

    std::vector<std::uint8_t> syntheticFastfile;
    if (const char *buildError = BuildSyntheticFastfile(syntheticFastfile))
    {
        g_runtimeExtraction.phase = RuntimePhase::Failed;
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Synthetic fastfile construction failed: %s.\n",
            buildError);
        EmitWorldSurfaceLifecycle(
            "failed", "fixture-build", buildError, nullptr, 0u, 0u, 0u);
        return false;
    }

    if (syntheticFastfile.size() > UINT32_MAX)
    {
        g_runtimeExtraction.phase = RuntimePhase::Failed;
        EmitWorldSurfaceLifecycle(
            "failed",
            "fixture-build",
            "Synthetic fastfile size could not be represented",
            nullptr,
            0u,
            0u,
            0u);
        return false;
    }

    g_runtimeExtraction.fastfileBytes =
        static_cast<std::uint32_t>(syntheticFastfile.size());
    const kisak::fastfile::Error beginResult =
        g_runtimeExtraction.job.Begin(std::move(syntheticFastfile), {});
    if (beginResult != kisak::fastfile::Error::None)
    {
        const char *message = kisak::fastfile::ErrorString(beginResult);
        g_runtimeExtraction.phase = RuntimePhase::Failed;
        g_runtimeExtraction.job.Reset();
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Incremental fastfile extraction could not begin: %s.\n",
            message);
        EmitWorldSurfaceLifecycle(
            "failed",
            "begin",
            message,
            nullptr,
            g_runtimeExtraction.fastfileBytes,
            0u,
            0u);
        return false;
    }

    g_runtimeExtraction.phase = RuntimePhase::Running;
    EmitWorldSurfaceLifecycle(
        "loading",
        "begin",
        "The synthetic fastfile is queued for bounded incremental extraction",
        nullptr,
        g_runtimeExtraction.fastfileBytes,
        0u,
        0u);
    return true;
}

WebEngineSurfaceFrameResult WebEngineSurface_Frame(const WebFrameInfo &frame)
{
    switch (g_runtimeExtraction.phase)
    {
    case RuntimePhase::Ready: return WebEngineSurfaceFrameResult::Ready;
    case RuntimePhase::Failed: return WebEngineSurfaceFrameResult::Failed;
    case RuntimePhase::Idle: return WebEngineSurfaceFrameResult::Failed;
    case RuntimePhase::Running: break;
    }

    const kisak::fastfile::JobStage stageBeforeStep =
        g_runtimeExtraction.job.Stage();
    const kisak::fastfile::StepReport report =
        g_runtimeExtraction.job.Step(EXTRACTION_STEP_BUDGET);
    if (!RecordStep(frame, report))
    {
        return FailRuntimeExtraction(
            "metrics", "Incremental fastfile work counters overflowed");
    }

    if (report.progress == kisak::fastfile::JobProgress::Failed)
    {
        const kisak::fastfile::Error error = report.error != kisak::fastfile::Error::None
            ? report.error
            : g_runtimeExtraction.job.Failure();
        const char *message = kisak::fastfile::ErrorString(error);
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Incremental fastfile extraction failed: %s.\n",
            message);
        return FailRuntimeExtraction(RuntimeStageString(stageBeforeStep), message);
    }

    EmitWorldSurfaceLifecycle(
        "loading",
        RuntimeStageString(stageBeforeStep),
        report.progress == kisak::fastfile::JobProgress::Succeeded
            ? "Incremental fastfile traversal completed within this frame budget"
            : "Incremental fastfile extraction yielded to the browser frame pump",
        nullptr,
        g_runtimeExtraction.fastfileBytes,
        0u,
        0u);

    if (report.progress == kisak::fastfile::JobProgress::Succeeded)
    {
        return CompleteRuntimeExtraction();
    }
    if (report.progress != kisak::fastfile::JobProgress::Running)
    {
        return FailRuntimeExtraction(
            "extraction", "Incremental fastfile extraction returned an invalid state");
    }
    return WebEngineSurfaceFrameResult::Pending;
}
