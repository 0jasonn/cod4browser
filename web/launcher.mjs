import { createBrowserAssetStore, selectInstallEntries } from "./asset_store.mjs";
import { installBrowserFilesystemBridge } from "./filesystem_bridge.mjs";

const canvas = document.querySelector("#game-canvas");
const runtimeLabel = document.querySelector("#runtime-label");
const runtimeMessage = document.querySelector("#runtime-message");
const bootLog = document.querySelector("#boot-log");
const frameCounter = document.querySelector("#frame-counter");
const canvasSize = document.querySelector("#canvas-size");
const physicsStatus = document.querySelector("#physics-status");
const rendererStatus = document.querySelector("#renderer-status");
const systemStatus = document.querySelector("#system-status");
const selectInstallButton = document.querySelector("#select-install-button");
const clearAssetsButton = document.querySelector("#clear-assets-button");
const installFolderInput = document.querySelector("#install-folder-input");
const assetStateLabel = document.querySelector("#asset-state-label");
const assetMessage = document.querySelector("#asset-message");
const assetProgress = document.querySelector("#asset-progress");
const assetManifest = document.querySelector("#asset-manifest");
const assetControl = document.querySelector(".asset-control");
const assetRetention = document.querySelector("#asset-retention");
const archiveStatus = document.querySelector("#archive-status");
const engineAssetStatus = document.querySelector("#engine-asset-status");

const runtime = {
    state: "loading",
    module: null,
    logs: [],
    lastFrame: null,
    contextLosses: 0,
    engine: null,
    qcommon: {
        state: "idle",
        stage: "idle",
        message: "Waiting for a validated local installation",
    },
    retailCensus: {
        state: "idle",
        stage: "idle",
        message: "Waiting for qcommon pre-database startup",
    },
    scheduler: {
        state: "idle",
        message: "Waiting for the cooperative frame scheduler",
    },
    schedulerSamples: [],
    system: null,
    systemSamples: [],
    assets: {
        state: "checking",
        message: "Waiting for the WebAssembly asset probe",
    },
    archive: { state: "idle", message: "Waiting for a validated local archive" },
    engineAsset: { state: "idle", message: "Waiting for a mounted engine archive" },
    engineWorldSurface: {
        state: "idle",
        message: "Waiting for a fastfile world-surface extraction",
    },
    rendererSurface: { state: "idle", message: "Waiting for an engine surface" },
    rendererTexture: { state: "idle", message: "Waiting for a supported engine image" },
    rendererTextures: [],
    rendererShader: { state: "idle", message: "Waiting for a retail shader contract" },
    rendererMaterial: {
        state: "idle",
        message: "Waiting for a shader sampler and decoded engine image",
    },
};
globalThis.__KISAKCOD_WEB__ = runtime;

let assetStore = null;
let filesystemBridge = null;
let archiveImportId = null;
let qcommonImportId = null;
let retailCensusImportId = null;
let schedulerWarningsReported = 0;
let schedulerViolationsReported = 0;

const stateLabels = {
    loading: "Runtime loading",
    "runtime-ready": "Engine ready",
    running: "Browser frame pump active",
    "renderer-lost": "Renderer interrupted",
    failed: "Engine boot failed",
};

function setState(state, message) {
    runtime.state = state;
    document.documentElement.dataset.runtimeState = state;
    runtimeLabel.textContent = stateLabels[state] ?? state;
    runtimeMessage.textContent = message;
}

function appendLog(message, level = "info") {
    const text = String(message);
    runtime.logs.push({ level, text });
    if (runtime.logs.length > 80) {
        runtime.logs.splice(0, runtime.logs.length - 80);
    }

    bootLog.textContent = runtime.logs
        .map((entry) => `${entry.level === "error" ? "!" : ">"} ${entry.text}`)
        .join("\n");
    bootLog.scrollTop = bootLog.scrollHeight;

    if (text.includes("ODE physics math verified")) {
        physicsStatus.textContent = "ODE verified; commands pending";
    }
    if (text.startsWith("[kisakcod-web] Renderer:")) {
        rendererStatus.textContent = runtime.engineWorldSurface.state === "ready"
            ? "WebGL2 + world surface ready"
            : "WebGL2 initialized";
    }
}

