import { createBrowserAssetStore, selectInstallEntries } from "./asset_store.mjs";
import { detectBrowserCapabilities } from "./browser_capabilities.mjs";
import { createEngineWorkerHost } from "./product_engine_worker_host.mjs";
import { createVisibilityCheckpoint } from "./product_checkpoint_controller.mjs";
import { createInputControllerCore } from "./input_controller_core.mjs";
import { createLatestMountController } from "./product_mount_controller.mjs";
import { createBrowserQuit } from "./browser_quit.mjs";

/** @template {Element} T @param {string} selector @returns {T} */
function requiredElement(selector)
{
    const element = document.querySelector(selector);
    if (!element) throw new Error(`Required product element is missing: ${selector}`);
    return /** @type {T} */ (element);
}

const canvas = /** @type {HTMLCanvasElement} */ (requiredElement("#game-canvas"));
const gameTextInput = /** @type {HTMLTextAreaElement} */ (
    requiredElement("#game-text-input"));
const frameCounter = /** @type {HTMLElement} */ (requiredElement("#frame-counter"));
const cinematicStatus = /** @type {HTMLElement} */ (requiredElement("#cinematic-status"));
const saveStatus = /** @type {HTMLElement} */ (requiredElement("#save-status"));
const bootLog = /** @type {HTMLElement} */ (requiredElement("#boot-log"));
const assetControl = /** @type {HTMLElement} */ (requiredElement(".asset-control"));
const assetStateLabel = /** @type {HTMLElement} */ (requiredElement("#asset-state-label"));
const assetMessage = /** @type {HTMLElement} */ (requiredElement("#asset-message"));
const assetProgress = /** @type {HTMLProgressElement} */ (requiredElement("#asset-progress"));
const storageRetention = /** @type {HTMLElement} */ (requiredElement("#storage-retention"));
const storageCapacity = /** @type {HTMLElement} */ (requiredElement("#storage-capacity"));
const storageWarning = /** @type {HTMLElement} */ (requiredElement("#storage-warning"));
const retryPersistenceButton = /** @type {HTMLButtonElement} */ (
    requiredElement("#retry-persistence-button"));
const selectInstallButton = /** @type {HTMLButtonElement} */ (
    requiredElement("#select-install-button"));
const portableInstallButton = /** @type {HTMLButtonElement} */ (
    requiredElement("#portable-install-button"));
const clearAssetsButton = /** @type {HTMLButtonElement} */ (
    requiredElement("#clear-assets-button"));
const installFolderInput = /** @type {HTMLInputElement} */ (
    requiredElement("#install-folder-input"));
const commandForm = /** @type {HTMLFormElement} */ (requiredElement("#engine-command-form"));
const commandInput = /** @type {HTMLInputElement} */ (requiredElement("#engine-command-input"));
const commandSubmit = /** @type {HTMLButtonElement} */ (requiredElement("#engine-command-submit"));
const commandStatus = /** @type {HTMLOutputElement} */ (requiredElement("#engine-command-status"));

let engine = null;
let assetStore = null;
let mountController = null;
let inputController = null;
let checkpointController = null;
let resizeObserver = null;
let disposePromise = null;
let quitController = null;
let assetState = { state: "checking", message: "Waiting for the engine" };
const logs = [];

function appendLog(message, level = "info")
{
    logs.push({ level, message: String(message) });
    if (logs.length > 160) logs.splice(0, logs.length - 160);
    bootLog.textContent = logs.map(({ level: entryLevel, message: text }) =>
        `${entryLevel === "error" ? "!" : ">"} ${text}`).join("\n");
    bootLog.scrollTop = bootLog.scrollHeight;
}

function renderCheckpointStatus(detail)
{
    saveStatus.hidden = false;
    saveStatus.dataset.state = detail.state;
    saveStatus.textContent = detail.message;
    if (detail.error) {
        appendLog(`[kisakcod-web] Checkpoint: ${detail.error.message}`, "error");
    }
}

function labelForAssetState(state)
{
    return ({
        checking: "Checking browser storage",
        empty: "Installation required",
        selecting: "Selecting installation",
        validating: "Validating installation",
        importing: "Copying local files",
        ready: "Installation ready",
        clearing: "Removing local copy",
        invalid: "Stored installation invalid",
        failed: "Installation unavailable",
        unsupported: "Browser storage unsupported",
    })[state] ?? state;
}

