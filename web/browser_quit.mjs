/**
 * @param {{engine: {flushAndUnmount: () => Promise<unknown>, filesystemState?: string},
 *   onStop: () => void, dispose: () => Promise<unknown>,
 *   dialog: HTMLDialogElement}} options
 */
export function createBrowserQuit({ engine, onStop, dispose, dialog })
{
    const status = dialog.querySelector("p");
    const button = dialog.querySelector("button");
    let started = false;
    let saved = false;
    let terminated = false;
    let pending = null;

    const request = () => {
        if (saved || pending) return pending ?? Promise.resolve();
        if (!started) {
            started = true;
            onStop();
            if (document.pointerLockElement) document.exitPointerLock();
            dialog.showModal();
        }
        button.disabled = true;
        status.textContent = "Saving your browser profile…";
        const operation = (async () => {
            try {
                await engine.flushAndUnmount();
                await dispose();
                saved = true;
                document.documentElement.dataset.runtimeState = "stopped";
                status.textContent = "Game closed. Your browser profile and imported installation are saved.";
                button.textContent = "Start game";
            } catch (error) {
                document.documentElement.dataset.runtimeState = "quit-save-failed";
                terminated = engine.filesystemState === "terminated";
                status.textContent = terminated
                    ? `Saving could not be confirmed: ${error.message}. The engine stopped; unsaved changes may be lost.`
                    : `Could not save: ${error.message}. Keep this tab open and retry.`;
                button.textContent = terminated ? "Reload launcher" : "Retry save and quit";
            } finally {
                button.disabled = false;
                button.focus();
            }
        })();
        pending = operation;
        operation.finally(() => { pending = null; });
        return operation;
    };
    dialog.addEventListener("cancel", (event) => event.preventDefault());
    button.addEventListener("click", () => {
        if (saved || terminated) location.reload();
        else void request();
    });
    return { request };
}
