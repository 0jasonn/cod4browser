import { createBrowserAssetStore, selectInstallEntries } from "./asset_store.mjs";
import { createEngineWorkerHost } from "./engine_worker_host.mjs";
import { createInputControllerCore } from "./input_controller_core.mjs";

const canvas = document.querySelector("#game-canvas");
const runtimeLabel = document.querySelector("#runtime-label");
const runtimeMessage = document.querySelector("#runtime-message");
const bootLog = document.querySelector("#boot-log");
const frameCounter = document.querySelector("#frame-counter");
const canvasSize = document.querySelector("#canvas-size");
const rendererStatus = document.querySelector("#renderer-status");
const systemStatus = document.querySelector("#system-status");
const selectInstallButton = document.querySelector("#select-install-button");
const portableInstallButton = document.querySelector("#portable-install-button");
const clearAssetsButton = document.querySelector("#clear-assets-button");
const installFolderInput = document.querySelector("#install-folder-input");
const assetStateLabel = document.querySelector("#asset-state-label");
const assetMessage = document.querySelector("#asset-message");
const assetProgress = document.querySelector("#asset-progress");
const assetManifest = document.querySelector("#asset-manifest");
const assetControl = document.querySelector(".asset-control");
const assetRetention = document.querySelector("#asset-retention");
const engineAssetStatus = document.querySelector("#engine-asset-status");
const engineCommandForm = document.querySelector("#engine-command-form");
const engineCommandInput = document.querySelector("#engine-command-input");
const engineCommandSubmit = document.querySelector("#engine-command-submit");
const engineCommandStatus = document.querySelector("#engine-command-status");
const rendererSceneViewEvidence = document.querySelector(
    "#renderer-scene-view-evidence");
const rendererSceneFrameEvidence = document.querySelector(
    "#renderer-scene-frame-evidence");
const engineLifecycleEvidence = document.querySelector(
    "#engine-lifecycle-evidence");
const rendererFxEvidence = document.querySelector("#renderer-fx-evidence");
const audioPlaybackEvidence = document.querySelector("#audio-playback-evidence");
const databaseEvidence = document.querySelector("#database-evidence");

const runtime = {
    state: "loading",
    module: null,
    logs: [],
    lastFrame: null,
    contextLosses: 0,
    engine: null,
    database: { stage: "idle", stopStage: "" },
    system: null,
    systemSamples: [],
    assets: {
        state: "checking",
        message: "Waiting for the WebAssembly asset probe",
    },
    rendererSurface: { state: "idle", message: "Waiting for an engine surface" },
    rendererTexture: { state: "idle", message: "Waiting for a supported engine image" },
    rendererShader: { state: "idle", message: "Waiting for a retail shader contract" },
    rendererAa: { state: "idle", message: "Waiting for the scene sample target" },
    rendererSceneView: null,
    rendererSceneFrame: null,
    rendererFx: [],
    audioPlayback: [],
    engineLifecycle: [],
    input: {
        keyEvents: 0,
        mouseEvents: 0,
        pointerLocked: false,
        cursorVisible: false,
        absoluteMouse: false,
    },
};
globalThis.__KISAKCOD_WEB__ = runtime;

async function submitCanonicalCommand(command) {
    const text = String(command).trim();
    if (!text || new TextEncoder().encode(text).byteLength > 1023) {
        throw new Error("Enter a command between 1 and 1023 UTF-8 bytes.");
    }
    if (typeof runtime.module?.callProbe !== "function") {
        throw new Error("The engine Worker is not ready.");
    }
    const bytes = new TextEncoder().encode(`${text}\0`);
    const accepted = await runtime.module.callProbe(
        "_KisakWeb_SubmitCanonicalCommand",
        [bytes],
        [{ kind: "pointer", index: 0 }],
    );
    if (accepted !== 1) {
        throw new Error("The canonical client/server runtime is not ready.");
    }
    return accepted;
}

runtime.submitCanonicalCommand = submitCanonicalCommand;

