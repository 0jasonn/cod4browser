/**
 * @typedef {{state: string, message: string, error?: unknown}} CheckpointStatus
 * @param {{checkpoint: () => Promise<unknown>, isMounted: () => boolean,
 *   onStatus?: (status: CheckpointStatus) => void,
 *   documentTarget?: Document | (EventTarget & {visibilityState: string}),
 *   quietDelayMs?: number, maxDirtyAgeMs?: number}} options
 */
export function createVisibilityCheckpoint({
    checkpoint,
    isMounted,
    onStatus = () => {},
    documentTarget = document,
    quietDelayMs = 7_500,
    maxDirtyAgeMs = 30_000,
})
{
    if (!Number.isFinite(quietDelayMs) || quietDelayMs <= 0 ||
        !Number.isFinite(maxDirtyAgeMs) || maxDirtyAgeMs < quietDelayMs) {
        throw new RangeError("Checkpoint delays must be positive and maxDirtyAgeMs must be at least quietDelayMs.");
    }
    /** @type {Promise<boolean> | null} */
    let inFlight = null;
    let checkpointQueued = false;
    let dirty = false;
    let dirtySince = 0;
    /** @type {ReturnType<typeof setTimeout> | null} */
    let timer = null;
    let disposed = false;

    const clearTimer = () => {
        if (timer !== null) clearTimeout(timer);
        timer = null;
    };

    const schedule = () => {
        if (disposed || inFlight || !dirty) return;
        clearTimer();
        const now = Date.now();
        const deadline = Math.min(now + quietDelayMs, dirtySince + maxDirtyAgeMs);
        timer = setTimeout(() => {
            timer = null;
            void request().catch(() => {});
        }, Math.max(0, deadline - now));
    };

    const request = () => {
        if (disposed || !isMounted()) return Promise.resolve(false);
        if (inFlight) {
            checkpointQueued = true;
            return inFlight;
        }
        clearTimer();
        onStatus({ state: "saving", message: "Saving browser profile" });
        const operation = Promise.resolve().then(async () => {
            do {
                checkpointQueued = false;
                const pendingDirty = dirty;
                const pendingDirtySince = dirtySince;
                dirty = false;
                dirtySince = 0;
                try {
                    await checkpoint();
                } catch (error) {
                    if (pendingDirty) {
                        if (!dirty || pendingDirtySince < dirtySince) {
                            dirtySince = pendingDirtySince;
                        }
                        dirty = true;
                    }
                    throw error;
                }
            } while ((checkpointQueued || dirty) && !disposed && isMounted());
        }).then(() => {
            onStatus({ state: "saved", message: "Browser profile saved" });
            return true;
        }, (error) => {
            onStatus({
                state: "failed",
                message: "Save failed; changes remain pending",
                error,
            });
            throw error;
        });
        inFlight = operation;
        operation.finally(() => {
            if (inFlight === operation) {
                inFlight = null;
                schedule();
            }
        }).catch(() => {});
        return operation;
    };

    const markDirty = () => {
        if (disposed) return false;
        if (!dirty) {
            dirty = true;
            dirtySince = Date.now();
        }
        schedule();
        return true;
    };
    const handleVisibilityChange = () => {
        if (documentTarget.visibilityState === "hidden") void request().catch(() => {});
    };
    documentTarget.addEventListener("visibilitychange", handleVisibilityChange);

    return Object.freeze({
        markDirty,
        request,
        dispose() {
            if (disposed) return;
            disposed = true;
            clearTimer();
            documentTarget.removeEventListener("visibilitychange", handleVisibilityChange);
        },
    });
}