function formatBytes(bytes) {
    if (!Number.isFinite(bytes)) {
        return "unknown size";
    }
    const units = ["B", "KiB", "MiB", "GiB"];
    let value = bytes;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
        value /= 1024;
        unit += 1;
    }
    const digits = unit === 0 || value >= 100 ? 0 : value >= 10 ? 1 : 2;
    return `${value.toFixed(digits)} ${units[unit]}`;
}

function updateRendererMaterialBinding() {
    const shaderReady = runtime.rendererShader?.state === "ready" &&
        runtime.rendererShader?.resident === true &&
        runtime.rendererShader?.firstDrawCompleted === true;
    const renderSurface =
        runtime.retailCensus?.worldInventory?.firstXModel?.renderSurface;
    const xmodelColorMap = renderSurface?.colorMap;
    const drawTextures = renderSurface?.drawList?.textures ??
        (xmodelColorMap ? [{ ...xmodelColorMap, textureSlot: 0 }] : []);
    const residentTextures = drawTextures.map((binding) =>
        runtime.rendererTextures[binding.textureSlot >>> 0]);
    const textureReady = drawTextures.length > 0 && residentTextures.every(
        (texture, index) => texture?.state === "ready" && texture?.resident === true &&
            texture.path?.toLowerCase() === drawTextures[index].imagePath.toLowerCase(),
    );
    const materialReady = runtime.retailCensus?.state === "ready" &&
        xmodelColorMap?.state === "selected" &&
        xmodelColorMap.materialIdentity !== 0 && xmodelColorMap.imageIdentity !== 0 &&
        xmodelColorMap.semantic === 2;
    const primaryTexture = residentTextures[0] ?? runtime.rendererTexture;
    const textureMatchesMaterial = materialReady && textureReady;
    const previousReady = runtime.rendererMaterial?.state === "ready";
    if (!shaderReady || !textureReady || !materialReady || !textureMatchesMaterial) {
        runtime.rendererMaterial = {
            state: "waiting",
        message: "Waiting for the rendered XModel material and its resident color map",
            shaderReady,
            textureReady,
            materialReady,
            textureMatchesMaterial,
        };
        return;
    }

    const sourceFormat = primaryTexture.sourceFormat >>> 0;
    const compressedSource = sourceFormat >= 11 && sourceFormat <= 13;
    runtime.rendererMaterial = {
        state: "ready",
        message: compressedSource
            ? "Retail material selected a COD4 DXT image for the shader sampler"
            : "Retail material selected an engine image for the shader sampler",
        materialSource: "retail-xmodel",
        materialName: xmodelColorMap.materialName,
        materialIdentity: xmodelColorMap.materialIdentity,
        imageName: xmodelColorMap.imageName,
        imageIdentity: xmodelColorMap.imageIdentity,
        imageSemantic: xmodelColorMap.semantic,
        shaderSubstitutionId: runtime.rendererShader.substitutionId,
        sampler: "u_colorMapSampler",
        textureUnit: 0,
        imagePath: primaryTexture.path,
        sourceFormat,
        decodedFormat: primaryTexture.gpuFormat,
        compressedSource,
        recoveryBytes: residentTextures.reduce(
            (total, texture) => total + Number(texture?.recoveryBytes ?? 0), 0),
        drawCount: renderSurface?.drawList?.drawCount ?? 1,
        textureCount: drawTextures.length,
        geometrySource:
            runtime.retailCensus?.worldInventory?.firstXModel?.renderSurface?.state === "ready"
                ? "retail-xmodel"
                : runtime.engineWorldSurface?.state !== "ready"
                    ? "unknown"
                    : runtime.engineWorldSurface.synthetic ? "synthetic" : "retail",
    };
    if (compressedSource) {
        rendererStatus.textContent = "WebGL2 + COD4 DXT texture binding ready";
    }
    if (!previousReady) {
        appendLog(
            `[kisakcod-web] XModel material ${xmodelColorMap.materialName} selected ` +
            `${drawTextures.length} resident color map(s) for ` +
            `${runtime.rendererShader.substitutionId}.`,
        );
    }
}

function publishAssetState(detail) {
    runtime.assets = { ...detail };
    globalThis.dispatchEvent(new CustomEvent("kisakcod:assets", {
        detail: runtime.assets,
    }));
}

function publishArchiveState(detail) {
    runtime.archive = { ...detail };
}