function formatBytes(bytes)
{
    if (!Number.isFinite(bytes)) return "Unavailable";
    const units = ["B", "KiB", "MiB", "GiB", "TiB"];
    let value = Math.max(0, bytes);
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
        value /= 1024;
        ++unit;
    }
    return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function renderStorageStatus(detail)
{
    const usage = detail.storageEstimate?.usage;
    const quota = detail.storageEstimate?.quota;
    const knownCapacity = Number.isFinite(usage) && Number.isFinite(quota) && quota > 0;
    const constrained = knownCapacity && usage / quota >= 0.8;
    storageRetention.textContent = detail.persistenceGranted === true
        ? "Persistent storage granted; browser data controls can still remove local files."
        : detail.persistenceGranted === false
            ? "Persistent storage not granted; the browser may evict imported files."
            : "Persistent storage permission has not been checked.";
    storageCapacity.textContent = knownCapacity
        ? `${formatBytes(usage)} used of ${formatBytes(quota)} (${Math.round(usage / quota * 100)}%).`
        : "Browser storage usage and quota are unavailable.";
    storageWarning.hidden = detail.persistenceGranted !== false && !constrained;
    storageWarning.textContent = constrained
        ? "Browser storage is nearly full. Free browser storage or re-import if files are removed."
        : "Retry persistence below; if files are evicted, choose the legal installation again.";
    retryPersistenceButton.hidden = detail.persistenceGranted !== false;
}

function renderAssetState(detail)
{
    assetState = { ...detail };
    assetControl.dataset.assetState = detail.state;
    assetStateLabel.textContent = labelForAssetState(detail.state);
    assetMessage.textContent = detail.message;
    const busy = ["checking", "selecting", "validating", "importing", "clearing"]
        .includes(detail.state);
    const pickerDisabled = busy || detail.state === "unsupported" || !assetStore;
    selectInstallButton.disabled = pickerDisabled;
    portableInstallButton.disabled = pickerDisabled;
    selectInstallButton.textContent = detail.manifest
        ? "Choose a different installation"
        : "Choose COD4 installation";
    clearAssetsButton.hidden = !detail.manifest;
    clearAssetsButton.disabled = busy;
    assetProgress.hidden = detail.state !== "importing";
    assetProgress.value = Number.isFinite(detail.progress) ? detail.progress : 0;
    renderStorageStatus(detail);

    if (detail.state === "ready" && detail.manifest?.importId &&
        detail.manifest.importId !== mountController?.activeImportId) {
        void mountController?.select(detail.manifest).catch(() => {});
    } else if (detail.state !== "ready") {
        mountController?.invalidate();
    }
}

async function chooseInstallation(portable = false)
{
    const previous = assetState;
    renderAssetState({
        ...previous,
        state: "selecting",
        message: "Select a legally owned Call of Duty 4 installation",
    });
    const persistence = assetStore.requestPersistence();
    try {
        const entries = await selectInstallEntries(installFolderInput, { portable, module: engine });
        await persistence;
        if (!entries) {
            renderAssetState({ ...previous, ...assetStore.storageStatus });
            return;
        }
        await assetStore.importEntries(entries);
    } catch (error) {
        appendLog(`[kisakcod-web] Asset import: ${error.message}`, "error");
        if (assetState.state === "selecting") {
            renderAssetState({
                ...previous,
                state: "failed",
                message: error.message,
                error: error.code ?? "SELECTION_FAILED",
            });
        }
    }
}

function resizeCanvas()
{
    const bounds = canvas.getBoundingClientRect();
    const scale = Math.min(globalThis.devicePixelRatio || 1, 2);
    const width = Math.max(1, Math.round(bounds.width * scale));
    const height = Math.max(1, Math.round(bounds.height * scale));
    if (!engine) {
        canvas.width = width;
        canvas.height = height;
        return;
    }
    void engine.resize(width, height).catch((error) =>
        appendLog(`[kisakcod-web] Resize: ${error.message}`, "error"));
}

