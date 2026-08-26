/**
 * @param {{
 *   mount: (manifest: any) => Promise<any>,
 *   unmount: () => Promise<any>,
 *   onMounted?: (manifest: any, result: any) => void,
 *   onCleared?: (result: any) => void,
 *   onFailed?: (error: any, manifest: any) => void,
 * }} options
 */
export function createLatestMountController({
    mount,
    unmount,
    onMounted = () => {},
    onCleared = () => {},
    onFailed = () => {},
})
{
    let generation = 0;
    let pending = null;
    let draining = null;
    let activeImportId = null;
    let disposed = false;

    async function drain()
    {
        while (!disposed && pending) {
            const request = pending;
            pending = null;
            try {
                const result = request.manifest
                    ? await mount(request.manifest)
                    : await unmount();
                if (disposed || request.generation !== generation) {
                    request.resolve({ stale: true });
                    continue;
                }
                if (request.manifest && result?.runtime !== true) {
                    throw Object.assign(new Error(
                        "The canonical runtime did not accept the mounted installation."), {
                        code: "RUNTIME_MOUNT_FAILED",
                    });
                }
                activeImportId = request.manifest?.importId ?? null;
                if (request.manifest) onMounted(request.manifest, result);
                else onCleared(result);
                request.resolve(result);
            } catch (error) {
                if (disposed || request.generation !== generation) {
                    request.resolve({ stale: true });
                    continue;
                }
                activeImportId = null;
                onFailed(error, request.manifest);
                request.reject(error);
            }
        }
    }

    function schedule(manifest)
    {
        if (disposed) {
            return Promise.reject(Object.assign(new Error("Mount controller is disposed."), {
                code: "MOUNT_CONTROLLER_DISPOSED",
            }));
        }
        ++generation;
        pending?.resolve({ stale: true });
        const completion = new Promise((resolve, reject) => {
            pending = { manifest, generation, resolve, reject };
        });
        if (!draining) {
            const operation = drain();
            draining = operation;
            operation.finally(() => {
                if (draining === operation) draining = null;
            }).catch(() => {});
        }
        return completion;
    }

    return Object.freeze({
        select(manifest) {
            if (!manifest || typeof manifest.importId !== "string") {
                return Promise.reject(new TypeError("A ready installation manifest is required."));
            }
            return schedule(manifest);
        },
        clear() { return schedule(null); },
        invalidate() {
            ++generation;
            pending?.resolve({ stale: true });
            pending = null;
        },
        async dispose() {
            if (disposed) return;
            disposed = true;
            ++generation;
            pending?.resolve({ stale: true });
            pending = null;
            await draining;
        },
        get activeImportId() { return activeImportId; },
        get busy() { return Boolean(draining); },
    });
}