globalThis.addEventListener("kisakcod:archive", (event) => {
    publishArchiveState(event.detail);
    archiveStatus.textContent = event.detail.state === "ready"
        ? `${event.detail.recordCount.toLocaleString()} entries; members verified`
        : event.detail.state === "failed"
            ? "Archive verification failed"
            : event.detail.state === "idle"
                ? "Waiting for local assets"
                : "Reading archive asynchronously";
    if (event.detail.state === "ready") {
        appendLog(
            `[kisakcod-web] Enumerated ${event.detail.recordCount} IWD records and ` +
            `verified ${event.detail.verifiedMembers.length} member(s).`,
        );
    } else if (event.detail.state === "failed") {
        appendLog(`[kisakcod-web] Archive reader: ${event.detail.message}`, "error");
    }
});

globalThis.addEventListener("kisakcod:qcommon", (event) => {
    runtime.qcommon = { ...event.detail };
    if (event.detail.state === "ready") {
        physicsStatus.textContent = "ODE + qcommon pre-database ready";
        appendLog(
            `[kisakcod-web] Qcommon checked ${event.detail.filesChecked}/` +
            `${event.detail.totalFiles} startup files through the cooperative VFS.`,
        );
        const readyImportId = runtime.assets.state === "ready"
            ? runtime.assets.manifest?.importId ?? null
            : null;
        if (readyImportId && readyImportId === qcommonImportId &&
            readyImportId !== retailCensusImportId &&
            typeof runtime.module?._KisakWeb_StartRetailCensus === "function") {
            retailCensusImportId = readyImportId;
            runtime.module._KisakWeb_StartRetailCensus();
        }
    } else if (event.detail.state === "failed") {
        physicsStatus.textContent = "Qcommon startup failed";
        appendLog(`[kisakcod-web] Qcommon startup: ${event.detail.message}`, "error");
    } else if (event.detail.state === "loading") {
        physicsStatus.textContent = `Qcommon startup: ${event.detail.stage}`;
    }
});

globalThis.addEventListener("kisakcod:retail-census", (event) => {
    runtime.retailCensus = structuredClone(event.detail);
    if (event.detail.state === "ready") {
        const firstXModel = event.detail.worldInventory.firstXModel;
        const postXModelTechniqueSet =
            event.detail.worldInventory.postXModelTechniqueSet;
        const publishedTechniqueSets = event.detail.worldInventory.techniqueSets
            .filter((entry) => entry.published).length;
        appendLog(
            `[kisakcod-web] Counted ${event.detail.assetCount.toLocaleString()} ` +
            `code_post_gfx assets; material ${event.detail.materialName} selected ` +
            `${event.detail.materialImagePath} with ${event.detail.shaderSubstitutionId}. ` +
            `Killhouse contains ${event.detail.worldInventory.assetCount.toLocaleString()} ` +
            `assets; its first GfxWorld is table index ` +
            `${event.detail.worldInventory.firstGfxWorldAssetIndex}. ` +
            `Published ${publishedTechniqueSets} bounded ` +
            `map technique sets, then traversed XModel ${firstXModel?.name ?? "unknown"} ` +
            `(${firstXModel?.totals?.vertices ?? 0} vertices, ` +
            `${firstXModel?.totals?.triangles ?? 0} triangles, ` +
            `${firstXModel?.surfaceCount ?? 0} surfaces, ` +
            `${firstXModel?.materials?.length ?? 0} inline materials, and ` +
            `${firstXModel?.totals?.collisionTriangles ?? 0} collision triangles) ` +
            `and published its checked dependency boundary. ` +
            (postXModelTechniqueSet?.published
                ? `M30 also published post-model technique set ` +
                    `${postXModelTechniqueSet.name} at asset ` +
                    `${postXModelTechniqueSet.assetIndex}. `
                : `The post-model technique set remains unpublished. `) +
            (firstXModel?.renderSurface?.state === "ready"
                ? `WebGL2 is now drawing retail surface ` +
                    `${firstXModel.renderSurface.surfaceIndex} ` +
                    `(${firstXModel.renderSurface.vertexCount} vertices); ` +
                    (firstXModel.renderSurface.colorMap?.state === "selected"
                        ? `its typed color map is ` +
                            `${firstXModel.renderSurface.colorMap.imagePath}.`
                        : `its current texture is retained: ` +
                            `${firstXModel.renderSurface.colorMap?.message ?? "no color map"}.`)
                : `The bootstrap surface remains active: ` +
                    `${firstXModel?.renderSurface?.message ?? "no render candidate"}.`),
        );
        const readyImportId = runtime.assets.state === "ready"
            ? runtime.assets.manifest?.importId ?? null
            : null;
        if (readyImportId && readyImportId === retailCensusImportId &&
            readyImportId !== archiveImportId &&
            typeof runtime.module?._KisakWeb_StartArchiveJob === "function") {
            archiveImportId = readyImportId;
            runtime.module._KisakWeb_StartArchiveJob();
        }
    } else if (event.detail.state === "failed") {
        appendLog(`[kisakcod-web] Retail fastfile census: ${event.detail.message}`, "error");
    }
    updateRendererMaterialBinding();
});