let assetStore = null;
let filesystemBridge = null;
let mountedImportId = null;
let mountingImportId = null;

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
    if (runtime.logs.length > 512) {
        runtime.logs.splice(0, runtime.logs.length - 512);
    }

    bootLog.textContent = runtime.logs
        .map((entry) => `${entry.level === "error" ? "!" : ">"} ${entry.text}`)
        .join("\n");
    bootLog.scrollTop = bootLog.scrollHeight;

    if (text.startsWith("[kisakcod-web] Renderer:")) {
        rendererStatus.textContent = "WebGL2 initialized";
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

function publishAssetState(detail) {
    runtime.assets = { ...detail };
    globalThis.dispatchEvent(new CustomEvent("kisakcod:assets", {
        detail: runtime.assets,
    }));
}

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
});

globalThis.addEventListener("kisakcod:renderer-aa", (event) => {
    const previousGeneration = runtime.rendererAa?.resourceGeneration ?? 0;
    runtime.rendererAa = structuredClone(event.detail);
    if (event.detail.state === "ready" &&
        event.detail.resourceGeneration > previousGeneration) {
        appendLog(
            `[kisakcod-web] Using ${event.detail.activeSamples}x anti-aliasing; ` +
            `the scene resolves before post effects and UI.`,
        );
    } else if (event.detail.state === "fallback" || event.detail.state === "failed") {
        appendLog(`[kisakcod-web] Anti-aliasing: ${event.detail.message}`, "warn");
    }
});

globalThis.addEventListener("kisakcod:renderer-texture", (event) => {
    const previousState = runtime.rendererTexture.state;
    const previousRecoveryCount = runtime.rendererTexture.recoveryCount ?? 0;
    runtime.rendererTexture = {
        ...runtime.rendererTexture,
        ...event.detail,
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
    const pickerDisabled = busy || assets.state === "unsupported" || !assetStore;
    selectInstallButton.disabled = pickerDisabled;
    portableInstallButton.disabled = pickerDisabled;
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
    const preserveExistingMount = assets.manifest?.importId === mountedImportId &&
        ["checking", "selecting", "validating", "importing", "failed", "ready"]
            .includes(assets.state);
    if (readyImportId && readyImportId !== mountedImportId &&
        readyImportId !== mountingImportId && filesystemBridge) {
        mountingImportId = readyImportId;
        void (async () => {
            try {
                await runtime.module.mount(assets.manifest);
                if (runtime.assets.state !== "ready" ||
                    runtime.assets.manifest?.importId !== readyImportId) return;
                mountedImportId = readyImportId;
            } catch (error) {
                appendLog(`[kisakcod-web] Engine filesystem mount: ${error.message}`, "error");
                publishAssetState({
                    ...runtime.assets,
                    state: "failed",
                    message: error.message,
                    error: error.code ?? "ENGINE_MOUNT_FAILED",
                    retained: true,
                });
            } finally {
                if (mountingImportId === readyImportId) mountingImportId = null;
            }
        })();
    } else if (!readyImportId && mountedImportId !== null && !preserveExistingMount) {
        mountedImportId = null;
        filesystemBridge?.invalidate();
    }
});

function resizeCanvas() {
    const bounds = canvas.getBoundingClientRect();
    const scale = Math.min(globalThis.devicePixelRatio || 1, 2);
    const width = Math.max(1, Math.round(bounds.width * scale));
    const height = Math.max(1, Math.round(bounds.height * scale));
    if (!runtime.module && (canvas.width !== width || canvas.height !== height)) {
        canvas.width = width;
        canvas.height = height;
    }
    runtime.module?.resize?.(width, height);
}

function installBrowserInput()
{
    createInputControllerCore({
        canvas,
        sendInput(event) {
            const result = runtime.module?.input?.(event);
            if (event.type === "key") runtime.input.keyEvents += 1;
            else runtime.input.mouseEvents += 1;
            return result;
        },
        onState(state) { Object.assign(runtime.input, state); },
        onFailure(error) {
            setState("failed", `Input transport failed: ${error?.message ?? error}`);
        },
    });
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
});

globalThis.addEventListener("kisakcod:database", (event) => {
    // Database trace events are deliberately normalized deltas.  Retain the
    // deterministic prefix state so the final stop publication describes the
    // initialization envelope as well as the last file operation.
    runtime.database = { ...runtime.database, ...event.detail };
    // Keep the existing database trace observable without exposing or
    // influencing engine state. This bounded mirror lets diagnostics inspect
    // the first generated-loader failure after a map request.
    databaseEvidence.textContent = JSON.stringify(runtime.database);
});