globalThis.addEventListener("kisakcod:display", (event) => {
    const { detail } = /** @type {CustomEvent} */ (event);
    const wrapper = /** @type {HTMLElement} */ (document.querySelector(".canvas-wrap"));
    if (Number.isInteger(detail.width) && Number.isInteger(detail.height) && detail.width > 0 && detail.height > 0)
        wrapper.style.aspectRatio = detail.fixed
            ? `${detail.width} / ${detail.height}` : "";
});

const handleRuntimeState = (event) => {
    document.documentElement.dataset.runtimeState = event.detail.state;
    if (event.detail.message) appendLog(`[kisakcod-web] ${event.detail.message}`);
    if (event.detail.state === "quitting") void quitController?.request();
};
const handleFrame = (event) => {
    frameCounter.textContent = `Frame ${event.detail.frame.toLocaleString()}`;
};
const handleSystem = (event) => {
    if (event.detail?.framePumpTicks) {
        frameCounter.textContent = `Frame ${event.detail.framePumpTicks.toLocaleString()}`;
    }
};
const handleDatabase = (event) => {
    if (event.detail?.state === "failed") {
        appendLog(`[kisakcod-web] Database: ${event.detail.message}`, "error");
    }
};
const handleCinematic = (event) => {
    const { state, name, reason } = event.detail;
    cinematicStatus.hidden = state !== "skipped" && state !== "failed";
    cinematicStatus.textContent = state === "skipped"
        ? "Movie missing from browser storage. Select your installation again to import movies."
        : state === "failed" ? `Movie playback failed: ${reason}` : "";
    appendLog(`[kisakcod-web] Cinematic '${name}' ${state}${reason ? `: ${reason}` : ""}.`);
};