globalThis.addEventListener("kisakcod:renderer-shader", (event) => {
    const previousFirstDraw = runtime.rendererShader?.firstDrawCompleted === true;
    runtime.rendererShader = structuredClone(event.detail);
    if (event.detail.state === "ready" && event.detail.firstDrawCompleted) {
        rendererStatus.textContent = "WebGL2 + COD4 shader contract ready";
        if (!previousFirstDraw) {
            appendLog(
                `[kisakcod-web] Drew the indexed surface through ` +
                `${event.detail.substitutionId}.`,
            );
        }
    } else if (event.detail.state === "failed") {
        rendererStatus.textContent = "WebGL2 shader substitution failed";
        appendLog(`[kisakcod-web] Renderer shader: ${event.detail.message}`, "error");
    } else if (event.detail.state === "lost") {
        rendererStatus.textContent = "WebGL2 shader context lost";
    } else if (event.detail.state === "cleared") {
        rendererStatus.textContent = "WebGL2 + world surface ready";
    }
    updateRendererMaterialBinding();
});

globalThis.addEventListener("kisakcod:schedule", (event) => {
    runtime.scheduler = structuredClone(event.detail);
    runtime.schedulerSamples.push(runtime.scheduler);
    if (runtime.schedulerSamples.length > 80) {
        runtime.schedulerSamples.splice(0, runtime.schedulerSamples.length - 80);
    }
    if (event.detail.protocolViolations > schedulerViolationsReported) {
        appendLog(
            `[kisakcod-web] Scheduler quarantined ${event.detail.protocolViolations} ` +
            `task protocol violation(s).`,
            "error",
        );
        schedulerViolationsReported = event.detail.protocolViolations;
    }
    if (event.detail.starvationWarnings > schedulerWarningsReported) {
        appendLog(
            `[kisakcod-web] Scheduler observed ${event.detail.starvationWarnings} ` +
            `repeated starvation condition(s).`,
            "error",
        );
        schedulerWarningsReported = event.detail.starvationWarnings;
    }
});

globalThis.addEventListener("kisakcod:engine-asset", (event) => {
    runtime.engineAsset = { ...event.detail };
    switch (event.detail.state) {
    case "ready":
        engineAssetStatus.textContent =
            `${event.detail.width}×${event.detail.height} IWI loaded`;
        appendLog(
            `[kisakcod-web] Engine asset ${event.detail.path}: ` +
            `${event.detail.width}×${event.detail.height}×${event.detail.depth}, ` +
            `${formatBytes(event.detail.size)}; request cache released.`,
        );
        break;
    case "failed":
        engineAssetStatus.textContent = "Engine asset rejected";
        appendLog(`[kisakcod-web] Engine asset: ${event.detail.message}`, "error");
        break;
    case "unavailable":
        engineAssetStatus.textContent = "No bounded IWI member found";
        break;
    case "loading":
        engineAssetStatus.textContent = "Reading one IWI asynchronously";
        break;
    default:
        engineAssetStatus.textContent = "Waiting for mounted archive";
        break;
    }
});

