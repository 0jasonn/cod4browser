/**
 * @typedef {{state: string, message: string, error?: unknown}} CheckpointStatus
 * @param {{checkpoint: () => Promise<unknown>, isMounted: () => boolean,
 *   onStatus?: (status: CheckpointStatus) => void,
 *   documentTarget?: Document | (EventTarget & {visibilityState: string})}} options
 */
export function createVisibilityCheckpoint({
    checkpoint,
    isMounted,
    onStatus = () => {},
    documentTarget = document,
})
{
    /** @type {Promise<boolean> | null} */
    let inFlight = null;
    let disposed = false;

    const request = () => {
        if (disposed || !isMounted()) return Promise.resolve(false);
        if (inFlight) return inFlight;
        onStatus({ state: "saving", message: "Saving browser profile" });
        const operation = Promise.resolve().then(checkpoint).then(() => {
            onStatus({ state: "saved", message: "Browser profile saved" });
            return true;
        }, (error) => {
            onStatus({
                state: "failed",
                message: "Save failed; hide the tab to retry",
                error,
            });
            throw error;
        });
        inFlight = operation;
        operation.finally(() => {
            if (inFlight === operation) inFlight = null;
        }).catch(() => {});
        return operation;
    };
    const handleVisibilityChange = () => {
        if (documentTarget.visibilityState === "hidden") void request().catch(() => {});
    };
    documentTarget.addEventListener("visibilitychange", handleVisibilityChange);

    return Object.freeze({
        request,
        dispose() {
            if (disposed) return;
            disposed = true;
            documentTarget.removeEventListener("visibilitychange", handleVisibilityChange);
        },
    });
}