const capabilityReport = await detectBrowserCapabilities();
if (!capabilityReport.supported) {
    const missing = capabilityReport.missingRequired.map(
        (name) => capabilityReport.capabilities[name].label);
    document.documentElement.dataset.runtimeState = "unsupported";
    frameCounter.textContent = "Unsupported browser";
    appendLog(`[kisakcod-web] Missing browser features: ${missing.join(", ")}.`, "error");
    renderAssetState({
        state: "unsupported",
        message: `This browser is missing: ${missing.join(", ")}. Use a current Chromium-based browser.`,
        error: "BROWSER_CAPABILITIES",
    });
} else {
globalThis.addEventListener("kisakcod:state", handleRuntimeState);
globalThis.addEventListener("kisakcod:frame", handleFrame);
globalThis.addEventListener("kisakcod:system", handleSystem);
globalThis.addEventListener("kisakcod:database", handleDatabase);
globalThis.addEventListener("kisakcod:cinematic", handleCinematic);

const handleSelectInstall = () => { void chooseInstallation(false); };
const handlePortableInstall = () => { void chooseInstallation(true); };
const handleClearAssets = async () => {
    if (!confirm("Remove the imported COD4 files from this browser?")) return;
    try {
        await mountController.clear();
        await assetStore.clear();
    } catch (error) {
        appendLog(`[kisakcod-web] Remove browser copy: ${error.message}`, "error");
    }
};
const handleRetryPersistence = async () => {
    retryPersistenceButton.disabled = true;
    try {
        const granted = await assetStore.requestPersistence();
        renderAssetState({ ...assetState, ...assetStore.storageStatus });
        appendLog(`[kisakcod-web] Persistent storage ${granted ? "granted" : "not granted"}.`);
    } catch (error) {
        appendLog(`[kisakcod-web] Persistence request: ${error.message}`, "error");
    } finally {
        retryPersistenceButton.disabled = false;
    }
};
const handleCommand = async (event) => {
    event.preventDefault();
    const command = commandInput.value.trim();
    commandInput.disabled = true;
    commandSubmit.disabled = true;
    try {
        const accepted = await engine.submitCanonicalCommand(command);
        if (accepted !== 1) throw new Error("The canonical runtime is not ready.");
        commandInput.value = "";
        commandStatus.textContent = `Accepted: ${command}`;
    } catch (error) {
        commandStatus.textContent = error.message;
    } finally {
        commandInput.disabled = false;
        commandSubmit.disabled = false;
        commandInput.focus();
    }
};

selectInstallButton.addEventListener("click", handleSelectInstall);
portableInstallButton.addEventListener("click", handlePortableInstall);
clearAssetsButton.addEventListener("click", handleClearAssets);
retryPersistenceButton.addEventListener("click", handleRetryPersistence);
commandForm.addEventListener("submit", handleCommand);

function disposeApp()
{
    if (disposePromise) return disposePromise;
    inputController?.dispose();
    checkpointController?.dispose();
    resizeObserver?.disconnect();
    globalThis.removeEventListener("kisakcod:state", handleRuntimeState);
    globalThis.removeEventListener("kisakcod:frame", handleFrame);
    globalThis.removeEventListener("kisakcod:system", handleSystem);
    globalThis.removeEventListener("kisakcod:database", handleDatabase);
    globalThis.removeEventListener("kisakcod:cinematic", handleCinematic);
    selectInstallButton.removeEventListener("click", handleSelectInstall);
    portableInstallButton.removeEventListener("click", handlePortableInstall);
    clearAssetsButton.removeEventListener("click", handleClearAssets);
    retryPersistenceButton.removeEventListener("click", handleRetryPersistence);
    commandForm.removeEventListener("submit", handleCommand);
    globalThis.removeEventListener("pagehide", handlePageHide);
    disposePromise = (async () => {
        await mountController?.dispose();
        try {
            await assetStore?.dispose();
        } finally {
            await engine?.dispose();
        }
    })();
    return disposePromise;
}

const handlePageHide = () => { void disposeApp(); };
globalThis.addEventListener("pagehide", handlePageHide, { once: true });

appendLog("Starting the browser engine Worker.");
try {
    engine = createEngineWorkerHost(canvas, {
        managePageLifecycle: false,
        onLog: appendLog,
        onAbort(reason) {
            document.documentElement.dataset.runtimeState = "failed";
            appendLog(`[kisakcod-web] WebAssembly aborted: ${reason}`, "error");
        },
        onFilesystemDirty() { checkpointController?.markDirty(); },
    });
    await engine.ready;
    quitController = createBrowserQuit({
        engine,
        dialog: requiredElement("#quit-dialog"),
        onStop() {
            inputController?.dispose();
            checkpointController?.dispose();
            resizeObserver?.disconnect();
            mountController?.invalidate();
            commandInput.disabled = commandSubmit.disabled = true;
        },
        dispose: disposeApp,
    });
    mountController = createLatestMountController({
        mount: (manifest) => engine.mountAssets(manifest),
        unmount: () => engine.flushAndUnmount(),
        onMounted() {
            appendLog("[kisakcod-web] Local installation mounted; canonical runtime started.");
        },
        onFailed(error, manifest) {
            appendLog(`[kisakcod-web] Engine mount: ${error.message}`, "error");
            if (assetState.state === "ready" &&
                assetState.manifest?.importId === manifest?.importId) {
                renderAssetState({
                    ...assetState,
                    state: "failed",
                    message: error.message,
                    error: error.code ?? "ENGINE_MOUNT_FAILED",
                    retained: true,
                });
            }
        },
    });
    checkpointController = createVisibilityCheckpoint({
        checkpoint: () => engine.checkpoint(),
        isMounted: () => ["mounted", "flush-failed-retryable"]
            .includes(engine.filesystemState),
        onStatus: renderCheckpointStatus,
    });
    inputController = createInputControllerCore({
        canvas,
        textInput: gameTextInput,
        commandInput,
        sendInput: (event) => engine.input(event),
        onFailure(error) {
            document.documentElement.dataset.runtimeState = "failed";
            appendLog(`[kisakcod-web] Input transport failed: ${
                error instanceof Error ? error.message : String(error)}`, "error");
        },
    });
    resizeObserver = new ResizeObserver(resizeCanvas);
    resizeObserver.observe(canvas);
    resizeCanvas();
    commandInput.disabled = false;
    commandSubmit.disabled = false;
    assetStore = createBrowserAssetStore(engine, { onState: renderAssetState });
    renderAssetState(assetState);
    await assetStore.initialize();
} catch (error) {
    document.documentElement.dataset.runtimeState = "failed";
    appendLog(error?.stack ?? error, "error");
    renderAssetState({
        state: "failed",
        message: "The browser engine could not start",
        error: error?.code ?? "STARTUP_FAILED",
    });
}
}