globalThis.addEventListener("kisakcod:renderer-texture", (event) => {
    const previousState = runtime.rendererTexture.state;
    const previousRecoveryCount = runtime.rendererTexture.recoveryCount ?? 0;
    runtime.rendererTexture = {
        ...runtime.rendererTexture,
        ...event.detail,
    };
    const textureSlot = event.detail.textureSlot >>> 0;
    runtime.rendererTextures[textureSlot] = {
        ...(runtime.rendererTextures[textureSlot] ?? {}),
        ...event.detail,
        textureSlot,
    };
    switch (event.detail.state) {
    case "ready":
        if (event.detail.resident === false) {
            engineAssetStatus.textContent = "IWI texture retained; awaiting WebGL2";
            break;
        }
        engineAssetStatus.textContent =
            `${runtime.rendererTexture.width}\u00d7${runtime.rendererTexture.height} IWI texture active`;
        if (previousState === "lost" || previousState === "retained" ||
            runtime.rendererTexture.recoveryCount > previousRecoveryCount) {
            appendLog(
                `[kisakcod-web] Recreated the renderer texture from ` +
                `${formatBytes(runtime.rendererTexture.recoveryBytes)} of bounded recovery pixels.`,
            );
        } else {
            appendLog(
                `[kisakcod-web] Uploaded ${runtime.rendererTexture.path || "engine IWI"} ` +
                `as RGBA8; renderer owns ${formatBytes(runtime.rendererTexture.recoveryBytes)}.`,
            );
        }
        break;
    case "retained":
        engineAssetStatus.textContent = "IWI texture retained; awaiting WebGL2";
        appendLog(
            `[kisakcod-web] Retained ${formatBytes(runtime.rendererTexture.recoveryBytes)} ` +
            `of bounded texture pixels for context recovery.`,
        );
        break;
    case "unsupported":
        engineAssetStatus.textContent = "IWI parsed; texture format deferred";
        appendLog(`[kisakcod-web] Renderer texture: ${event.detail.message}`);
        break;
    case "failed":
        engineAssetStatus.textContent = "IWI texture rejected";
        appendLog(`[kisakcod-web] Renderer texture: ${event.detail.message}`, "error");
        break;
    case "loading":
        engineAssetStatus.textContent = "Reading one IWI texture asynchronously";
        break;
    case "lost":
        engineAssetStatus.textContent = "IWI texture retained for context recovery";
        break;
    case "unavailable":
        engineAssetStatus.textContent = "No bounded IWI texture found";
        break;
    default:
        engineAssetStatus.textContent = "Waiting for mounted archive";
        break;
    }
    updateRendererMaterialBinding();
});

