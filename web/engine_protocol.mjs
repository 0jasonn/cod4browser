export const ENGINE_PROTOCOL_VERSION = 1;
export const DEFAULT_REQUEST_TIMEOUT_MS = 15_000;

export const HOST_EVENTS = new Set([
    "kisakcod:archive", "kisakcod:archive-progress", "kisakcod:qcommon",
    "kisakcod:retail-census", "kisakcod:renderer-shader", "kisakcod:schedule",
    "kisakcod:engine-asset", "kisakcod:renderer-texture",
    "kisakcod:engine-world-surface", "kisakcod:renderer-aa",
    "kisakcod:test-webgl-aa", "kisakcod:renderer-surface",
    "kisakcod:renderer-surface-draw", "kisakcod:renderer-scene-view",
    "kisakcod:renderer-scene-frame", "kisakcod:renderer-fx",
    "kisakcod:renderer-comparison", "kisakcod:renderer-comparison-record",
    "kisakcod:state", "kisakcod:frame", "kisakcod:system", "kisakcod:engine",
    "kisakcod:runtime", "kisakcod:database", "kisakcod:canonical-gfxworld",
    "kisakcod:canonical-runtime-prefix", "kisakcod:engine-lifecycle",
    "kisakcod:canonical-filesystem",
    "kisakcod:cinematic",
]);

export function protocolError(code, operation, message, recoverable = false, details)
{
    return { code, operation, message, recoverable, ...(details === undefined ? {} : { details }) };
}

export class EngineWorkerError extends Error
{
    constructor(error)
    {
        super(error?.message ?? "The engine Worker request failed.");
        this.name = "EngineWorkerError";
        this.code = error?.code ?? "WORKER_ERROR";
        this.operation = error?.operation ?? "unknown";
        this.recoverable = error?.recoverable === true;
        if (error?.details !== undefined) this.details = error.details;
    }
}
