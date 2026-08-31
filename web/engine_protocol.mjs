export {
    DEFAULT_REQUEST_TIMEOUT_MS,
    ENGINE_PROTOCOL_VERSION,
    EngineWorkerError,
    protocolError,
} from "./worker_transport.mjs";

export const HOST_EVENTS = new Set([
    "kisakcod:renderer-shader", "kisakcod:renderer-texture",
    "kisakcod:renderer-aa",
    "kisakcod:test-webgl-aa", "kisakcod:renderer-surface",
    "kisakcod:renderer-surface-draw", "kisakcod:renderer-scene-view",
    "kisakcod:renderer-scene-frame", "kisakcod:renderer-fx",
    "kisakcod:state", "kisakcod:frame", "kisakcod:system", "kisakcod:engine",
    "kisakcod:runtime", "kisakcod:database", "kisakcod:canonical-gfxworld",
    "kisakcod:canonical-runtime-prefix", "kisakcod:engine-lifecycle",
    "kisakcod:canonical-filesystem",
    "kisakcod:cinematic",
    "kisakcod:renderer-memory",
    "kisakcod:renderer-lifecycle",
    "kisakcod:frame-profile",
]);