globalThis.addEventListener("kisakcod:engine-world-surface", (event) => {
    runtime.engineWorldSurface = {
        state: event.detail.state,
        pipelineStage: event.detail.pipelineStage,
        message: event.detail.message,
        sourceRepresentation: event.detail.sourceRepresentation,
        sourceContainer: event.detail.sourceContainer,
        vertexFormat: event.detail.vertexFormat,
        projection: event.detail.projection,
        synthetic: event.detail.synthetic,
        extractionGeneration: event.detail.extractionGeneration,
        conversionGeneration: event.detail.conversionGeneration,
        framePumpTick: event.detail.framePumpTick,
        stepCount: event.detail.stepCount,
        stepInputBytes: event.detail.stepInputBytes,
        stepOutputBytes: event.detail.stepOutputBytes,
        stepParsedBytes: event.detail.stepParsedBytes,
        stepRecords: event.detail.stepRecords,
        stepSourceBytes: event.detail.stepSourceBytes,
        sourceFeedCount: event.detail.sourceFeedCount,
        sourceBytesReceived: event.detail.sourceBytesReceived,
        sourceBytesConsumed: event.detail.sourceBytesConsumed,
        maxSourceChunkBytes: event.detail.maxSourceChunkBytes,
        needsSource: event.detail.needsSource,
        compressedBytesConsumed: event.detail.compressedBytesConsumed,
        inflatedBytesProduced: event.detail.inflatedBytesProduced,
        parsedBytes: event.detail.parsedBytes,
        recordsProcessed: event.detail.recordsProcessed,
        maxStepBytes: event.detail.maxStepBytes,
        maxStepRecords: event.detail.maxStepRecords,
        fastfileVersion: event.detail.fastfileVersion,
        fastfileBytes: event.detail.fastfileBytes,
        compressedBytes: event.detail.compressedBytes,
        inflatedBytes: event.detail.inflatedBytes,
        declaredZoneBytes: event.detail.declaredZoneBytes,
        zoneBlock0Bytes: event.detail.zoneBlock0Bytes,
        zoneBlock4Bytes: event.detail.zoneBlock4Bytes,
        sourceAssetCount: event.detail.sourceAssetCount,
        materialAssetIndex: event.detail.materialAssetIndex,
        worldAssetIndex: event.detail.worldAssetIndex,
        materialIdentity: event.detail.materialIdentity,
        worldIdentity: event.detail.worldIdentity,
        registeredAssetCount: event.detail.registeredAssetCount,
        sourceSurfaceIndex: event.detail.sourceSurfaceIndex,
        worldVertexCount: event.detail.worldVertexCount,
        worldIndexCount: event.detail.worldIndexCount,
        worldSurfaceCount: event.detail.worldSurfaceCount,
        firstVertex: event.detail.firstVertex,
        vertexCount: event.detail.vertexCount,
        baseIndex: event.detail.baseIndex,
        triangleCount: event.detail.triangleCount,
        materialReferenceKind: event.detail.materialReferenceKind,
        materialName: event.detail.materialName,
        convertedVertexCount: event.detail.convertedVertexCount,
        convertedIndexCount: event.detail.convertedIndexCount,
    };
    switch (event.detail.state) {
    case "loading":
        rendererStatus.textContent = event.detail.pipelineStage === "begin"
            ? "Preparing incremental fastfile extraction"
            : event.detail.pipelineStage === "source-wait"
                ? "Waiting for the next bounded fastfile chunk"
            : event.detail.pipelineStage === "inflate"
                ? "Inflating fastfile incrementally"
                : "Traversing fastfile incrementally";
        break;
    case "ready":
        rendererStatus.textContent = event.detail.synthetic
            ? "Fastfile world surface extracted"
            : "Fastfile GfxWorld surface extracted";
        appendLog(
            `[kisakcod-web] Extracted and converted one ` +
            `${event.detail.synthetic ? "synthetic " : ""}` +
            `${event.detail.sourceContainer} v${event.detail.fastfileVersion} ` +
            `${event.detail.sourceRepresentation} surface: ` +
            `${event.detail.convertedVertexCount} vertices, ` +
            `${event.detail.convertedIndexCount} local indices, material ` +
            `'${event.detail.materialName}'.`,
        );
        break;
    case "failed":
        rendererStatus.textContent = "Fastfile world-surface extraction failed";
        appendLog(
            `[kisakcod-web] Engine world surface (${event.detail.pipelineStage}): ` +
            `${event.detail.message}`,
            "error",
        );
        break;
    default:
        break;
    }
    updateRendererMaterialBinding();
});

globalThis.addEventListener("kisakcod:renderer-surface", (event) => {
    const previousState = runtime.rendererSurface.state;
    const previousRecoveryCount = runtime.rendererSurface.recoveryCount ?? 0;
    runtime.rendererSurface = {
        ...runtime.rendererSurface,
        ...event.detail,
    };
    switch (event.detail.state) {
    case "ready":
        if (previousState === "lost" ||
            runtime.rendererSurface.recoveryCount > previousRecoveryCount) {
            appendLog(
                `[kisakcod-web] Recreated the indexed surface from ` +
                `${formatBytes(runtime.rendererSurface.recoveryBytes)} of renderer-owned data.`,
            );
        } else {
            appendLog(
                `[kisakcod-web] Indexed engine surface resident: ` +
                `${runtime.rendererSurface.vertexCount} vertices, ` +
                `${runtime.rendererSurface.drawIndexCount} drawn indices.`,
            );
        }
        break;
    case "retained":
        appendLog(
            `[kisakcod-web] Retained ${formatBytes(runtime.rendererSurface.recoveryBytes)} ` +
            `of backend-neutral engine surface data.`,
        );
        break;
    case "failed":
        appendLog(`[kisakcod-web] Renderer surface: ${event.detail.message}`, "error");
        break;
    default:
        break;
    }
});

function renderAssetManifest(manifest) {
    assetManifest.replaceChildren();
    if (!manifest?.files) {
        assetManifest.hidden = true;
        return;
    }
    const values = [
        ["Language", manifest.language],
        ["Install profile", manifest.profile?.id ?? "unknown"],
        ["Target map", manifest.profile?.map ?? "unknown"],
        ["Validated files", manifest.files.length.toLocaleString()],
        ["IWD archives verified", manifest.archiveProbe?.archivesProbed?.toLocaleString() ?? "unknown"],
        ["Fastfiles verified", manifest.zoneProbe?.filesProbed?.toLocaleString() ?? "unknown"],
        ...manifest.files.map((file) => [file.path, formatBytes(file.size)]),
        ["IWD entries declared", manifest.archiveProbe?.entriesDeclared?.toLocaleString() ?? "unknown"],
    ];
    for (const [label, value] of values) {
        const item = document.createElement("div");
        const term = document.createElement("dt");
        const description = document.createElement("dd");
        term.textContent = label;
        description.textContent = value;
        item.append(term, description);
        assetManifest.append(item);
    }
    assetManifest.hidden = false;
}