globalThis.addEventListener("kisakcod:renderer-scene-view", (event) => {
    runtime.rendererSceneView = structuredClone(event.detail);
    rendererSceneViewEvidence.textContent = JSON.stringify(
        runtime.rendererSceneView);
});

globalThis.addEventListener("kisakcod:renderer-scene-frame", (event) => {
    runtime.rendererSceneFrame = structuredClone(event.detail);
    rendererSceneFrameEvidence.textContent = JSON.stringify(
        runtime.rendererSceneFrame);
});

globalThis.addEventListener("kisakcod:renderer-fx", (event) => {
    runtime.rendererFx.push(structuredClone(event.detail));
    if (runtime.rendererFx.length > 8) runtime.rendererFx.shift();
    rendererFxEvidence.textContent = JSON.stringify(runtime.rendererFx);
});

globalThis.addEventListener("kisakcod:audio-playback", (event) => {
    const detail = structuredClone(event.detail);
    const alias = detail.aliasName || "<unnamed>";
    const existing = runtime.audioPlayback.find((entry) => entry.aliasName === alias);
    if (existing) {
        existing.count += 1;
        existing.last = detail;
    } else if (runtime.audioPlayback.length < 32) {
        runtime.audioPlayback.push({ aliasName: alias, count: 1, last: detail });
    }
    audioPlaybackEvidence.textContent = JSON.stringify(runtime.audioPlayback);
});

globalThis.addEventListener("kisakcod:engine-lifecycle", (event) => {
    runtime.engineLifecycle.push(structuredClone(event.detail));
    if (runtime.engineLifecycle.length > 512) runtime.engineLifecycle.shift();
    engineLifecycleEvidence.textContent = JSON.stringify(
        runtime.engineLifecycle);
});

const resizeObserver = new ResizeObserver(resizeCanvas);
resizeObserver.observe(canvas);
resizeCanvas();

appendLog("Launcher initialized; requesting the browser engine module.");

engineCommandForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    const command = engineCommandInput.value.trim();
    engineCommandInput.disabled = true;
    engineCommandSubmit.disabled = true;
    engineCommandStatus.textContent = "Submitting to the engine Worker…";
    try {
        await submitCanonicalCommand(command);
        engineCommandInput.value = "";
        engineCommandStatus.textContent = `Accepted: ${command}`;
        appendLog(`[kisakcod-web] Console accepted: ${command}`);
    } catch (error) {
        engineCommandStatus.textContent = error?.message ?? String(error);
    } finally {
        engineCommandInput.disabled = false;
        engineCommandSubmit.disabled = false;
        engineCommandInput.focus();
    }
});

async function chooseInstallation({ portable = false } = {})
{
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
        const entries = await selectInstallEntries(installFolderInput, { portable });
        await persistenceRequest;
        if (!entries) {
            publishAssetState(previousState);
            return;
        }
        await runtime.module?.unmount?.();
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
}

selectInstallButton.addEventListener("click", () => chooseInstallation());
portableInstallButton.addEventListener("click", () => chooseInstallation({ portable: true }));

clearAssetsButton.addEventListener("click", async () => {
    if (!assetStore || !globalThis.confirm(
        "Remove the locally imported COD4 files from this browser? Your Steam installation is unaffected.",
    )) {
        return;
    }
    try {
        await runtime.module?.unmount?.();
        await assetStore.clear();
        appendLog("[kisakcod-web] Removed the browser-local asset import.");
    } catch (error) {
        appendLog(`[kisakcod-web] Could not clear browser storage: ${error.message}`, "error");
    }
});

try {
    runtime.module = createEngineWorkerHost(canvas, {
        onLog(message, level) {
            appendLog(message, level);
        },
        onAbort(reason) {
            setState("failed", `WebAssembly aborted: ${reason}`);
        },
    });
    await runtime.module.ready;
    installBrowserInput();
    engineCommandInput.disabled = false;
    engineCommandSubmit.disabled = false;
    assetStore = createBrowserAssetStore(runtime.module, { onState: publishAssetState });
    filesystemBridge = runtime.module;
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
