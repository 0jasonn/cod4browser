import { createBrowserAssetStore, selectInstallEntries } from "./asset_store.mjs";
import { createEngineWorkerHost } from "./engine_worker_host.mjs";
import { createInputController } from "./product_input_controller.mjs";

const canvas = document.querySelector("#game-canvas");
const frameCounter = document.querySelector("#frame-counter");
const cinematicStatus = document.querySelector("#cinematic-status");
const bootLog = document.querySelector("#boot-log");
const assetControl = document.querySelector(".asset-control");
const assetStateLabel = document.querySelector("#asset-state-label");
const assetMessage = document.querySelector("#asset-message");
const assetProgress = document.querySelector("#asset-progress");
const selectInstallButton = document.querySelector("#select-install-button");
const portableInstallButton = document.querySelector("#portable-install-button");
const clearAssetsButton = document.querySelector("#clear-assets-button");
const installFolderInput = document.querySelector("#install-folder-input");
const commandForm = document.querySelector("#engine-command-form");
const commandInput = document.querySelector("#engine-command-input");
const commandSubmit = document.querySelector("#engine-command-submit");
const commandStatus = document.querySelector("#engine-command-status");

let engine = null;
let assetStore = null;
let activeImportId = null;
let mountInFlight = null;
let inputController = null;
let resizeObserver = null;
let disposePromise = null;
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

    const importId = detail.state === "ready" ? detail.manifest?.importId : null;
    if (importId && importId !== activeImportId && !mountInFlight) {
        mountInFlight = engine.mountAssets(detail.manifest)
            .then(({ runtime }) => {
                if (assetState.state !== "ready" ||
                    assetState.manifest?.importId !== importId) return;
                if (runtime !== true) throw Object.assign(
                    new Error("The canonical runtime did not accept the mounted installation."),
                    { code: "RUNTIME_MOUNT_FAILED" });
                activeImportId = importId;
                appendLog("[kisakcod-web] Local installation mounted; canonical runtime started.");
            })
            .catch((error) => {
                appendLog(`[kisakcod-web] Engine mount: ${error.message}`, "error");
                renderAssetState({
                    ...assetState,
                    state: "failed",
                    message: error.message,
                    error: error.code ?? "ENGINE_MOUNT_FAILED",
                    retained: true,
                });
            })
            .finally(() => { mountInFlight = null; });
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
        const entries = await selectInstallEntries(installFolderInput, { portable });
        await persistence;
        if (!entries) {
            renderAssetState(previous);
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

const handleRuntimeState = (event) => {
    document.documentElement.dataset.runtimeState = event.detail.state;
    if (event.detail.message) appendLog(`[kisakcod-web] ${event.detail.message}`);
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
    cinematicStatus.hidden = false;
    cinematicStatus.textContent = event.detail.message;
    appendLog(`[kisakcod-web] Cinematic '${event.detail.name}' skipped: ${event.detail.reason}.`);
};

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
        await engine.flushAndUnmount();
        activeImportId = null;
        await assetStore.clear();
    } catch (error) {
        appendLog(`[kisakcod-web] Remove browser copy: ${error.message}`, "error");
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
commandForm.addEventListener("submit", handleCommand);

function disposeApp()
{
    if (disposePromise) return disposePromise;
    inputController?.dispose();
    resizeObserver?.disconnect();
    globalThis.removeEventListener("kisakcod:state", handleRuntimeState);
    globalThis.removeEventListener("kisakcod:frame", handleFrame);
    globalThis.removeEventListener("kisakcod:system", handleSystem);
    globalThis.removeEventListener("kisakcod:database", handleDatabase);
    globalThis.removeEventListener("kisakcod:cinematic", handleCinematic);
    selectInstallButton.removeEventListener("click", handleSelectInstall);
    portableInstallButton.removeEventListener("click", handlePortableInstall);
    clearAssetsButton.removeEventListener("click", handleClearAssets);
    commandForm.removeEventListener("submit", handleCommand);
    globalThis.removeEventListener("pagehide", handlePageHide);
    disposePromise = (async () => {
        await mountInFlight?.catch(() => {});
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
    });
    await engine.ready;
    inputController = createInputController({ canvas, commandInput, engine });
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