globalThis.addEventListener("kisakcod:assets", (event) => {
    const assets = event.detail;
    const stateLabels = {
        checking: "Checking browser storage",
        empty: "Installation required",
        selecting: "Waiting for folder selection",
        validating: "Validating selected files",
        importing: "Importing local files",
        clearing: "Removing browser copy",
        ready: "Local installation ready",
        invalid: "Stored installation needs attention",
        failed: "Asset storage needs attention",
        unsupported: "Browser storage unavailable",
    };
    assetControl.dataset.assetState = assets.state;
    assetStateLabel.textContent = stateLabels[assets.state] ?? assets.state;
    assetMessage.textContent = assets.message;

    const busy = assets.state === "checking" || assets.state === "selecting" ||
        assets.state === "validating" || assets.state === "importing" ||
        assets.state === "clearing";
    selectInstallButton.disabled = busy || assets.state === "unsupported" || !assetStore;
    selectInstallButton.textContent = assets.manifest
        ? "Choose a different installation"
        : "Choose COD4 installation";
    clearAssetsButton.hidden = !assets.manifest;
    clearAssetsButton.disabled = busy;

    if (assets.state === "importing") {
        assetProgress.hidden = false;
        assetProgress.value = Number.isFinite(assets.progress) ? assets.progress : 0;
        const copied = formatBytes(assets.copiedBytes ?? 0);
        const total = formatBytes(assets.totalBytes ?? 0);
        assetProgress.setAttribute("aria-label", `Imported ${copied} of ${total}`);
    } else {
        assetProgress.hidden = true;
        assetProgress.value = 0;
    }
    const showRetentionWarning = assets.persistenceGranted === false &&
        Boolean(assets.manifest) && assets.state !== "clearing";
    assetRetention.hidden = !showRetentionWarning;
    renderAssetManifest(assets.manifest);

    const readyImportId = assets.state === "ready" ? assets.manifest?.importId ?? null : null;
    // A replacement is staged under a new ID, so the previously validated
    // import remains readable throughout selection and copying. Keep its
    // archive result unless the store explicitly begins removing it.
    const preserveExistingArchive = assets.manifest?.importId === archiveImportId &&
        ["checking", "selecting", "validating", "importing", "failed", "ready"]
            .includes(assets.state);
    if (readyImportId && readyImportId !== qcommonImportId && filesystemBridge &&
        typeof runtime.module?._KisakWeb_StartQcommonRuntime === "function") {
        if (archiveImportId !== null) {
            archiveImportId = null;
            runtime.module?._KisakWeb_CancelArchiveJob?.();
            publishArchiveState({
                state: "idle",
                message: "Waiting for qcommon pre-database startup",
            });
        }
        if (retailCensusImportId !== null) {
            retailCensusImportId = null;
            runtime.module?._KisakWeb_CancelRetailCensus?.();
        }
        filesystemBridge.invalidate();
        qcommonImportId = readyImportId;
        runtime.module._KisakWeb_StartQcommonRuntime();
    } else if (!readyImportId && qcommonImportId !== null && !preserveExistingArchive) {
        qcommonImportId = null;
        retailCensusImportId = null;
        archiveImportId = null;
        filesystemBridge?.invalidate();
        runtime.module?._KisakWeb_CancelQcommonRuntime?.();
        runtime.module?._KisakWeb_CancelRetailCensus?.();
        runtime.module?._KisakWeb_CancelArchiveJob?.();
        publishArchiveState({ state: "idle", message: "Waiting for a validated local archive" });
    }
});

function resizeCanvas() {
    const bounds = canvas.getBoundingClientRect();
    const scale = Math.min(globalThis.devicePixelRatio || 1, 2);
    const width = Math.max(1, Math.round(bounds.width * scale));
    const height = Math.max(1, Math.round(bounds.height * scale));
    if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
    }
}

globalThis.addEventListener("kisakcod:state", (event) => {
    if (event.detail.state === "renderer-lost") {
        runtime.contextLosses += 1;
        rendererStatus.textContent = "WebGL2 context lost";
    } else if (event.detail.state === "running" && runtime.contextLosses > 0) {
        rendererStatus.textContent = "WebGL2 restored";
    }
    setState(event.detail.state, event.detail.message);
});

globalThis.addEventListener("kisakcod:frame", (event) => {
    runtime.lastFrame = event.detail;
    frameCounter.textContent = `Frame ${event.detail.frame.toLocaleString()}`;
    canvasSize.textContent = `${event.detail.width} \u00d7 ${event.detail.height}`;
});

globalThis.addEventListener("kisakcod:system", (event) => {
    runtime.system = { ...event.detail };
    runtime.systemSamples.push(runtime.system);
    if (runtime.systemSamples.length > 80) {
        runtime.systemSamples.splice(0, runtime.systemSamples.length - 80);
    }
    if (event.detail.state === "ready") {
        systemStatus.textContent = "Monotonic clock ready";
        return;
    }
    systemStatus.textContent = `RAF tick ${event.detail.framePumpTicks.toLocaleString()}`;
});

globalThis.addEventListener("kisakcod:engine", (event) => {
    runtime.engine = { ...event.detail };
    physicsStatus.textContent = event.detail.state === "ready"
        ? "ODE + qcommon verified"
        : "ODE + commands initialized";
});

const resizeObserver = new ResizeObserver(resizeCanvas);
resizeObserver.observe(canvas);
resizeCanvas();

appendLog("Launcher initialized; requesting the browser engine module.");

selectInstallButton.addEventListener("click", async () => {
    if (!assetStore) {
        return;
    }
    const previousState = runtime.assets;
    publishAssetState({
        ...previousState,
        state: "selecting",
        message: "Select the root of a legally owned Call of Duty 4 installation",
    });
    const persistenceRequest = assetStore.requestPersistence();
    try {
        const entries = await selectInstallEntries(installFolderInput);
        await persistenceRequest;
        if (!entries) {
            publishAssetState(previousState);
            return;
        }
        await assetStore.importEntries(entries);
        appendLog("[kisakcod-web] Local installation persisted and probed successfully.");
    } catch (error) {
        appendLog(`[kisakcod-web] Asset import: ${error.message}`, "error");
        if (runtime.assets.state === "selecting") {
            publishAssetState({
                state: "failed",
                message: error.message,
                error: error.code ?? "SELECTION_FAILED",
                retained: Boolean(previousState.manifest),
                manifest: previousState.manifest,
            });
        }
    }
});

clearAssetsButton.addEventListener("click", async () => {
    if (!assetStore || !globalThis.confirm(
        "Remove the locally imported COD4 files from this browser? Your Steam installation is unaffected.",
    )) {
        return;
    }
    try {
        await assetStore.clear();
        appendLog("[kisakcod-web] Removed the browser-local asset import.");
    } catch (error) {
        appendLog(`[kisakcod-web] Could not clear browser storage: ${error.message}`, "error");
    }
});

try {
    const { default: createKisakCOD } = await import("./kisakcod.mjs");
    runtime.module = await createKisakCOD({
        canvas,
        locateFile(path) {
            return new URL(path, import.meta.url).href;
        },
        print(message) {
            appendLog(message);
        },
        printErr(message) {
            appendLog(message, "error");
        },
        onAbort(reason) {
            setState("failed", `WebAssembly aborted: ${reason}`);
        },
    });
    assetStore = createBrowserAssetStore(runtime.module, { onState: publishAssetState });
    filesystemBridge = installBrowserFilesystemBridge(runtime.module, assetStore);
    runtime.filesystemBridge = filesystemBridge;
    runtime.assetStore = assetStore;
    try {
        await assetStore.initialize();
    } catch (error) {
        appendLog(`[kisakcod-web] Browser asset storage: ${error.message}`, "error");
        publishAssetState({
            state: "failed",
            message: "Browser asset storage could not be initialized",
            error: error.code ?? "STORAGE_FAILED",
        });
    }
} catch (error) {
    appendLog(error?.stack ?? error, "error");
    setState("failed", "The WebAssembly module could not start");
}
